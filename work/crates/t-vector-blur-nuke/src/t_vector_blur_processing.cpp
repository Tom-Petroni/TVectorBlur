  float finalize_noise_value(float value, float gamma) const {
    float out = value;
    if (gamma != 1.0f) {
      if (gamma <= 0.0001f) {
        out = out >= 1.0f ? 1.0f : 0.0f;
      } else if (gamma == 0.5f) {
        if (out > 0.0f) {
          out *= out;
        }
      } else if (out > 0.0f) {
        out = std::pow(out, 1.0f / gamma);
      }
    }

    if (out < 0.0f) {
      out = 0.0f;
    } else if (out > 1.0f) {
      out = 1.0f;
    }
    return out;
  }

  float finalize_noise_value(float value) const {
    return finalize_noise_value(value, gamma_);
  }

  Vector3 project_2d_surface_from_raster(float raster_x, float raster_y) const {
    static const float kPi = 3.14159265358979323846f;
    static const float kTwoPi = 6.28318530717958647692f;

    const Format& f = format();
    const float min_x = static_cast<float>(f.x());
    const float min_y = static_cast<float>(f.y());
    const float width = std::max(1.0f, static_cast<float>(f.r() - f.x()));
    const float height = std::max(1.0f, static_cast<float>(f.t() - f.y()));
    const float u = clamp_compat((raster_x - min_x) / width, 0.0f, 1.0f);
    const float v = clamp_compat((raster_y - min_y) / height, 0.0f, 1.0f);
    const float lon = (u - 0.5f) * kTwoPi;

    if (projection_2d_ == PROJECTION_SPHERICAL) {
      const float lat = (0.5f - v) * kPi;
      const float cl = std::cos(lat);
      return Vector3(cl * std::cos(lon), std::sin(lat), cl * std::sin(lon));
    }

    // Cylindrical projection: single wrap across the frame width.
    const float h = (0.5f - v) * 2.0f;
    return Vector3(std::cos(lon), h, std::sin(lon));
  }

  Vector3 noise_point_from_input(float sx, float sy, float sz, bool pref_space,
                                 int noise_type = NOISE_PERLIN) const {
    const bool is_pattern = (sanitize_noise_type(noise_type) == NOISE_PATTERN);
    const float translate_x = static_cast<float>(pref_translate_[0]);
    const float translate_y = static_cast<float>(pref_translate_[1]);
    const float translate_z = static_cast<float>(pref_translate_[2]);
    const float local_x = pref_space
                              ? (sx - translate_x)
                              : (sx - static_cast<float>(center_[0]) - translate_x);
    const float local_y = pref_space
                              ? (sy - translate_y)
                              : (sy - static_cast<float>(center_[1]) - translate_y);
    const float local_z = pref_space ? (sz - translate_z) : sz;
    if (pref_space || projection_2d_ == PROJECTION_PLANAR) {
      return invmatrix_.transform(Vector3(local_x, local_y, local_z));
    }
    const Vector3 projected = project_2d_surface_from_raster(sx, sy);
    Vector3 out = invmatrix_.transform(Vector3(projected.x - translate_x,
                                               projected.y - translate_y,
                                               projected.z - translate_z));
    // On spherical/cylindrical 2D projection, Pattern is evaluated as a 2D motif
    // on the projected surface coordinates. This avoids concentric flattening and
    // makes spiral differ from radial.
    if (is_pattern) {
      out.z = 0.0f;
    }
    return out;
  }

  Vector3 domainwarp_point_from_input(float sx, float sy, float sz, bool pref_space,
                                      int noise_type = NOISE_PERLIN) const {
    const bool is_pattern = (sanitize_noise_type(noise_type) == NOISE_PATTERN);
    const float translate_x = static_cast<float>(domainwarp_translate_[0]);
    const float translate_y = static_cast<float>(domainwarp_translate_[1]);
    const float translate_z = static_cast<float>(domainwarp_translate_[2]);
    const float local_x = pref_space
                              ? (sx - translate_x)
                              : (sx - static_cast<float>(domainwarp_center_[0]) - translate_x);
    const float local_y = pref_space
                              ? (sy - translate_y)
                              : (sy - static_cast<float>(domainwarp_center_[1]) - translate_y);
    const float local_z = pref_space ? (sz - translate_z) : sz;
    if (pref_space || projection_2d_ == PROJECTION_PLANAR) {
      return domainwarp_invmatrix_.transform(Vector3(local_x, local_y, local_z));
    }
    const Vector3 projected = project_2d_surface_from_raster(sx, sy);
    Vector3 out = domainwarp_invmatrix_.transform(Vector3(projected.x - translate_x,
                                                          projected.y - translate_y,
                                                          projected.z - translate_z));
    if (is_pattern) {
      out.z = 0.0f;
    }
    return out;
  }

  static std::uint32_t fnv_hash4(int x, int y, int z, std::uint32_t seed) {
    const std::uint32_t kPrime = 16777619u;
    std::uint32_t h = 2166136261u;
    h = (h ^ static_cast<std::uint32_t>(x)) * kPrime;
    h = (h ^ static_cast<std::uint32_t>(y)) * kPrime;
    h = (h ^ static_cast<std::uint32_t>(z)) * kPrime;
    h = (h ^ seed) * kPrime;
    return h;
  }

  static float hash01(std::uint32_t h) {
    return static_cast<float>(h & 0x00ffffffu) / 16777215.0f;
  }

  static constexpr std::uint32_t kVoronoiRandModulus = 2147483647u;
  static constexpr float kVoronoiInvRandModulus = 4.65661287e-10f;

  static std::uint32_t voronoi_lcg_random(std::uint32_t seed) {
    if (seed == 0u) {
      seed = 1u;
    }
    constexpr std::int64_t kA = 48271;
    constexpr std::int64_t kM = 2147483647;
    constexpr std::int64_t kQ = 44488;
    constexpr std::int64_t kR = 3399;
    const std::int64_t hi = static_cast<std::int64_t>(seed) / kQ;
    const std::int64_t lo = static_cast<std::int64_t>(seed) % kQ;
    const std::int64_t test = kA * lo - kR * hi;
    return static_cast<std::uint32_t>(test > 0 ? test : test + kM);
  }

  static int voronoi_prob_lookup(std::uint32_t value) {
    if (value < 1022645910u) return 1;
    if (value < 2700834071u) return 2;
    if (value < 3819626178u) return 3;
    return 4;
  }

  static float voronoi_rand01(std::uint32_t h) {
    return static_cast<float>(h % kVoronoiRandModulus) * kVoronoiInvRandModulus;
  }

  struct VoronoiHit {
    float f1;
    float f2;
    std::uint32_t id1;
    std::uint32_t id2;
  };

  struct NoiseRGB {
    float r;
    float g;
    float b;
  };

  struct NoiseRGBA {
    float r;
    float g;
    float b;
    float a;
  };

  float output_alpha_from_sample(const NoiseRGB& n) const {
    return std::max(clamp01(n.r), std::max(clamp01(n.g), clamp01(n.b)));
  }

  static void downsample_flow_cache_box(const std::vector<float>& src,
                                        int src_w,
                                        int src_h,
                                        int factor,
                                        std::vector<float>& dst,
                                        int& dst_w,
                                        int& dst_h) {
    const int safe_factor = std::max(1, factor);
    dst_w = std::max(1, (src_w + safe_factor - 1) / safe_factor);
    dst_h = std::max(1, (src_h + safe_factor - 1) / safe_factor);
    dst.assign(static_cast<size_t>(dst_w) * static_cast<size_t>(dst_h) * 3u, 0.0f);

    auto src_idx = [src_w](int x, int y, int c) -> size_t {
      return (static_cast<size_t>(y) * static_cast<size_t>(src_w) +
              static_cast<size_t>(x)) * 3u +
             static_cast<size_t>(c);
    };
    auto dst_idx = [dst_w](int x, int y, int c) -> size_t {
      return (static_cast<size_t>(y) * static_cast<size_t>(dst_w) +
              static_cast<size_t>(x)) * 3u +
             static_cast<size_t>(c);
    };

    for (int y = 0; y < dst_h; ++y) {
      const int y0 = y * safe_factor;
      const int y1 = std::min(y0 + safe_factor, src_h);
      for (int x = 0; x < dst_w; ++x) {
        const int x0 = x * safe_factor;
        const int x1 = std::min(x0 + safe_factor, src_w);
        float sum_r = 0.0f;
        float sum_g = 0.0f;
        float sum_b = 0.0f;
        int count = 0;
        for (int yy = y0; yy < y1; ++yy) {
          for (int xx = x0; xx < x1; ++xx) {
            sum_r += src[src_idx(xx, yy, 0)];
            sum_g += src[src_idx(xx, yy, 1)];
            sum_b += src[src_idx(xx, yy, 2)];
            ++count;
          }
        }
        const float inv_count = (count > 0) ? (1.0f / static_cast<float>(count)) : 0.0f;
        dst[dst_idx(x, y, 0)] = sum_r * inv_count;
        dst[dst_idx(x, y, 1)] = sum_g * inv_count;
        dst[dst_idx(x, y, 2)] = sum_b * inv_count;
      }
    }
  }

  static void upsample_flow_cache_bilinear(const std::vector<float>& src,
                                           int src_w,
                                           int src_h,
                                           std::vector<float>& dst,
                                           int dst_w,
                                           int dst_h) {
    dst.assign(static_cast<size_t>(dst_w) * static_cast<size_t>(dst_h) * 3u, 0.0f);
    if (src.empty() || src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) {
      return;
    }

    auto src_idx = [src_w](int x, int y, int c) -> size_t {
      return (static_cast<size_t>(y) * static_cast<size_t>(src_w) +
              static_cast<size_t>(x)) * 3u +
             static_cast<size_t>(c);
    };
    auto dst_idx = [dst_w](int x, int y, int c) -> size_t {
      return (static_cast<size_t>(y) * static_cast<size_t>(dst_w) +
              static_cast<size_t>(x)) * 3u +
             static_cast<size_t>(c);
    };

    for (int y = 0; y < dst_h; ++y) {
      const float sy = (dst_h > 1 && src_h > 1)
                           ? (static_cast<float>(y) * static_cast<float>(src_h - 1) /
                              static_cast<float>(dst_h - 1))
                           : 0.0f;
      const int y0 = clamp_compat(static_cast<int>(std::floor(sy)), 0, src_h - 1);
      const int y1 = std::min(y0 + 1, src_h - 1);
      const float ty = sy - static_cast<float>(y0);
      for (int x = 0; x < dst_w; ++x) {
        const float sx = (dst_w > 1 && src_w > 1)
                             ? (static_cast<float>(x) * static_cast<float>(src_w - 1) /
                                static_cast<float>(dst_w - 1))
                             : 0.0f;
        const int x0 = clamp_compat(static_cast<int>(std::floor(sx)), 0, src_w - 1);
        const int x1 = std::min(x0 + 1, src_w - 1);
        const float tx = sx - static_cast<float>(x0);
        for (int c = 0; c < 3; ++c) {
          const float v00 = src[src_idx(x0, y0, c)];
          const float v10 = src[src_idx(x1, y0, c)];
          const float v01 = src[src_idx(x0, y1, c)];
          const float v11 = src[src_idx(x1, y1, c)];
          const float vx0 = v00 + (v10 - v00) * tx;
          const float vx1 = v01 + (v11 - v01) * tx;
          dst[dst_idx(x, y, c)] = vx0 + (vx1 - vx0) * ty;
        }
      }
    }
  }

  static void box_blur_horizontal_rgb(const std::vector<float>& src,
                                      std::vector<float>& dst,
                                      int width,
                                      int height,
                                      int radius) {
    if (radius <= 0 || width <= 1 || height <= 0 || src.empty()) {
      dst = src;
      return;
    }
    dst.assign(src.size(), 0.0f);
    const int r = std::max(1, radius);
    const int window = r * 2 + 1;
    const float inv_window = 1.0f / static_cast<float>(window);

    auto idx = [width](int x, int y, int c) -> size_t {
      return (static_cast<size_t>(y) * static_cast<size_t>(width) +
              static_cast<size_t>(x)) * 3u +
             static_cast<size_t>(c);
    };

    for (int y = 0; y < height; ++y) {
      float sum_r = 0.0f;
      float sum_g = 0.0f;
      float sum_b = 0.0f;
      for (int k = -r; k <= r; ++k) {
        const int sx = clamp_compat(k, 0, width - 1);
        sum_r += src[idx(sx, y, 0)];
        sum_g += src[idx(sx, y, 1)];
        sum_b += src[idx(sx, y, 2)];
      }

      for (int x = 0; x < width; ++x) {
        dst[idx(x, y, 0)] = sum_r * inv_window;
        dst[idx(x, y, 1)] = sum_g * inv_window;
        dst[idx(x, y, 2)] = sum_b * inv_window;

        const int remove_x = clamp_compat(x - r, 0, width - 1);
        const int add_x = clamp_compat(x + r + 1, 0, width - 1);
        sum_r += src[idx(add_x, y, 0)] - src[idx(remove_x, y, 0)];
        sum_g += src[idx(add_x, y, 1)] - src[idx(remove_x, y, 1)];
        sum_b += src[idx(add_x, y, 2)] - src[idx(remove_x, y, 2)];
      }
    }
  }

  static void box_blur_vertical_rgb(const std::vector<float>& src,
                                    std::vector<float>& dst,
                                    int width,
                                    int height,
                                    int radius) {
    if (radius <= 0 || height <= 1 || width <= 0 || src.empty()) {
      dst = src;
      return;
    }
    dst.assign(src.size(), 0.0f);
    const int r = std::max(1, radius);
    const int window = r * 2 + 1;
    const float inv_window = 1.0f / static_cast<float>(window);

    auto idx = [width](int x, int y, int c) -> size_t {
      return (static_cast<size_t>(y) * static_cast<size_t>(width) +
              static_cast<size_t>(x)) * 3u +
             static_cast<size_t>(c);
    };

    for (int x = 0; x < width; ++x) {
      float sum_r = 0.0f;
      float sum_g = 0.0f;
      float sum_b = 0.0f;
      for (int k = -r; k <= r; ++k) {
        const int sy = clamp_compat(k, 0, height - 1);
        sum_r += src[idx(x, sy, 0)];
        sum_g += src[idx(x, sy, 1)];
        sum_b += src[idx(x, sy, 2)];
      }

      for (int y = 0; y < height; ++y) {
        dst[idx(x, y, 0)] = sum_r * inv_window;
        dst[idx(x, y, 1)] = sum_g * inv_window;
        dst[idx(x, y, 2)] = sum_b * inv_window;

        const int remove_y = clamp_compat(y - r, 0, height - 1);
        const int add_y = clamp_compat(y + r + 1, 0, height - 1);
        sum_r += src[idx(x, add_y, 0)] - src[idx(x, remove_y, 0)];
        sum_g += src[idx(x, add_y, 1)] - src[idx(x, remove_y, 1)];
        sum_b += src[idx(x, add_y, 2)] - src[idx(x, remove_y, 2)];
      }
    }
  }

  static void blur_flow_cache_with_box_gaussian(std::vector<float>& data,
                                                int width,
                                                int height,
                                                float sigma) {
    if (sigma <= 1e-6f || width <= 1 || height <= 1 || data.empty()) {
      return;
    }

    constexpr int kPasses = 3;
    const float ideal = std::sqrt((12.0f * sigma * sigma / static_cast<float>(kPasses)) + 1.0f);
    int low_size = static_cast<int>(std::floor(ideal));
    if ((low_size & 1) == 0) {
      --low_size;
    }
    low_size = std::max(1, low_size);
    const int high_size = low_size + 2;
    const float m_ideal =
        (12.0f * sigma * sigma -
         static_cast<float>(kPasses * low_size * low_size) -
         static_cast<float>(4 * kPasses * low_size) -
         static_cast<float>(3 * kPasses)) /
        static_cast<float>(-4 * low_size - 4);
    const int low_pass_count =
        clamp_compat(static_cast<int>(std::round(m_ideal)), 0, kPasses);

    int radii[kPasses] = {};
    for (int i = 0; i < kPasses; ++i) {
      const int size = (i < low_pass_count) ? low_size : high_size;
      radii[i] = std::max(0, (size - 1) / 2);
    }

    std::vector<float> tmp_a;
    std::vector<float> tmp_b;
    for (int i = 0; i < kPasses; ++i) {
      const int r = radii[i];
      if (r <= 0) {
        continue;
      }
      box_blur_horizontal_rgb(data, tmp_a, width, height, r);
      box_blur_vertical_rgb(tmp_a, tmp_b, width, height, r);
      data.swap(tmp_b);
    }
  }

  static void apply_gaussian_blur_to_flow_cache(std::vector<float>& data,
                                                int width,
                                                int height,
                                                float radius) {
    if (radius <= 1e-6f || width <= 1 || height <= 1 || data.empty()) {
      return;
    }

    const float sigma = std::max(0.1f, radius);
    int downsample_factor = 1;
    while ((sigma / static_cast<float>(downsample_factor)) > 4.0f &&
           (width / (downsample_factor * 2)) >= 2 &&
           (height / (downsample_factor * 2)) >= 2 &&
           downsample_factor < 16) {
      downsample_factor *= 2;
    }

    std::vector<float> work;
    int work_w = width;
    int work_h = height;
    if (downsample_factor > 1) {
      downsample_flow_cache_box(data, width, height, downsample_factor, work, work_w, work_h);
    } else {
      work = data;
    }

    const float effective_sigma = sigma / static_cast<float>(downsample_factor);
    blur_flow_cache_with_box_gaussian(work, work_w, work_h, effective_sigma);

    if (downsample_factor > 1) {
      std::vector<float> upsampled;
      upsample_flow_cache_bilinear(work, work_w, work_h, upsampled, width, height);
      data.swap(upsampled);
    } else {
      data.swap(work);
    }
  }

  static int domainwarp_map_cache_downsample_factor(float blur_radius,
                                                    int full_width,
                                                    int full_height) {
    if (blur_radius <= 1e-6f || full_width <= 1 || full_height <= 1) {
      return 1;
    }

    int blur_factor = 4;
    if (blur_radius >= 1.0f) {
      blur_factor = 4;
    }
    if (blur_radius >= 6.0f) {
      blur_factor = 8;
    }
    if (blur_radius >= 20.0f) {
      blur_factor = 16;
    }

    // Keep cache generation bounded for interactive playback on large formats.
    std::int64_t target_samples = 180000;
    const std::int64_t full_pixels =
        static_cast<std::int64_t>(full_width) * static_cast<std::int64_t>(full_height);
    int density_factor = 1;
    while (density_factor < 16 &&
           (full_pixels / (static_cast<std::int64_t>(density_factor) *
                           static_cast<std::int64_t>(density_factor))) > target_samples) {
      density_factor *= 2;
    }

    int factor = std::max(blur_factor, density_factor);

    while (factor > 1 &&
           (((full_width + factor - 1) / factor) < 2 ||
            ((full_height + factor - 1) / factor) < 2)) {
      factor /= 2;
    }
    return std::max(1, factor);
  }

  bool build_domainwarp_flow_cache_view(const DomainwarpFlowCacheKey& key,
                                        DomainwarpFlowCacheView& view) {
    view = DomainwarpFlowCacheView{};
    Guard build_guard(domainwarp_flow_build_lock_);
    if (key.format_r <= key.format_x || key.format_t <= key.format_y) {
      return false;
    }

    const int full_width = key.format_r - key.format_x;
    const int full_height = key.format_t - key.format_y;
    if (full_width <= 0 || full_height <= 0) {
      Guard guard(domainwarp_flow_cache_lock_);
      domainwarp_flow_cache_valid_ = false;
      domainwarp_flow_cache_width_ = 0;
      domainwarp_flow_cache_height_ = 0;
      domainwarp_flow_cache_data_.reset();
      domainwarp_flow_base_cache_valid_ = false;
      domainwarp_flow_base_cache_width_ = 0;
      domainwarp_flow_base_cache_height_ = 0;
      domainwarp_flow_base_cache_data_.reset();
      return false;
    }
    const int cache_factor = domainwarp_map_cache_downsample_factor(
        key.map_blur,
        full_width,
        full_height);
    const int width = std::max(1, (full_width + cache_factor - 1) / cache_factor);
    const int height = std::max(1, (full_height + cache_factor - 1) / cache_factor);
    const float sample_scale_x =
        (width > 1 && full_width > 1)
            ? static_cast<float>(full_width - 1) / static_cast<float>(width - 1)
            : 1.0f;
    const float sample_scale_y =
        (height > 1 && full_height > 1)
            ? static_cast<float>(full_height - 1) / static_cast<float>(height - 1)
            : 1.0f;

    auto fill_view_from_base = [&]() {
      view.storage = domainwarp_flow_base_cache_data_;
      view.data = (view.storage && !view.storage->empty()) ? view.storage->data() : nullptr;
      view.x = domainwarp_flow_base_cache_key_.format_x;
      view.y = domainwarp_flow_base_cache_key_.format_y;
      view.width = domainwarp_flow_base_cache_width_;
      view.height = domainwarp_flow_base_cache_height_;
      view.sample_scale_x = sample_scale_x;
      view.sample_scale_y = sample_scale_y;
    };
    auto fill_view_from_blur = [&]() {
      view.storage = domainwarp_flow_cache_data_;
      view.data = (view.storage && !view.storage->empty()) ? view.storage->data() : nullptr;
      view.x = domainwarp_flow_cache_key_.format_x;
      view.y = domainwarp_flow_cache_key_.format_y;
      view.width = domainwarp_flow_cache_width_;
      view.height = domainwarp_flow_cache_height_;
      view.sample_scale_x = sample_scale_x;
      view.sample_scale_y = sample_scale_y;
    };

    bool need_base_build = false;
    bool need_blur_build = false;
    std::vector<float> blur_source;

    {
      Guard guard(domainwarp_flow_cache_lock_);
      const bool base_match =
          domainwarp_flow_base_cache_valid_ &&
          domainwarp_flow_base_cache_width_ == width &&
          domainwarp_flow_base_cache_height_ == height &&
          domainwarp_flow_base_cache_key_equal(domainwarp_flow_base_cache_key_, key);
      if (!base_match) {
        need_base_build = true;
      } else if (key.map_blur <= 1e-6f) {
        fill_view_from_base();
        return true;
      } else {
        const bool blur_match =
            domainwarp_flow_cache_valid_ &&
            domainwarp_flow_cache_width_ == width &&
            domainwarp_flow_cache_height_ == height &&
            domainwarp_flow_cache_key_equal(domainwarp_flow_cache_key_, key);
        if (blur_match) {
          fill_view_from_blur();
          return true;
        }
        need_blur_build = true;
        blur_source = domainwarp_flow_base_cache_data_
                          ? *domainwarp_flow_base_cache_data_
                          : std::vector<float>{};
      }
    }

    if (need_base_build) {
      std::vector<float> base_data;
      base_data.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 3u);
      size_t idx = 0;
      for (int yy = 0; yy < height; ++yy) {
        const float sy = std::min(
            static_cast<float>(key.format_t - 1),
            static_cast<float>(key.format_y) + static_cast<float>(yy) * sample_scale_y);
        for (int xx = 0; xx < width; ++xx) {
          const float sx = std::min(
              static_cast<float>(key.format_r - 1),
              static_cast<float>(key.format_x) + static_cast<float>(xx) * sample_scale_x);
          const NoiseRGB flow = evaluate_domainwarp_map(
              sx + key.relative_tx,
              sy + key.relative_ty,
              0.0f,
              false,
              key.seed,
              key.use_time != 0);
          base_data[idx + 0] = flow.r;
          base_data[idx + 1] = flow.g;
          base_data[idx + 2] = flow.b;
          idx += 3;
        }
      }

      Guard guard(domainwarp_flow_cache_lock_);
      const bool base_match_now =
          domainwarp_flow_base_cache_valid_ &&
          domainwarp_flow_base_cache_width_ == width &&
          domainwarp_flow_base_cache_height_ == height &&
          domainwarp_flow_base_cache_key_equal(domainwarp_flow_base_cache_key_, key);
      if (!base_match_now) {
        domainwarp_flow_base_cache_data_ =
            std::make_shared<std::vector<float>>(std::move(base_data));
        domainwarp_flow_base_cache_key_ = key;
        domainwarp_flow_base_cache_width_ = width;
        domainwarp_flow_base_cache_height_ = height;
        domainwarp_flow_base_cache_valid_ = true;

        domainwarp_flow_cache_valid_ = false;
        domainwarp_flow_cache_data_.reset();
      }

      if (!domainwarp_flow_base_cache_valid_ ||
          domainwarp_flow_base_cache_width_ <= 0 ||
          domainwarp_flow_base_cache_height_ <= 0 ||
          !domainwarp_flow_base_cache_data_ ||
          domainwarp_flow_base_cache_data_->empty()) {
        return false;
      }

      if (key.map_blur <= 1e-6f) {
        fill_view_from_base();
        return true;
      }

      const bool blur_match_now =
          domainwarp_flow_cache_valid_ &&
          domainwarp_flow_cache_width_ == width &&
          domainwarp_flow_cache_height_ == height &&
          domainwarp_flow_cache_key_equal(domainwarp_flow_cache_key_, key);
      if (blur_match_now) {
        fill_view_from_blur();
        return true;
      }
      need_blur_build = true;
      blur_source = *domainwarp_flow_base_cache_data_;
    }

    if (key.map_blur <= 1e-6f) {
      Guard guard(domainwarp_flow_cache_lock_);
      if (!domainwarp_flow_base_cache_valid_ ||
          domainwarp_flow_base_cache_width_ <= 0 ||
          domainwarp_flow_base_cache_height_ <= 0 ||
          !domainwarp_flow_base_cache_data_ ||
          domainwarp_flow_base_cache_data_->empty()) {
        return false;
      }
      fill_view_from_base();
      return true;
    }

    if (!need_blur_build) {
      Guard guard(domainwarp_flow_cache_lock_);
      const bool blur_match_now =
          domainwarp_flow_cache_valid_ &&
          domainwarp_flow_cache_width_ == width &&
          domainwarp_flow_cache_height_ == height &&
          domainwarp_flow_cache_key_equal(domainwarp_flow_cache_key_, key);
      if (blur_match_now) {
        fill_view_from_blur();
        return true;
      }
      if (!domainwarp_flow_base_cache_valid_ ||
          domainwarp_flow_base_cache_width_ <= 0 ||
          domainwarp_flow_base_cache_height_ <= 0 ||
          !domainwarp_flow_base_cache_data_ ||
          domainwarp_flow_base_cache_data_->empty()) {
        return false;
      }
      need_blur_build = true;
      blur_source = *domainwarp_flow_base_cache_data_;
    }

    if (need_blur_build) {
      std::vector<float> blurred_data = std::move(blur_source);
      const float scaled_blur =
          key.map_blur / std::max(1.0f, static_cast<float>(cache_factor));
      apply_gaussian_blur_to_flow_cache(blurred_data, width, height, scaled_blur);

      Guard guard(domainwarp_flow_cache_lock_);
      const bool blur_match_now =
          domainwarp_flow_cache_valid_ &&
          domainwarp_flow_cache_width_ == width &&
          domainwarp_flow_cache_height_ == height &&
          domainwarp_flow_cache_key_equal(domainwarp_flow_cache_key_, key);
      if (!blur_match_now) {
        domainwarp_flow_cache_data_ =
            std::make_shared<std::vector<float>>(std::move(blurred_data));
        domainwarp_flow_cache_key_ = key;
        domainwarp_flow_cache_width_ = width;
        domainwarp_flow_cache_height_ = height;
        domainwarp_flow_cache_valid_ = true;
      }

      if (!domainwarp_flow_cache_valid_ ||
          domainwarp_flow_cache_width_ <= 0 ||
          domainwarp_flow_cache_height_ <= 0 ||
          !domainwarp_flow_cache_data_ ||
          domainwarp_flow_cache_data_->empty()) {
        return false;
      }
      fill_view_from_blur();
      return true;
    }

    return false;
  }

  static bool sample_domainwarp_flow_cache(const DomainwarpFlowCacheView* view,
                                           float sx,
                                           float sy,
                                           NoiseRGB& out) {
    if (!view || !view->data || view->width <= 0 || view->height <= 0) {
      return false;
    }

    const float inv_scale_x = 1.0f / std::max(1e-6f, view->sample_scale_x);
    const float inv_scale_y = 1.0f / std::max(1e-6f, view->sample_scale_y);
    const float local_x = (sx - static_cast<float>(view->x)) * inv_scale_x;
    const float local_y = (sy - static_cast<float>(view->y)) * inv_scale_y;
    if (!std::isfinite(local_x) || !std::isfinite(local_y)) {
      return false;
    }
    const float max_x = static_cast<float>(view->width - 1);
    const float max_y = static_cast<float>(view->height - 1);
    if (local_x < -1.0f || local_y < -1.0f ||
        local_x > (max_x + 1.0f) || local_y > (max_y + 1.0f)) {
      return false;
    }
    // Clamp-to-edge instead of rejecting out-of-bounds samples.
    // This avoids hard path switches (cache vs direct sampling) that can create seams.
    const float clamped_x = clamp_compat(local_x, 0.0f, max_x);
    const float clamped_y = clamp_compat(local_y, 0.0f, max_y);

    const int x0 = clamp_compat(static_cast<int>(std::floor(clamped_x)), 0, view->width - 1);
    const int y0 = clamp_compat(static_cast<int>(std::floor(clamped_y)), 0, view->height - 1);
    const int x1 = std::min(x0 + 1, view->width - 1);
    const int y1 = std::min(y0 + 1, view->height - 1);

    const float tx = clamp_compat(clamped_x - static_cast<float>(x0), 0.0f, 1.0f);
    const float ty = clamp_compat(clamped_y - static_cast<float>(y0), 0.0f, 1.0f);

    const size_t row0 = static_cast<size_t>(y0) * static_cast<size_t>(view->width) * 3u;
    const size_t row1 = static_cast<size_t>(y1) * static_cast<size_t>(view->width) * 3u;
    const float* p00 = view->data + row0 + static_cast<size_t>(x0) * 3u;
    const float* p10 = view->data + row0 + static_cast<size_t>(x1) * 3u;
    const float* p01 = view->data + row1 + static_cast<size_t>(x0) * 3u;
    const float* p11 = view->data + row1 + static_cast<size_t>(x1) * 3u;

    const float r00 = p00[0];
    const float g00 = p00[1];
    const float b00 = p00[2];
    const float r10 = p10[0];
    const float g10 = p10[1];
    const float b10 = p10[2];
    const float r01 = p01[0];
    const float g01 = p01[1];
    const float b01 = p01[2];
    const float r11 = p11[0];
    const float g11 = p11[1];
    const float b11 = p11[2];

    const float rx0 = r00 + (r10 - r00) * tx;
    const float gx0 = g00 + (g10 - g00) * tx;
    const float bx0 = b00 + (b10 - b00) * tx;
    const float rx1 = r01 + (r11 - r01) * tx;
    const float gx1 = g01 + (g11 - g01) * tx;
    const float bx1 = b01 + (b11 - b01) * tx;

    out.r = rx0 + (rx1 - rx0) * ty;
    out.g = gx0 + (gx1 - gx0) * ty;
    out.b = bx0 + (bx1 - bx0) * ty;
    return true;
  }

  float quantize_steps_from_strength() const {
    const float strength = std::max(0.0f, static_cast<float>(quantize_levels_));
    if (strength <= 1e-6f) {
      return 0.0f;
    }
    // Non-linear remap so visible posterization starts earlier in the slider.
    // 0 => disabled, 100 => very strong quantization.
    const float t = strength * 0.01f;
    const float shaped = std::pow(t, 0.35f);
    return std::max(2.0f, 257.0f - shaped * 255.0f);
  }

  static float quantize_output_component(float v, float levels, bool signed_mode) {
    if (levels <= 1.0f) {
      return v;
    }
    if (signed_mode) {
      const float v01 = clamp01(v * 0.5f + 0.5f);
      const float q01 = quantize_unit_value(v01, levels);
      return clamp_compat(q01 * 2.0f - 1.0f, -1.0f, 1.0f);
    }
    return quantize_unit_value(v, levels);
  }

  bool output_uses_blue_channel(bool pref_space) const {
    if (pref_space) {
      return true;
    }
    return !(output_mode_ == OUTPUT_VECTORS || output_mode_ == OUTPUT_STMAP);
  }

  NoiseRGB apply_quantize_output_rgb(const NoiseRGB& in,
                                     float levels,
                                     bool pref_space) const {
    if (levels <= 1.0f) {
      return in;
    }
    const bool signed_mode = (output_mode_ == OUTPUT_VECTORS);
    const float out_r = quantize_output_component(in.r, levels, signed_mode);
    const float out_g = quantize_output_component(in.g, levels, signed_mode);
    const float out_b = output_uses_blue_channel(pref_space)
                            ? quantize_output_component(in.b, levels, signed_mode)
                            : 0.0f;
    return {out_r, out_g, out_b};
  }

  static float remap_to_unit(float v, bool signed_mode) {
    if (signed_mode) {
      return clamp01(v * 0.5f + 0.5f);
    }
    return clamp01(v);
  }

  static float remap_from_unit(float v01, bool signed_mode) {
    if (signed_mode) {
      return clamp_compat(v01 * 2.0f - 1.0f, -1.0f, 1.0f);
    }
    return clamp01(v01);
  }

  struct HalftoneCellCoords {
    float u = 0.0f;
    float v = 0.0f;
    float w = 0.0f;
    bool pref_space = false;
  };

  static float halftone_smoothstep01(float t) {
    const float c = clamp_compat(t, 0.0f, 1.0f);
    return c * c * (3.0f - 2.0f * c);
  }

  static float halftone_threshold_ascending_inward(float x, float threshold, float smooth) {
    const float s = std::max(0.0f, smooth);
    if (s <= 1e-6f) {
      return (x >= threshold) ? 1.0f : 0.0f;
    }
    if (x <= threshold) {
      return 0.0f;
    }
    if (x >= threshold + s) {
      return 1.0f;
    }
    const float t = (x - threshold) / std::max(s, 1e-6f);
    return halftone_smoothstep01(t);
  }

  static float halftone_threshold_descending_inward(float x, float threshold, float smooth) {
    const float s = std::max(0.0f, smooth);
    if (s <= 1e-6f) {
      return (x <= threshold) ? 1.0f : 0.0f;
    }
    if (x <= threshold - s) {
      return 1.0f;
    }
    if (x >= threshold) {
      return 0.0f;
    }
    const float t = (x - (threshold - s)) / std::max(s, 1e-6f);
    return 1.0f - halftone_smoothstep01(t);
  }

  HalftoneCellCoords halftone_cell_coords(float sx, float sy, float sz,
                                          int sample_x, int sample_y,
                                          bool pref_space) const {
    HalftoneCellCoords out;
    out.pref_space = pref_space;
    const float cell = std::max(1.0f, static_cast<float>(halftone_cell_size_));
    if (!pref_space) {
      out.u = (static_cast<float>(sample_x) + 0.5f) / cell;
      out.v = (static_cast<float>(sample_y) + 0.5f) / cell;
      out.w = 0.0f;
      return out;
    }

    // In PRef mode, keep spacing tied to object/noise scale.
    const float base_scale = std::max(1.0f, static_cast<float>(xsize_));
    const float step = std::max(1e-4f, (cell * 10.0f) / base_scale);
    out.u = sx / step;
    out.v = sy / step;
    out.w = sz / step;
    return out;
  }

  float halftone_dots_mask(const HalftoneCellCoords& coords,
                           float tone01,
                           float smooth) const {
    const float tone = clamp01(tone01);
    if (tone <= 0.0015f) {
      // Drop tiny residual dots in near-black regions.
      return 0.0f;
    }
    const float radius_limit = coords.pref_space ? 0.57735026919f : 0.70710678118f;
    const float radius = radius_limit * std::sqrt(tone);
    if (radius <= 0.04f) {
      return 0.0f;
    }
    const float fx = (coords.u - std::floor(coords.u)) - 0.5f;
    const float fy = (coords.v - std::floor(coords.v)) - 0.5f;
    const float fz = (coords.w - std::floor(coords.w)) - 0.5f;
    const float dist = coords.pref_space
                           ? (std::sqrt(fx * fx + fy * fy + fz * fz) * 1.15470053838f)
                           : (std::sqrt(fx * fx + fy * fy) * 1.41421356237f);
    const float soft = clamp_compat(smooth, 0.0f, 0.5f);
    return halftone_threshold_descending_inward(dist, radius, soft);
  }

  static float halftone_line_energy(float coord) {
    const float f = coord - std::floor(coord);
    return 1.0f - std::fabs(f - 0.5f) * 2.0f;
  }

  float halftone_hatches_mask(const HalftoneCellCoords& coords,
                              float tone01,
                              float smooth) const {
    const float tone = clamp01(tone01);
    if (tone <= 1e-6f) {
      return 0.0f;
    }
    const float angle0 = radians(static_cast<float>(halftone_hatch_angle_));
    const float c0 = std::cos(angle0);
    const float s0 = std::sin(angle0);
    const float ru = coords.u * c0 - coords.v * s0;
    const float rv = coords.u * s0 + coords.v * c0;

    const int layer_count = clamp_compat(halftone_hatch_count_, 1, 8);
    const float step = 3.14159265358979323846f / static_cast<float>(layer_count);
    float energy = 0.0f;
    for (int i = 0; i < layer_count; ++i) {
      const float a = step * static_cast<float>(i);
      const float ca = std::cos(a);
      const float sa = std::sin(a);
      const float line_coord = ru * ca + rv * sa;
      energy = std::max(energy, halftone_line_energy(line_coord));
    }

    const float threshold = 1.0f - tone;
    const float aa_soft = coords.pref_space ? 0.0125f : 0.018f;
    const float soft = clamp_compat(std::max(smooth, aa_soft), 0.0f, 0.5f);
    return halftone_threshold_ascending_inward(energy, threshold, soft);
  }

  NoiseRGB apply_halftone_output_rgb(const NoiseRGB& base,
                                     const NoiseRGB& sample,
                                     int sample_x, int sample_y,
                                     float sx, float sy, float sz,
                                     bool pref_space) const {
    if (!halftone_enable_) {
      return base;
    }

    const float strength =
        clamp_compat(static_cast<float>(halftone_strength_), 0.0f, 1.0f);
    if (strength <= 1e-6f) {
      return base;
    }

    const int mode = clamp_compat(
        halftone_mode_,
        static_cast<int>(HALFTONE_DOTS),
        static_cast<int>(HALFTONE_HATCHES));
    const float smooth = clamp_compat(static_cast<float>(halftone_smoothness_), 0.0f, 1.0f) * 0.5f;
    const bool invert = halftone_invert_;
    const HalftoneCellCoords coords =
        halftone_cell_coords(sx, sy, sz, sample_x, sample_y, pref_space);
    const bool signed_mode = (output_mode_ == OUTPUT_VECTORS);
    const bool blue_enabled = output_uses_blue_channel(pref_space);
    const float sample_b = blue_enabled ? sample.b : 0.0f;
    const float tone01 = clamp01(
        remap_to_unit(sample.r, signed_mode) * 0.2126f +
        remap_to_unit(sample.g, signed_mode) * 0.7152f +
        remap_to_unit(sample_b, signed_mode) * 0.0722f);
    const float tone_for_pattern = invert ? (1.0f - tone01) : tone01;

    float pattern = (mode == HALFTONE_HATCHES)
                        ? halftone_hatches_mask(coords, tone_for_pattern, smooth)
                        : halftone_dots_mask(coords, tone_for_pattern, smooth);

    const auto apply_component = [&](float base_v, float sample_v) -> float {
      const float base01 = remap_to_unit(base_v, signed_mode);
      const float sample01 = remap_to_unit(sample_v, signed_mode);
      const float halftone01 = sample01 * pattern;
      const float out01 = base01 + (halftone01 - base01) * strength;
      return remap_from_unit(out01, signed_mode);
    };

    return {
        apply_component(base.r, sample.r),
        apply_component(base.g, sample.g),
        blue_enabled ? apply_component(base.b, sample.b) : 0.0f,
    };
  }

  static float fractf(float v) {
    return v - std::floor(v);
  }

  float voronoi_distance(float dx, float dy, float dz, float dw, bool use_4d,
                         int distance_function, float minkowski_exp) const {
    const float adx = std::fabs(dx);
    const float ady = std::fabs(dy);
    const float adz = std::fabs(dz);
    const float adw = use_4d ? std::fabs(dw) : 0.0f;
    switch (distance_function) {
      case VORONOI_EUCLIDEAN:
        // Keep squared distance in the hot loop, take sqrt only once after the search.
        return dx * dx + dy * dy + dz * dz + (use_4d ? (dw * dw) : 0.0f);
      case VORONOI_MANHATTAN:
        return adx + ady + adz + adw;
      case VORONOI_CHEBYSHEV:
        return use_4d
                   ? std::max(std::max(adx, ady), std::max(adz, adw))
                   : std::max(adx, std::max(ady, adz));
      case VORONOI_MINKOWSKI: {
        const float p = std::max(0.1f, minkowski_exp);
        const float inv_p = 1.0f / p;
        float sum = std::pow(adx, p) + std::pow(ady, p) + std::pow(adz, p);
        if (use_4d) {
          sum += std::pow(adw, p);
        }
        return std::pow(sum, inv_p);
      }
      default: {
        return dx * dx + dy * dy + dz * dz + (use_4d ? (dw * dw) : 0.0f);
      }
    }
  }

  static float distance_to_interval(float v, float vmin, float vmax) {
    if (v < vmin) {
      return vmin - v;
    }
    if (v > vmax) {
      return v - vmax;
    }
    return 0.0f;
  }

  float voronoi_cell_lower_bound(float x, float y, float z, float w, bool use_4d,
                                 int cX, int cY, int cZ, int cW,
                                 float bias, float rand_amt,
                                 int distance_function, float minkowski_exp) const {
    const float min_x = static_cast<float>(cX) + bias;
    const float min_y = static_cast<float>(cY) + bias;
    const float min_z = static_cast<float>(cZ) + bias;
    const float min_w = static_cast<float>(cW) + bias;
    const float max_x = min_x + rand_amt;
    const float max_y = min_y + rand_amt;
    const float max_z = min_z + rand_amt;
    const float max_w = min_w + rand_amt;

    const float dx = distance_to_interval(x, min_x, max_x);
    const float dy = distance_to_interval(y, min_y, max_y);
    const float dz = distance_to_interval(z, min_z, max_z);
    const float dw = use_4d ? distance_to_interval(w, min_w, max_w) : 0.0f;

    // Exact lower bound for each metric represented by voronoi_distance.
    return voronoi_distance(dx, dy, dz, dw, use_4d, distance_function, minkowski_exp);
  }

  VoronoiHit voronoi_search(float x, float y, float z, float w, bool use_time,
                            int distance_function, float jitter_modifier,
                            float minkowski_exp,
                            bool need_second_hit) const {
    float best_dist0 = 1e20f;
    float best_dist1 = 1e20f;
    std::uint32_t best_id0 = 0u;
    std::uint32_t best_id1 = 0u;
    const float wt = use_time ? w : 0.0f;

    const int cx = static_cast<int>(std::floor(x));
    const int cy = static_cast<int>(std::floor(y));
    const int cz = static_cast<int>(std::floor(z));
    const int cw = static_cast<int>(std::floor(wt));
    const float rand_amt = std::max(0.0f, jitter_modifier);
    const float bias = 0.5f * (1.0f - rand_amt);
    constexpr std::uint32_t user_seed = 1337u;

    const int dw_min = use_time ? -1 : 0;
    const int dw_max = use_time ? 1 : 0;
    for (int dw = dw_min; dw <= dw_max; ++dw) {
      for (int dz = -1; dz <= 1; ++dz) {
        for (int dy = -1; dy <= 1; ++dy) {
          for (int dx = -1; dx <= 1; ++dx) {
            const int cX = cx + dx;
            const int cY = cy + dy;
            const int cZ = cz + dz;
            const int cW = cw + dw;
            const float cell_lower_bound = voronoi_cell_lower_bound(
                x, y, z, wt, use_time,
                cX, cY, cZ, cW,
                bias, rand_amt, distance_function, minkowski_exp);
            const float prune_bound = need_second_hit ? best_dist1 : best_dist0;
            if (cell_lower_bound >= prune_bound) {
              continue;
            }
            std::uint32_t rng = use_time
                                    ? voronoi_lcg_random(
                                          fnv_hash5(cX, cY, cZ, cW, user_seed))
                                    : voronoi_lcg_random(fnv_hash4(cX, cY, cZ, user_seed));
            const std::uint32_t id_base = rng;
            const int n_pts = voronoi_prob_lookup(rng);
            for (int l = 0; l < n_pts; ++l) {
              rng = voronoi_lcg_random(rng);
              const float rx = static_cast<float>(rng - 1u) * kVoronoiInvRandModulus;
              rng = voronoi_lcg_random(rng);
              const float ry = static_cast<float>(rng - 1u) * kVoronoiInvRandModulus;
              rng = voronoi_lcg_random(rng);
              const float rz = static_cast<float>(rng - 1u) * kVoronoiInvRandModulus;
              float rw = 0.0f;
              if (use_time) {
                rng = voronoi_lcg_random(rng);
                rw = static_cast<float>(rng - 1u) * kVoronoiInvRandModulus;
              }

              const float px = static_cast<float>(cX) + bias + rand_amt * rx;
              const float py = static_cast<float>(cY) + bias + rand_amt * ry;
              const float pz = static_cast<float>(cZ) + bias + rand_amt * rz;
              const float pw = static_cast<float>(cW) + bias + rand_amt * rw;

              const float d = voronoi_distance(
                  x - px, y - py, z - pz, wt - pw, use_time,
                  distance_function, minkowski_exp);
              const std::uint32_t sid = id_base + static_cast<std::uint32_t>(l);
              if (d < best_dist0) {
                if (need_second_hit) {
                  best_dist1 = best_dist0;
                  best_id1 = best_id0;
                }
                best_dist0 = d;
                best_id0 = sid;
              } else if (need_second_hit && d < best_dist1) {
                best_dist1 = d;
                best_id1 = sid;
              }
            }
          }
        }
      }
    }
    if (!need_second_hit) {
      best_dist1 = best_dist0;
      best_id1 = best_id0;
    }
    return {best_dist0, best_dist1, best_id0, best_id1};
  }

  float voronoi_color_noise_1d(std::uint32_t cell_id, int channel,
                               float offset, int color_seed) const {
    const int k = static_cast<int>(std::floor(offset));
    const float t = offset - static_cast<float>(k);
    const std::uint32_t seed = static_cast<std::uint32_t>(std::max(0, color_seed));
    const int cid = static_cast<int>(cell_id);

    const float p0 = voronoi_rand01(fnv_hash4(cid, channel, k - 1, seed));
    const float p1 = voronoi_rand01(fnv_hash4(cid, channel, k, seed));
    const float p2 = voronoi_rand01(fnv_hash4(cid, channel, k + 1, seed));
    const float p3 = voronoi_rand01(fnv_hash4(cid, channel, k + 2, seed));

    const float t2 = t * t;
    const float t3 = t2 * t;
    const float a0 = -0.5f * p0 + 1.5f * p1 - 1.5f * p2 + 0.5f * p3;
    const float a1 = 1.0f * p0 - 2.5f * p1 + 2.0f * p2 - 0.5f * p3;
    const float a2 = -0.5f * p0 + 0.5f * p2;
    const float a3 = p1;
    return clamp01((a0 * t3) + (a1 * t2) + (a2 * t) + a3);
  }

  float voronoi_noise_value(float x, float y, float z, float w, bool use_time,
                            int distance_function, int shape_mode,
                            float jitter_modifier, float minkowski_exp,
                            float color_offset, float saturation,
                            int color_seed) const {
    const int safe_shape = clamp_compat(
        shape_mode,
        static_cast<int>(VORONOI_SHAPE_VORONOI),
        static_cast<int>(VORONOI_SHAPE_BUBBLES));
    const bool need_second_hit = (safe_shape != VORONOI_SHAPE_VORONOI);
    const VoronoiHit hit = voronoi_search(
        x, y, z, w, use_time, distance_function, jitter_modifier,
        minkowski_exp, need_second_hit);

    if (safe_shape == VORONOI_SHAPE_VORONOI) {
      const float r = voronoi_color_noise_1d(hit.id1 + 7919u, 0, color_offset, color_seed);
      const float g = voronoi_color_noise_1d(hit.id1 + 7919u, 1, color_offset, color_seed);
      const float b = voronoi_color_noise_1d(hit.id1 + 7919u, 2, color_offset, color_seed);
      const float sat = clamp01(saturation);
      const float gray = 0.299f * r + 0.587f * g + 0.114f * b;
      const float rr = lerpf(gray, r, sat);
      const float gg = lerpf(gray, g, sat);
      const float bb = lerpf(gray, b, sat);
      const float luma = 0.299f * rr + 0.587f * gg + 0.114f * bb;
      return clamp_compat(luma * 2.0f - 1.0f, -1.0f, 1.0f);
    }

    float d0 = hit.f1;
    float d1 = (hit.f2 > 1e19f) ? hit.f1 : hit.f2;
    if (distance_function == VORONOI_EUCLIDEAN) {
      d0 = std::sqrt(std::max(0.0f, d0));
      d1 = std::sqrt(std::max(0.0f, d1));
    }
    float value = 0.0f;
    switch (safe_shape) {
      case VORONOI_SHAPE_CELLS:
        value = d0;
        break;
      case VORONOI_SHAPE_CRYSTALS:
        value = d1;
        break;
      case VORONOI_SHAPE_CRACKS:
        value = d1 - d0;
        break;
      case VORONOI_SHAPE_WEB:
        value = d0 * d1;
        break;
      case VORONOI_SHAPE_BUBBLES:
        value = 1.0f - d0;
        break;
      default:
        value = d0;
        break;
    }
    return clamp_compat(clamp01(value) * 2.0f - 1.0f, -1.0f, 1.0f);
  }

  float voronoi_shape_value01(const VoronoiHit& hit, int distance_function,
                              int shape_mode) const {
    float d0 = hit.f1;
    float d1 = (hit.f2 > 1e19f) ? hit.f1 : hit.f2;
    if (distance_function == VORONOI_EUCLIDEAN) {
      d0 = std::sqrt(std::max(0.0f, d0));
      d1 = std::sqrt(std::max(0.0f, d1));
    }

    float value = 1.0f;
    switch (shape_mode) {
      case VORONOI_SHAPE_CELLS:
        value = d0;
        break;
      case VORONOI_SHAPE_CRYSTALS:
        value = d1;
        break;
      case VORONOI_SHAPE_CRACKS:
        value = d1 - d0;
        break;
      case VORONOI_SHAPE_WEB:
        value = d0 * d1;
        break;
      case VORONOI_SHAPE_BUBBLES:
        value = 1.0f - d0;
        break;
      case VORONOI_SHAPE_VORONOI:
      default:
        value = 1.0f;
        break;
    }
    return clamp01(value);
  }

