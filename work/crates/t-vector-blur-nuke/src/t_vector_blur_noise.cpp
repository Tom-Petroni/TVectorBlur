  static float lerpf(float a, float b, float t) {
    return a + (b - a) * t;
  }

  static float fadef(float t) {
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
  }


  static std::uint32_t fnv_hash5(int x, int y, int z, int w, std::uint32_t seed) {
    std::uint32_t h = fnv_hash4(x, y, z, seed);
    const std::uint32_t kPrime = 16777619u;
    h = (h ^ static_cast<std::uint32_t>(w)) * kPrime;
    return h;
  }

  static float hash_signed(std::uint32_t h) {
    return hash01(h) * 2.0f - 1.0f;
  }





  float simplex2_noise_value(float x, float y, float z, float w, bool use_time) const {
    // OpenSimplex2-like reorientation.
    const float xy = x + y;
    const float s2 = xy * -0.2113248654f;
    const float zz = z * 0.5773502692f;
    const float sx = x + s2 - zz;
    const float sy = y + s2 - zz;
    const float sz = xy * 0.5773502692f + zz;
    const float sw = use_time ? (w * 0.5773502692f) : 0.0f;
    return noise4_compat(sx, sy, sz, sw + 17.13f);
  }


  float get_pattern_metric(float x, float y, float z) const {
    return std::sqrt(x * x + y * y + z * z);
  }

  static void unit_complex_pow_int(float nx, float ny, int power,
                                   float& out_cos, float& out_sin) {
    if (power == 0) {
      out_cos = 1.0f;
      out_sin = 0.0f;
      return;
    }

    int p = (power < 0) ? -power : power;
    float rr = 1.0f;
    float ri = 0.0f;
    float br = nx;
    float bi = ny;

    while (p > 0) {
      if (p & 1) {
        const float nr = rr * br - ri * bi;
        const float ni = rr * bi + ri * br;
        rr = nr;
        ri = ni;
      }
      const float nbr = br * br - bi * bi;
      const float nbi = 2.0f * br * bi;
      br = nbr;
      bi = nbi;
      p >>= 1;
    }

    if (power < 0) {
      ri = -ri;
    }

    out_cos = rr;
    out_sin = ri;
  }

  float evaluate_pattern_val_for_mode(float x, float y, float z, float w, bool use_time,
                                      int pattern_type_mode,
                                      int pattern_segment_count, int pattern_twist) const {
    pattern_type_mode = sanitize_pattern_type_mode(pattern_type_mode);
    const float time_phase = use_time ? w : 0.0f;
    float val = 0.0f;

    if (pattern_type_mode == PATTERN_CONCENTRIC) {
      const float dist = get_pattern_metric(x, y, z);
      const float phase = dist - time_phase;
      const float xy_len = std::sqrt(x * x + y * y);
      if (xy_len <= 1e-12f || pattern_twist == 0) {
        val = std::sin(phase);
      } else {
        const float nx = x / xy_len;
        const float ny = y / xy_len;
        float cos_tw = 1.0f;
        float sin_tw = 0.0f;
        unit_complex_pow_int(nx, ny, pattern_twist, cos_tw, sin_tw);
        const float sphase = std::sin(phase);
        const float cphase = std::cos(phase);
        val = sphase * cos_tw - cphase * sin_tw;
      }
    } else if (pattern_type_mode == PATTERN_LINEAR) {
      val = std::sin(x - time_phase);
    } else if (pattern_type_mode == PATTERN_RADIAL) {
      const int segments = std::max(1, pattern_segment_count);
      const float xy_len = std::sqrt(x * x + y * y);
      if (xy_len <= 1e-12f) {
        val = std::sin(time_phase);
      } else {
        const float nx = x / xy_len;
        const float ny = y / xy_len;
        float cos_seg = 1.0f;
        float sin_seg = 0.0f;
        unit_complex_pow_int(nx, ny, segments, cos_seg, sin_seg);
        const float st = std::sin(time_phase);
        const float ct = std::cos(time_phase);
        val = sin_seg * ct + cos_seg * st;
      }
    }

    return clamp_compat(val, -1.0f, 1.0f);
  }

  float evaluate_pattern_val(float x, float y, float z, float w, bool use_time) const {
    return evaluate_pattern_val_for_mode(
        x, y, z, w, use_time,
        pattern_type_mode_,
        pattern_segment_count_, pattern_twist_);
  }

  float evaluate_pattern_fractal_signed_for_mode(float x, float y, float z,
                                                 float w, bool use_time,
                                                 int pattern_type_mode,
                                                 int pattern_segment_count,
                                                 int pattern_twist,
                                                 int fractal_mode,
                                                 int real_octaves,
                                                 double lacunarity,
                                                 double gain) const {
    const int fallback_oct = (fractal_mode == FRACTAL_NONE) ? 1 : std::max(1, real_octaves);
    const float* octave_freqs = nullptr;
    const float* octave_amps = nullptr;
    int oct = 0;
    float cached_amp_sum = 0.0f;
    if (!resolve_cached_octave_tables(fractal_mode, real_octaves, lacunarity, gain,
                                      octave_freqs, octave_amps, oct, cached_amp_sum)) {
      oct = fallback_oct;
    }
    const double lac = std::max(1e-4, lacunarity);
    const double g = std::max(0.0, gain);

    double sum = 0.0;
    double amp = 1.0;
    double freq = 1.0;
    double amp_sum = 0.0;
    for (int i = 0; i < oct; ++i) {
      const float sf = octave_freqs ? octave_freqs[i] : static_cast<float>(freq);
      const float octave_amp = octave_amps ? octave_amps[i] : static_cast<float>(amp);
      float n = evaluate_pattern_val_for_mode(
          x * sf,
          y * sf,
          z * sf,
          use_time ? (w * sf) : 0.0f,
          use_time,
          pattern_type_mode,
          pattern_segment_count, pattern_twist);

      if (fractal_mode == FRACTAL_BILLOW) {
        n = 2.0f * std::fabs(n) - 1.0f;
      } else if (fractal_mode == FRACTAL_RIDGED) {
        float r = 1.0f - std::fabs(n);
        r = r * r;
        n = r * 2.0f - 1.0f;
      }

      sum += static_cast<double>(n) * static_cast<double>(octave_amp);
      amp_sum += static_cast<double>(octave_amp);
      if (!octave_freqs) {
        freq *= lac;
      }
      if (!octave_amps) {
        amp *= g;
      }
    }

    if (octave_amps && cached_amp_sum > 1e-9f) {
      amp_sum = static_cast<double>(cached_amp_sum);
    }

    float signed_value = 0.0f;
    if (amp_sum > 1e-9) {
      signed_value = static_cast<float>(sum / amp_sum);
    }
    return clamp_compat(signed_value, -1.0f, 1.0f);
  }

  NoiseRGB evaluate_pattern_vector_from_point_for_mode(float x, float y, float z,
                                                       float w, bool use_time,
                                                       bool pref_space,
                                                       int pattern_type_mode,
                                                       int pattern_segment_count,
                                                       int pattern_twist,
                                                       float rotate_degrees,
                                                       int fractal_mode,
                                                       int real_octaves,
                                                       double lacunarity,
                                                       double gain) const {
    pattern_type_mode = sanitize_pattern_type_mode(pattern_type_mode);
    // Pattern value is evaluated in transformed space (x,y,z) so rotate affects pattern.
    // Vector field direction is evaluated in counter-rotated space so vectors stay in place.
    const float rot = radians(rotate_degrees);
    const float ca = std::cos(rot);
    const float sa = std::sin(rot);
    const float dir_x = ca * x + sa * y;
    const float dir_y = -sa * x + ca * y;
    const float dir_z = z;

    auto compute_metric_gradient = [&](float px, float py, float pz,
                                       float& gx, float& gy, float& gz) {
      const float len = std::sqrt(px * px + py * py + pz * pz);
      gx = 0.0f;
      gy = 0.0f;
      gz = 0.0f;
      if (len > 1e-6f) {
        const float inv = 1.0f / len;
        gx = px * inv;
        gy = py * inv;
        gz = pz * inv;
      } else {
        gx = 1.0f;
      }
    };

    float metric_gx = 0.0f;
    float metric_gy = 0.0f;
    float metric_gz = 0.0f;
    compute_metric_gradient(dir_x, dir_y, dir_z, metric_gx, metric_gy, metric_gz);

    float tangent_x = 0.0f;
    float tangent_y = 1.0f;
    const float dir_r2 = (dir_x * dir_x) + (dir_y * dir_y);
    if (dir_r2 > 1e-8f) {
      const float inv_r = 1.0f / std::sqrt(dir_r2);
      const float radial_x = dir_x * inv_r;
      const float radial_y = dir_y * inv_r;
      tangent_x = -radial_y;
      tangent_y = radial_x;
    }
    const float pattern_signed = evaluate_pattern_fractal_signed_for_mode(
        x, y, z, w, use_time,
        pattern_type_mode,
        pattern_segment_count, pattern_twist,
        fractal_mode, real_octaves, lacunarity, gain);
    const float pattern_positive = std::max(0.0f, pattern_signed);
    float vector_weight = pattern_signed;

    float vx = 0.0f;
    float vy = 0.0f;
    float vz = 0.0f;
    const float tw = static_cast<float>(pattern_twist);
    if (pattern_type_mode == PATTERN_CONCENTRIC) {
      vx = (-metric_gx) + (tangent_x * tw);
      vy = (-metric_gy) + (tangent_y * tw);
      vz = -metric_gz;
    } else if (pattern_type_mode == PATTERN_LINEAR) {
      // Linear: fixed direction from rotate, active on line bands only.
      vx = -sa;
      vy = ca;
      vz = 0.0f;
      vector_weight = pattern_positive;
    } else if (pattern_type_mode == PATTERN_RADIAL) {
      // Radial: same vector field as concentric, modulated by radial pattern.
      vx = -metric_gx;
      vy = -metric_gy;
      vz = -metric_gz;
    }

    float len = 0.0f;
    if (pref_space) {
      len = std::sqrt(vx * vx + vy * vy + vz * vz);
    } else {
      len = std::sqrt(vx * vx + vy * vy);
      vz = 0.0f;
    }
    if (len > 1e-6f) {
      const float inv = 1.0f / len;
      vx *= inv;
      vy *= inv;
      vz *= inv;
    } else {
      vx = 1.0f;
      vy = 0.0f;
      vz = 0.0f;
    }
    vx *= vector_weight;
    vy *= vector_weight;
    vz *= vector_weight;

    return {vx, vy, pref_space ? vz : 0.0f};
  }

  NoiseRGB evaluate_pattern_vector_from_point(float x, float y, float z,
                                              float w, bool use_time,
                                              bool pref_space) const {
    return evaluate_pattern_vector_from_point_for_mode(
        x, y, z, w, use_time, pref_space,
        pattern_type_mode_,
        pattern_segment_count_, pattern_twist_,
        static_cast<float>(pref_rotate_[2]),
        fractal_mode_, real_octaves_, lacunarity_, gain_);
  }

  NoiseRGB evaluate_voronoi_shape_vector_from_point(float x, float y, float z, float w,
                                                    bool use_time, bool pref_space,
                                                    int distance_function, float jitter_modifier,
                                                    float minkowski_exp,
                                                    int fractal_mode, int octaves,
                                                    double lacunarity, double gain,
                                                    float vector_offset) const {
    const int fallback_oct = (fractal_mode == FRACTAL_NONE) ? 1 : std::max(1, octaves);
    const float* octave_freqs = nullptr;
    const float* octave_amps = nullptr;
    int oct = 0;
    float cached_amp_sum = 0.0f;
    if (!resolve_cached_octave_tables(fractal_mode, octaves, lacunarity, gain,
                                      octave_freqs, octave_amps, oct, cached_amp_sum)) {
      oct = fallback_oct;
    }
    const double lac = std::max(1e-4, lacunarity);
    const double g = std::max(0.0, gain);
    const float phase = std::max(0.0f, vector_offset);
    const float rot = radians(phase * 3.6f);
    const float ca = std::cos(rot);
    const float sa = std::sin(rot);

    NoiseRGB accum = {0.0f, 0.0f, 0.0f};
    double amp = 1.0;
    double freq = 1.0;
    double amp_sum = 0.0;
    for (int i = 0; i < oct; ++i) {
      const float sf = octave_freqs ? octave_freqs[i] : static_cast<float>(freq);
      const float per_oct_phase = phase * 0.01f * static_cast<float>(i + 1);
      const float px = x * sf + per_oct_phase;
      const float py = y * sf - per_oct_phase * 0.73f;
      const float pz = z * sf + per_oct_phase * 0.41f;
      const float pw = use_time ? (w * sf + per_oct_phase * 0.19f) : 0.0f;
      const VoronoiHit hit = voronoi_search(
          px, py, pz, pw, use_time,
          distance_function, jitter_modifier, minkowski_exp, false);
      const int cid = static_cast<int>(hit.id1);

      float vx = hash_signed(fnv_hash4(cid, 0, 97 + i * 31, 0xA341316Cu));
      float vy = hash_signed(fnv_hash4(cid, 1, 173 + i * 31, 0xC8013EA4u));
      float vz = pref_space
                     ? hash_signed(fnv_hash4(cid, 2, 251 + i * 31, 0xAD90777Du))
                     : 0.0f;

      const float len = pref_space
                            ? std::sqrt(vx * vx + vy * vy + vz * vz)
                            : std::sqrt(vx * vx + vy * vy);
      if (len > 1e-6f) {
        const float inv = 1.0f / len;
        vx *= inv;
        vy *= inv;
        vz *= inv;
      } else {
        vx = 1.0f;
        vy = 0.0f;
        vz = 0.0f;
      }

      const float rvx = ca * vx - sa * vy;
      const float rvy = sa * vx + ca * vy;
      const float rvz = pref_space ? vz : 0.0f;

      const float wa = octave_amps ? octave_amps[i] : static_cast<float>(amp);
      accum.r += rvx * wa;
      accum.g += rvy * wa;
      accum.b += rvz * wa;
      amp_sum += static_cast<double>(wa);
      if (!octave_freqs) {
        freq *= lac;
      }
      if (!octave_amps) {
        amp *= g;
      }
    }

    if (octave_amps && cached_amp_sum > 1e-9f) {
      amp_sum = static_cast<double>(cached_amp_sum);
    }

    if (amp_sum > 1e-9) {
      const float inv = static_cast<float>(1.0 / amp_sum);
      accum.r *= inv;
      accum.g *= inv;
      accum.b *= inv;
    }

    return {
        clamp_compat(accum.r, -1.0f, 1.0f),
        clamp_compat(accum.g, -1.0f, 1.0f),
        clamp_compat(accum.b, -1.0f, 1.0f),
    };
  }


  float base_source_value_for_type(int noise_type, float x, float y, float z, float w,
                                   bool use_time, int voronoi_metric,
                                   int voronoi_shape_mode, float voronoi_jitter,
                                   float voronoi_minkowski_exp,
                                   float voronoi_color_offset,
                                   float voronoi_saturation,
                                   int voronoi_color_seed) const {
    switch (noise_type) {
      case NOISE_PERLIN:
        return use_time ? noise4_compat(x, y, z, w) : static_cast<float>(noise(x, y, z));
      case NOISE_SIMPLEX:
        return simplex2_noise_value(x, y, z, w, use_time);
      case NOISE_VORONOI:
        return voronoi_noise_value(
            x, y, z, w, use_time, voronoi_metric, voronoi_shape_mode,
            voronoi_jitter, voronoi_minkowski_exp,
            voronoi_color_offset, voronoi_saturation, voronoi_color_seed);
      case NOISE_PATTERN:
        return evaluate_pattern_val(x, y, z, w, use_time);
      default:
        return use_time ? noise4_compat(x, y, z, w) : static_cast<float>(noise(x, y, z));
    }
  }

  float base_source_value_for_type(int noise_type, float x, float y, float z, float w,
                                   bool use_time) const {
    return base_source_value_for_type(
        noise_type, x, y, z, w, use_time,
        voronoi_metric_, voronoi_shape_mode_,
        static_cast<float>(voronoi_randomness_),
        static_cast<float>(voronoi_minkowski_exp_),
        static_cast<float>(voronoi_color_offset_),
        static_cast<float>(voronoi_saturation_),
        voronoi_color_seed_);
  }

  float fractalized_value_for_settings(int noise_type, int fractal_mode,
                                       int real_octaves, double lacunarity,
                                       double gain, float x, float y, float z,
                                       float w, bool use_time,
                                       int voronoi_metric, int voronoi_shape_mode,
                                       float voronoi_jitter,
                                       float voronoi_minkowski_exp,
                                       float voronoi_color_offset,
                                       float voronoi_saturation,
                                       int voronoi_color_seed) const {

    if (fractal_mode == FRACTAL_NONE) {
      return base_source_value_for_type(
          noise_type, x, y, z, w, use_time,
          voronoi_metric, voronoi_shape_mode, voronoi_jitter,
          voronoi_minkowski_exp, voronoi_color_offset,
          voronoi_saturation, voronoi_color_seed);
    }

    const int fallback_oct = std::max(1, real_octaves);
    const float* octave_freqs = nullptr;
    const float* octave_amps = nullptr;
    int oct = 0;
    float ignored_amp_sum = 0.0f;
    if (!resolve_cached_octave_tables(fractal_mode, real_octaves, lacunarity, gain,
                                      octave_freqs, octave_amps, oct, ignored_amp_sum)) {
      oct = fallback_oct;
    }

    const double lac = std::max(1e-4, lacunarity);
    const double g = std::max(0.0, gain);
    double sum = 0.0;

    auto eval_octave_noise = [&](float sf) -> float {
      return base_source_value_for_type(
          noise_type, x * sf, y * sf,
          z * sf, w * sf, use_time,
          voronoi_metric, voronoi_shape_mode, voronoi_jitter,
          voronoi_minkowski_exp, voronoi_color_offset,
          voronoi_saturation, voronoi_color_seed);
    };

    if (octave_freqs && octave_amps) {
      int i = 0;
#if defined(__SSE__)
      if (fractal_mode == FRACTAL_FBM && oct >= 4) {
        __m128 packed_sum = _mm_setzero_ps();
        for (; i + 3 < oct; i += 4) {
          const float n0 = eval_octave_noise(octave_freqs[i + 0]);
          const float n1 = eval_octave_noise(octave_freqs[i + 1]);
          const float n2 = eval_octave_noise(octave_freqs[i + 2]);
          const float n3 = eval_octave_noise(octave_freqs[i + 3]);
          const __m128 n_pack = _mm_set_ps(n3, n2, n1, n0);
          const __m128 a_pack = _mm_loadu_ps(octave_amps + i);
          packed_sum = _mm_add_ps(packed_sum, _mm_mul_ps(n_pack, a_pack));
        }

        float packed_lanes[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        _mm_storeu_ps(packed_lanes, packed_sum);
        sum += static_cast<double>(packed_lanes[0]);
        sum += static_cast<double>(packed_lanes[1]);
        sum += static_cast<double>(packed_lanes[2]);
        sum += static_cast<double>(packed_lanes[3]);
      }
#endif

      for (; i < oct; ++i) {
        float n = eval_octave_noise(octave_freqs[i]);
        if (fractal_mode == FRACTAL_BILLOW) {
          n = 2.0f * std::fabs(n) - 1.0f;
        } else if (fractal_mode == FRACTAL_RIDGED) {
          float r = 1.0f - std::fabs(n);
          r = r * r;
          n = r * 2.0f - 1.0f;
        }
        sum += static_cast<double>(n) * static_cast<double>(octave_amps[i]);
      }
      return static_cast<float>(sum);
    }

    double amp = 1.0;
    double freq = 1.0;
    for (int i = 0; i < oct; ++i) {
      const float sf = static_cast<float>(freq);
      const float octave_amp = static_cast<float>(amp);
      float n = eval_octave_noise(sf);
      if (fractal_mode == FRACTAL_BILLOW) {
        n = 2.0f * std::fabs(n) - 1.0f;
      } else if (fractal_mode == FRACTAL_RIDGED) {
        float r = 1.0f - std::fabs(n);
        r = r * r;
        n = r * 2.0f - 1.0f;
      }
      sum += static_cast<double>(n) * static_cast<double>(octave_amp);
      freq *= lac;
      amp *= g;
    }

    return static_cast<float>(sum);
  }

  float fractalized_value_for_settings(int noise_type, int fractal_mode,
                                       int real_octaves, double lacunarity,
                                       double gain, float x, float y, float z,
                                       float w, bool use_time) const {
    return fractalized_value_for_settings(
        noise_type, fractal_mode, real_octaves, lacunarity, gain,
        x, y, z, w, use_time,
        voronoi_metric_, voronoi_shape_mode_,
        static_cast<float>(voronoi_randomness_),
        static_cast<float>(voronoi_minkowski_exp_),
        static_cast<float>(voronoi_color_offset_),
        static_cast<float>(voronoi_saturation_),
        voronoi_color_seed_);
  }

  float fractalized_value(float x, float y, float z, float w, bool use_time) const {
    return fractalized_value_for_settings(type_, fractal_mode_, real_octaves_,
                                          lacunarity_, gain_, x, y, z, w, use_time);
  }

  NoiseRGB evaluate_noise_sample(float sx, float sy, float sz, bool pref_space,
                                 float time_seed, bool use_time) const {
    float base01 = 0.5f;
    Vector3 v(0.0f, 0.0f, 0.0f);

    if (!uniform_) {
      v = noise_point_from_input(sx, sy, sz, pref_space, type_);
      if (!use_time) {
        // 2D path: zoffset offsets Z after matrix (translate-like evolution).
        v.z += time_seed;
      }
      const float raw_signed = fractalized_value(v.x, v.y, v.z, use_time ? time_seed : 0.0f, use_time);
      base01 = clamp01((raw_signed + 1.0f) * 0.5f);
    }

    const float base_final = finalize_noise_value(base01);
    return {base_final, base_final, base_final};
  }

  NoiseRGB evaluate_voronoi_color_sample_from_point(float x, float y, float z,
                                                    float time_seed,
                                                    bool use_time) const {
    const int fallback_oct = (fractal_mode_ == FRACTAL_NONE)
                                 ? 1
                                 : std::max(1, real_octaves_);
    const float jitter = static_cast<float>(voronoi_randomness_);
    const float minkowski_exp = static_cast<float>(voronoi_minkowski_exp_);
    const float color_offset = static_cast<float>(voronoi_color_offset_);
    const float sat = clamp01(static_cast<float>(voronoi_saturation_));
    constexpr int kColorSeed = 750;
    const float* octave_freqs = nullptr;
    const float* octave_amps = nullptr;
    int oct = 0;
    float cached_amp_sum = 0.0f;
    if (!resolve_cached_octave_tables(fractal_mode_, real_octaves_, lacunarity_, gain_,
                                      octave_freqs, octave_amps, oct, cached_amp_sum)) {
      oct = fallback_oct;
    }
    const double lac = std::max(1e-4, lacunarity_);
    const double g = std::max(0.0, gain_);

    const bool allow_color_shape =
        (voronoi_shape_mode_ == VORONOI_SHAPE_VORONOI) ||
        (voronoi_shape_mode_ == VORONOI_SHAPE_CRACKS);

    auto remap_fractal01 = [&](float v01) -> float {
      const float clamped = clamp01(v01);
      if (fractal_mode_ == FRACTAL_NONE || fractal_mode_ == FRACTAL_FBM) {
        return clamped;
      }

      float n = clamped * 2.0f - 1.0f;
      if (fractal_mode_ == FRACTAL_BILLOW) {
        n = 2.0f * std::fabs(n) - 1.0f;
      } else if (fractal_mode_ == FRACTAL_RIDGED) {
        float r = 1.0f - std::fabs(n);
        r = r * r;
        n = r * 2.0f - 1.0f;
      }
      return clamp01((n + 1.0f) * 0.5f);
    };

    double amp = 1.0;
    double freq = 1.0;
    double amp_sum = 0.0;
    NoiseRGB accum = {0.0f, 0.0f, 0.0f};

    for (int i = 0; i < oct; ++i) {
      const float sf = octave_freqs ? octave_freqs[i] : static_cast<float>(freq);
      const bool need_second_hit = (voronoi_shape_mode_ != VORONOI_SHAPE_VORONOI);
      const VoronoiHit hit = voronoi_search(
          x * sf, y * sf, z * sf, (use_time ? (time_seed * sf) : 0.0f), use_time,
          voronoi_metric_, jitter, minkowski_exp, need_second_hit);
      const float shape01 = voronoi_shape_value01(hit, voronoi_metric_, voronoi_shape_mode_);

      const std::uint32_t octave_seed = hit.id1 + static_cast<std::uint32_t>(i) * 7919u;
      const float r = voronoi_color_noise_1d(octave_seed, 0, color_offset, kColorSeed);
      const float gch = voronoi_color_noise_1d(octave_seed, 1, color_offset, kColorSeed);
      const float b = voronoi_color_noise_1d(octave_seed, 2, color_offset, kColorSeed);
      const float gray = 0.299f * r + 0.587f * gch + 0.114f * b;
      const float cr = allow_color_shape ? (lerpf(gray, r, sat) * shape01) : shape01;
      const float cg = allow_color_shape ? (lerpf(gray, gch, sat) * shape01) : shape01;
      const float cb = allow_color_shape ? (lerpf(gray, b, sat) * shape01) : shape01;

      const float fr = remap_fractal01(cr);
      const float fg = remap_fractal01(cg);
      const float fb = remap_fractal01(cb);

      const float wa = octave_amps ? octave_amps[i] : static_cast<float>(amp);
      accum.r += fr * wa;
      accum.g += fg * wa;
      accum.b += fb * wa;
      amp_sum += static_cast<double>(wa);
      if (!octave_freqs) {
        freq *= lac;
      }
      if (!octave_amps) {
        amp *= g;
      }
    }

    if (octave_amps && cached_amp_sum > 1e-9f) {
      amp_sum = static_cast<double>(cached_amp_sum);
    }

    if (amp_sum > 1e-9) {
      const float inv = static_cast<float>(1.0 / amp_sum);
      accum.r *= inv;
      accum.g *= inv;
      accum.b *= inv;
    }

    return {
        finalize_noise_value(clamp01(accum.r)),
        finalize_noise_value(clamp01(accum.g)),
        finalize_noise_value(clamp01(accum.b)),
    };
  }

  NoiseRGB evaluate_voronoi_color_sample(float sx, float sy, float sz, bool pref_space,
                                         float time_seed, bool use_time) const {
    if (uniform_) {
      const float mid = finalize_noise_value(0.5f);
      return {mid, mid, mid};
    }

    Vector3 v = noise_point_from_input(sx, sy, sz, pref_space, type_);
    if (!use_time) {
      v.z += time_seed;
    }
    return evaluate_voronoi_color_sample_from_point(
        v.x, v.y, v.z, time_seed, use_time);
  }

  NoiseRGB evaluate_noise_sample_rgb(float sx, float sy, float sz, bool pref_space,
                                     float time_seed, bool use_time,
                                     bool apply_rgb_offsets) const {
    const bool rgb_offsets_enabled = apply_rgb_offsets && main_rgb_has_offsets_;
    // 4D behavior: RGB time offset always shifts the time axis (seed/W),
    // independent of 2D/3D input mode.
    const bool rgb_time_offset_on_seed = true;
    const bool voronoi_color_mode = (type_ == NOISE_VORONOI);
    if (voronoi_color_mode) {
      if (!rgb_offsets_enabled) {
        return evaluate_voronoi_color_sample(sx, sy, sz, pref_space, time_seed, use_time);
      }

      if (pref_space || projection_2d_ == PROJECTION_PLANAR) {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float noise_time = 0.0f;
        if (!evaluate_noise_point(sx, sy, sz, pref_space, time_seed, use_time,
                                  x, y, z, noise_time)) {
          const float mid = finalize_noise_value(0.5f);
          return {mid, mid, mid};
        }

        const auto rgb_offsets_cache =
            std::atomic_load_explicit(&main_rgb_noise_offsets_cache_, std::memory_order_acquire);
        auto eval_channel = [&](int idx, int channel) -> float {
          const int c = clamp_compat(idx, 0, 2);
          const Vector3 delta = rgb_offsets_cache
                                    ? rgb_offsets_cache->delta[c]
                                    : invmatrix_.transform(Vector3(main_rgb_dx_[c], main_rgb_dy_[c], 0.0f));
          const float channel_time =
              noise_time + (rgb_time_offset_on_seed
                                ? (rgb_offsets_cache
                                       ? rgb_offsets_cache->time_offset[c]
                                       : main_rgb_dt_[c])
                                : 0.0f);
          const NoiseRGB sample = evaluate_voronoi_color_sample_from_point(
              x + delta.x, y + delta.y, z + delta.z, channel_time, use_time);
          if (channel == 0) {
            return sample.r;
          }
          if (channel == 1) {
            return sample.g;
          }
          return sample.b;
        };

        return {
            eval_channel(0, 0),
            eval_channel(1, 1),
            eval_channel(2, 2),
        };
      }

      const float r = evaluate_voronoi_color_sample(
                          sx + main_rgb_dx_[0], sy + main_rgb_dy_[0],
                          sz + (rgb_time_offset_on_seed ? 0.0f : main_rgb_dt_[0]),
                          pref_space,
                          time_seed + (rgb_time_offset_on_seed ? main_rgb_dt_[0] : 0.0f),
                          use_time)
                          .r;
      const float g = evaluate_voronoi_color_sample(
                          sx + main_rgb_dx_[1], sy + main_rgb_dy_[1],
                          sz + (rgb_time_offset_on_seed ? 0.0f : main_rgb_dt_[1]),
                          pref_space,
                          time_seed + (rgb_time_offset_on_seed ? main_rgb_dt_[1] : 0.0f),
                          use_time)
                          .g;
      const float b = evaluate_voronoi_color_sample(
                          sx + main_rgb_dx_[2], sy + main_rgb_dy_[2],
                          sz + (rgb_time_offset_on_seed ? 0.0f : main_rgb_dt_[2]),
                          pref_space,
                          time_seed + (rgb_time_offset_on_seed ? main_rgb_dt_[2] : 0.0f),
                          use_time)
                          .b;
      return {r, g, b};
    }

    if (!rgb_offsets_enabled) {
      return evaluate_noise_sample(sx, sy, sz, pref_space, time_seed, use_time);
    }

    // Fast path for planar/pref: the input->noise transform is affine and can be
    // reused across channels while keeping exactly the same visual result.
    if (pref_space || projection_2d_ == PROJECTION_PLANAR) {
      float x = 0.0f;
      float y = 0.0f;
      float z = 0.0f;
      float noise_time = 0.0f;
      if (!evaluate_noise_point(sx, sy, sz, pref_space, time_seed, use_time,
                                x, y, z, noise_time)) {
        const float mid = finalize_noise_value(0.5f);
        return {mid, mid, mid};
      }

      const auto rgb_offsets_cache =
          std::atomic_load_explicit(&main_rgb_noise_offsets_cache_, std::memory_order_acquire);
      auto eval_channel = [&](int idx) -> float {
        const int c = clamp_compat(idx, 0, 2);
        const Vector3 delta = rgb_offsets_cache
                                  ? rgb_offsets_cache->delta[c]
                                  : invmatrix_.transform(
                                        Vector3(main_rgb_dx_[c], main_rgb_dy_[c], 0.0f));
        const float channel_time =
            noise_time + (rgb_time_offset_on_seed
                              ? (rgb_offsets_cache
                                     ? rgb_offsets_cache->time_offset[c]
                                     : main_rgb_dt_[c])
                              : 0.0f);
        const float signed_v = evaluate_noise_scalar_from_point(
            x + delta.x, y + delta.y, z + delta.z, channel_time, use_time, 0.0f);
        const float base01 = clamp01((signed_v + 1.0f) * 0.5f);
        return finalize_noise_value(base01);
      };

      return {eval_channel(0), eval_channel(1), eval_channel(2)};
    }

    const float r = evaluate_noise_sample(
                        sx + main_rgb_dx_[0], sy + main_rgb_dy_[0],
                        sz + (rgb_time_offset_on_seed ? 0.0f : main_rgb_dt_[0]),
                        pref_space,
                        time_seed + (rgb_time_offset_on_seed ? main_rgb_dt_[0] : 0.0f),
                        use_time)
                        .r;
    const float g = evaluate_noise_sample(
                        sx + main_rgb_dx_[1], sy + main_rgb_dy_[1],
                        sz + (rgb_time_offset_on_seed ? 0.0f : main_rgb_dt_[1]),
                        pref_space,
                        time_seed + (rgb_time_offset_on_seed ? main_rgb_dt_[1] : 0.0f),
                        use_time)
                        .r;
    const float b = evaluate_noise_sample(
                        sx + main_rgb_dx_[2], sy + main_rgb_dy_[2],
                        sz + (rgb_time_offset_on_seed ? 0.0f : main_rgb_dt_[2]),
                        pref_space,
                        time_seed + (rgb_time_offset_on_seed ? main_rgb_dt_[2] : 0.0f),
                        use_time)
                        .r;
    return {r, g, b};
  }

  bool evaluate_noise_point(float sx, float sy, float sz, bool pref_space,
                            float time_seed, bool use_time,
                            float& x, float& y, float& z, float& noise_time) const {
    if (uniform_) {
      x = 0.0f;
      y = 0.0f;
      z = 0.0f;
      noise_time = 0.0f;
      return false;
    }

    Vector3 v = noise_point_from_input(sx, sy, sz, pref_space, type_);
    noise_time = time_seed;
    if (!use_time) {
      // 2D path: zoffset offsets Z after matrix (translate-like evolution).
      v.z += noise_time;
    }
    x = v.x;
    y = v.y;
    z = v.z;
    return true;
  }

  float evaluate_noise_scalar_from_point(float x, float y, float z,
                                         float noise_time, bool use_time,
                                         float phase_shift) const {
    const float px = phase_shift;
    const float py = phase_shift * 0.5f;
    const float pz = -phase_shift * 0.5f;
    const float pw = phase_shift;
    return clamp_compat(
        fractalized_value(x + px, y + py, z + pz,
                          (use_time ? noise_time : 0.0f) + pw, use_time),
        -1.0f, 1.0f);
  }

  float evaluate_noise_scalar_signed(float sx, float sy, float sz, bool pref_space,
                                     float time_seed, bool use_time) const {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float noise_time = 0.0f;
    if (!evaluate_noise_point(sx, sy, sz, pref_space, time_seed, use_time,
                              x, y, z, noise_time)) {
      return 0.0f;
    }
    return evaluate_noise_scalar_from_point(x, y, z, noise_time, use_time, 0.0f);
  }

  NoiseRGB evaluate_noise_vector_map_with_offset_from_point(float x, float y, float z,
                                                            float noise_time,
                                                            bool pref_space,
                                                            bool use_time,
                                                            float uv_offset_phase,
                                                            int vector_mode) const {
    const int resolved_vector_mode = clamp_compat(
        vector_mode,
        static_cast<int>(DW_VECTOR_FIELD),
        static_cast<int>(DW_VECTOR_ORBIT_CENTER));
    const float vector_offset = std::max(0.0f, uv_offset_phase);
    if (resolved_vector_mode == DW_VECTOR_RADIAL_CENTER) {
      return evaluate_center_mode_vector(x, y, z, pref_space, false);
    }
    if (resolved_vector_mode == DW_VECTOR_ORBIT_CENTER) {
      return evaluate_center_mode_vector(x, y, z, pref_space, true);
    }
    if (resolved_vector_mode == DW_VECTOR_DERIVATIVE) {
      const float eps = 0.04f;
      const float inv2e = 1.0f / (2.0f * eps);
      auto scalar = [&](float px, float py, float pz) -> float {
        return evaluate_noise_scalar_from_point(
            px, py, pz, noise_time, use_time, vector_offset);
      };
      const float gx = (scalar(x + eps, y, z) - scalar(x - eps, y, z)) * inv2e;
      const float gy = (scalar(x, y + eps, z) - scalar(x, y - eps, z)) * inv2e;
      const float gz = pref_space
                           ? (scalar(x, y, z + eps) - scalar(x, y, z - eps)) * inv2e
                           : 0.0f;
      float vx = -gy;
      float vy = gx;
      if (pref_space) {
        vx *= 2.0f;
        vy *= 2.0f;
      }
      const float vz = pref_space ? (gz * 0.15f) : 0.0f;
      return {soft_clip_signed(vx), soft_clip_signed(vy),
              pref_space ? soft_clip_signed(vz) : 0.0f};
    }

    if (type_ == NOISE_VORONOI &&
        voronoi_shape_mode_ == VORONOI_SHAPE_VORONOI) {
      return evaluate_voronoi_shape_vector_from_point(
          x, y, z, noise_time,
          use_time, pref_space,
          voronoi_metric_,
          static_cast<float>(voronoi_randomness_),
          static_cast<float>(voronoi_minkowski_exp_),
          fractal_mode_, real_octaves_, lacunarity_, gain_,
          vector_offset);
    }

    if (type_ == NOISE_PATTERN) {
      return evaluate_pattern_vector_from_point(
          x, y, z, noise_time, use_time, pref_space);
    }

    const float rx = evaluate_noise_scalar_from_point(
        x, y, z, noise_time, use_time, 0.0f);
    const float gy = evaluate_noise_scalar_from_point(
        x, y, z, noise_time, use_time, vector_offset);
    const float bz = pref_space
                         ? evaluate_noise_scalar_from_point(
                               x, y, z, noise_time, use_time, -vector_offset)
                         : 0.0f;
    return {rx, gy, bz};
  }

  NoiseRGB evaluate_noise_vector_map_with_offset(float sx, float sy, float sz,
                                                 bool pref_space,
                                                 float time_seed, bool use_time,
                                                 float uv_offset_phase,
                                                 int vector_mode) const {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float noise_time = 0.0f;
    if (!evaluate_noise_point(sx, sy, sz, pref_space, time_seed, use_time,
                              x, y, z, noise_time)) {
      return {0.0f, 0.0f, 0.0f};
    }
    return evaluate_noise_vector_map_with_offset_from_point(
        x, y, z, noise_time, pref_space, use_time, uv_offset_phase, vector_mode);
  }

  NoiseRGB evaluate_noise_vector_map(float sx, float sy, float sz, bool pref_space,
                                     float time_seed, bool use_time) const {
    const NoiseRGB unit = evaluate_noise_vector_map_with_offset(
        sx, sy, sz, pref_space, time_seed, use_time, kFixedUvOffset,
        output_vectors_mode_);
    const float vector_mul = std::max(0.0f, static_cast<float>(output_vectors_multiply_));
    return {
        unit.r * vector_mul,
        unit.g * vector_mul,
        unit.b * vector_mul,
    };
  }

  NoiseRGB evaluate_noise_stmap(float sx, float sy, float sz, bool pref_space,
                                float time_seed, bool use_time) const {
    const NoiseRGB v = evaluate_noise_vector_map_with_offset(
        sx, sy, sz, pref_space, time_seed, use_time, kFixedUvOffset,
        output_stmap_mode_);
    const float st_mul = std::max(0.0f, static_cast<float>(output_stmap_multiply_));
    const float rx = (0.5f + 0.5f * v.r) * st_mul;
    const float ry = (0.5f + 0.5f * v.g) * st_mul;
    const float rz = pref_space ? ((0.5f + 0.5f * v.b) * st_mul) : 0.0f;
    return {rx, ry, rz};
  }

  NoiseRGB evaluate_noise_normal_map(float sx, float sy, float sz, bool pref_space,
                                     float time_seed, bool use_time) const {
    const float eps = pref_space ? 0.01f : 1.0f;
    const float strength = std::max(0.0f, static_cast<float>(output_normal_strength_));
    float hxm = 0.0f;
    float hxp = 0.0f;
    float hym = 0.0f;
    float hyp = 0.0f;

    if (pref_space || projection_2d_ == PROJECTION_PLANAR) {
      float x = 0.0f;
      float y = 0.0f;
      float z = 0.0f;
      float noise_time = 0.0f;
      if (!evaluate_noise_point(sx, sy, sz, pref_space, time_seed, use_time,
                                x, y, z, noise_time)) {
        return {0.5f, 0.5f, 1.0f};
      }
      const Vector3 dx = invmatrix_.transform(Vector3(eps, 0.0f, 0.0f));
      const Vector3 dy = invmatrix_.transform(Vector3(0.0f, eps, 0.0f));
      auto sample_point = [&](float px, float py, float pz) -> float {
        return evaluate_noise_scalar_from_point(px, py, pz, noise_time, use_time, 0.0f);
      };
      hxm = sample_point(x - dx.x, y - dx.y, z - dx.z);
      hxp = sample_point(x + dx.x, y + dx.y, z + dx.z);
      hym = sample_point(x - dy.x, y - dy.y, z - dy.z);
      hyp = sample_point(x + dy.x, y + dy.y, z + dy.z);
    } else {
      hxm = evaluate_noise_scalar_signed(
          sx - eps, sy, sz, pref_space, time_seed, use_time);
      hxp = evaluate_noise_scalar_signed(
          sx + eps, sy, sz, pref_space, time_seed, use_time);
      hym = evaluate_noise_scalar_signed(
          sx, sy - eps, sz, pref_space, time_seed, use_time);
      hyp = evaluate_noise_scalar_signed(
          sx, sy + eps, sz, pref_space, time_seed, use_time);
    }

    float nx = -(hxp - hxm) * 0.5f * strength;
    float ny = -(hyp - hym) * 0.5f * strength;
    float nz = 1.0f;
    const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (len > 1e-6f) {
      const float inv = 1.0f / len;
      nx *= inv;
      ny *= inv;
      nz *= inv;
    }

    return {
        clamp01(nx * 0.5f + 0.5f),
        clamp01(ny * 0.5f + 0.5f),
        clamp01(nz * 0.5f + 0.5f),
    };
  }

  NoiseRGB evaluate_output_no_warp(float sx, float sy, float sz, bool pref_space,
                                   float time_seed, bool use_time,
                                   bool apply_rgb_offsets = true) const {
    NoiseRGB out = {0.0f, 0.0f, 0.0f};
    switch (output_mode_) {
      case OUTPUT_VECTORS:
        out = evaluate_noise_vector_map(sx, sy, sz, pref_space, time_seed, use_time);
        break;
      case OUTPUT_STMAP:
        out = evaluate_noise_stmap(sx, sy, sz, pref_space, time_seed, use_time);
        break;
      case OUTPUT_NORMAL:
        out = evaluate_noise_normal_map(sx, sy, sz, pref_space, time_seed, use_time);
        break;
      case OUTPUT_NOISE:
      default:
        out = evaluate_noise_sample_rgb(
            sx, sy, sz, pref_space, time_seed, use_time, apply_rgb_offsets);
        break;
    }

    if (output_invert_) {
      if (output_mode_ == OUTPUT_VECTORS) {
        out.r = -out.r;
        out.g = -out.g;
        out.b = -out.b;
      } else if (output_mode_ == OUTPUT_STMAP) {
        out.r = 1.0f - out.r;
        out.g = 1.0f - out.g;
        out.b = 1.0f - out.b;
      } else {
        out.r = clamp01(1.0f - out.r);
        out.g = clamp01(1.0f - out.g);
        out.b = clamp01(1.0f - out.b);
      }
    }
    return out;
  }

  float evaluate_domainwarp_base_scalar_for_type(int noise_type,
                                                 float x, float y, float z,
                                                 float domainwarp_time,
                                                 bool use_time,
                                                 float ox, float oy,
                                                 float oz, float ow) const {
    const float px = x + ox;
    const float py = y + oy;
    const float pz = z + oz;
    const float pw = (use_time ? domainwarp_time : 0.0f) + ow;
    if (noise_type == NOISE_PATTERN) {
      auto apply_domainwarp_gamma_to_signed = [&](float signed_value) -> float {
        const float mask01 = clamp01(signed_value * 0.5f + 0.5f);
        const float shaped01 = finalize_noise_value(mask01, kDomainwarpPatternGamma);
        return clamp_compat(shaped01 * 2.0f - 1.0f, -1.0f, 1.0f);
      };
      auto sample_pattern = [&](float sx, float sy, float sz, float sw) -> float {
        return evaluate_pattern_val_for_mode(
            sx, sy, sz, sw, use_time,
            domainwarp_pattern_type_mode_,
            domainwarp_pattern_segment_count_,
            domainwarp_pattern_twist_);
      };

      if (domainwarp_fractal_mode_ == FRACTAL_NONE) {
        return apply_domainwarp_gamma_to_signed(
            clamp_compat(sample_pattern(px, py, pz, pw), -1.0f, 1.0f));
      }

      const int fallback_oct = std::max(1, domainwarp_real_octaves_);
      const float* octave_freqs = nullptr;
      const float* octave_amps = nullptr;
      int oct = 0;
      float ignored_amp_sum = 0.0f;
      if (!resolve_cached_octave_tables(domainwarp_fractal_mode_, domainwarp_real_octaves_,
                                        domainwarp_lacunarity_, domainwarp_gain_,
                                        octave_freqs, octave_amps, oct, ignored_amp_sum)) {
        oct = fallback_oct;
      }
      const double lac = std::max(1e-4, domainwarp_lacunarity_);
      const double g = std::max(0.0, domainwarp_gain_);
      double sum = 0.0;
      double amp = 1.0;
      double freq = 1.0;
      for (int i = 0; i < oct; ++i) {
        const float sf = octave_freqs ? octave_freqs[i] : static_cast<float>(freq);
        const float octave_amp = octave_amps ? octave_amps[i] : static_cast<float>(amp);
        float n = sample_pattern(
            px * sf,
            py * sf,
            pz * sf,
            pw * sf);
        if (domainwarp_fractal_mode_ == FRACTAL_BILLOW) {
          n = 2.0f * std::fabs(n) - 1.0f;
        } else if (domainwarp_fractal_mode_ == FRACTAL_RIDGED) {
          float r = 1.0f - std::fabs(n);
          r = r * r;
          n = r * 2.0f - 1.0f;
        }

        sum += static_cast<double>(n) * static_cast<double>(octave_amp);
        if (!octave_freqs) {
          freq *= lac;
        }
        if (!octave_amps) {
          amp *= g;
        }
      }
      return apply_domainwarp_gamma_to_signed(
          clamp_compat(static_cast<float>(sum), -1.0f, 1.0f));
    }

    const float raw_signed = fractalized_value_for_settings(
        noise_type,
        domainwarp_fractal_mode_,
        domainwarp_real_octaves_,
        domainwarp_lacunarity_,
        domainwarp_gain_,
        px,
        py,
        pz,
        pw,
        use_time,
        domainwarp_voronoi_metric_,
        domainwarp_voronoi_shape_mode_,
        static_cast<float>(domainwarp_voronoi_randomness_),
        static_cast<float>(domainwarp_voronoi_minkowski_exp_),
        0.0f,
        1.0f,
        750);
    return clamp_compat(raw_signed, -1.0f, 1.0f);
  }

  bool evaluate_domainwarp_point(float sx, float sy, float sz, bool pref_space,
                                 float time_seed, bool use_time,
                                 float& x, float& y, float& z, float& domainwarp_time) const {
    if (domainwarp_uniform_) {
      x = 0.0f;
      y = 0.0f;
      z = 0.0f;
      domainwarp_time = 0.0f;
      return false;
    }

    Vector3 v = domainwarp_point_from_input(sx, sy, sz, pref_space, domainwarp_type_);
    domainwarp_time = time_seed;
    if (!use_time) {
      v.z += domainwarp_time;
    }

    x = v.x;
    y = v.y;
    z = v.z;
    return true;
  }

  float evaluate_domainwarp_scalar_from_point_for_type(int noise_type,
                                                       float x, float y, float z,
                                                       float domainwarp_time,
                                                       bool use_time,
                                                       float phase_shift,
                                                       float ox, float oy,
                                                       float oz, float ow,
                                                       float coord_scale) const {
    // Small asymmetric phase remap reduces channel locking artifacts.
    const float px = ox + phase_shift;
    const float py = oy + phase_shift * 0.5f;
    const float pz = oz - phase_shift * 0.5f;
    const float pw = ow + phase_shift;
    const float s = std::max(1e-4f, coord_scale);
    return evaluate_domainwarp_base_scalar_for_type(
        noise_type, x * s, y * s, z * s, domainwarp_time * s, use_time, px, py, pz, pw);
  }

  float evaluate_domainwarp_scalar_from_point(float x, float y, float z,
                                              float domainwarp_time, bool use_time,
                                              float phase_shift,
                                              float ox, float oy, float oz,
                                              float ow) const {
    return evaluate_domainwarp_scalar_from_point_for_type(
        domainwarp_type_, x, y, z, domainwarp_time, use_time, phase_shift,
        ox, oy, oz, ow, 1.0f);
  }

  float evaluate_domainwarp_scalar_from_point_for_channel(int noise_type,
                                                          int channel_index,
                                                          const ChannelOffsets& offsets,
                                                          float x, float y, float z,
                                                          float domainwarp_time,
                                                          bool use_time,
                                                          float phase_shift,
                                                          float ox, float oy,
                                                          float oz, float ow,
                                                          float coord_scale) const {
    if (!domainwarp_rgb_has_offsets_) {
      return evaluate_domainwarp_scalar_from_point_for_type(
          noise_type, x, y, z, domainwarp_time, use_time,
          phase_shift, ox, oy, oz, ow, coord_scale);
    }

    const int idx = clamp_compat(channel_index, 0, 2);
    const float dt = offsets.dt[idx];
    return evaluate_domainwarp_scalar_from_point_for_type(
        noise_type,
        x + offsets.dx[idx],
        y + offsets.dy[idx],
        z + dt,
        domainwarp_time,
        use_time,
        phase_shift,
        ox, oy, oz, ow, coord_scale);
  }

  float domainwarp_amount_scale() const {
    // Aggressive artist-facing mapping:
    // old_amount 15000 (linear*0.2 => 3000) ~= new amount 100.
    return domainwarp_amount_scale_from_knob(domainwarp_amount_);
  }

  static float soft_clip_signed(float v) {
    // Keep vector field in [-1, 1] while preserving smooth direction changes.
    return static_cast<float>(std::tanh(static_cast<double>(v) * 0.45));
  }


  NoiseRGB evaluate_domainwarp_noncurl_vector_from_point_params(float x, float y, float z,
                                                                bool pref_space,
                                                                float domainwarp_time,
                                                                bool use_time,
                                                                float vector_offset,
                                                                float coord_scale) const {
    if (domainwarp_type_ == NOISE_VORONOI &&
        domainwarp_voronoi_shape_mode_ == VORONOI_SHAPE_VORONOI) {
      return evaluate_voronoi_shape_vector_from_point(
          x * coord_scale, y * coord_scale, z * coord_scale,
          domainwarp_time * coord_scale,
          use_time, pref_space,
          domainwarp_voronoi_metric_,
          static_cast<float>(domainwarp_voronoi_randomness_),
          static_cast<float>(domainwarp_voronoi_minkowski_exp_),
          domainwarp_fractal_mode_, domainwarp_real_octaves_,
          domainwarp_lacunarity_, domainwarp_gain_,
          vector_offset);
    }

    if (domainwarp_type_ == NOISE_PATTERN) {
      return evaluate_pattern_vector_from_point_for_mode(
          x * coord_scale, y * coord_scale, z * coord_scale,
          domainwarp_time * coord_scale, use_time, pref_space,
          domainwarp_pattern_type_mode_,
          domainwarp_pattern_segment_count_, domainwarp_pattern_twist_,
          static_cast<float>(domainwarp_rotate_[2]),
          domainwarp_fractal_mode_, domainwarp_real_octaves_,
          domainwarp_lacunarity_, domainwarp_gain_);
    }

    const ChannelOffsets chroma_offsets = domainwarp_rgb_offsets();
    const float phase = std::max(0.0f, vector_offset);
    const float wx = evaluate_domainwarp_scalar_from_point_for_channel(
        domainwarp_type_, 0, chroma_offsets,
        x, y, z, domainwarp_time, use_time,
        0.0f, 11.5f, -13.2f, 5.6f, 31.7f, coord_scale);
    const float wy = evaluate_domainwarp_scalar_from_point_for_channel(
        domainwarp_type_, 1, chroma_offsets,
        x, y, z, domainwarp_time, use_time,
        phase, 11.5f, -13.2f, 5.6f, 31.7f, coord_scale);
    const float wz = pref_space
                         ? evaluate_domainwarp_scalar_from_point_for_channel(
                               domainwarp_type_, 2, chroma_offsets,
                               x, y, z, domainwarp_time, use_time,
                               -phase, 11.5f, -13.2f, 5.6f, 31.7f, coord_scale)
                         : 0.0f;
    return {wx, wy, wz};
  }

  NoiseRGB evaluate_domainwarp_noncurl_vector_from_point(float x, float y, float z,
                                                         bool pref_space,
                                                         float domainwarp_time,
                                                         bool use_time) const {
    return evaluate_domainwarp_noncurl_vector_from_point_params(
        x, y, z, pref_space, domainwarp_time, use_time,
        kFixedUvOffset, 1.0f);
  }

  NoiseRGB evaluate_domainwarp_derivative_vector_from_point_params(float x, float y, float z,
                                                                   bool pref_space,
                                                                   float domainwarp_time,
                                                                   bool use_time,
                                                                   float vector_offset,
                                                                   float coord_scale,
                                                                   float rotate_degrees) const {
    const float eps = 0.04f;
    const float inv2e = 1.0f / (2.0f * eps);
    const int scalar_type = domainwarp_type_;
    const ChannelOffsets chroma_offsets = domainwarp_rgb_offsets();
    auto scalar = [&](float px, float py, float pz) -> float {
      return evaluate_domainwarp_scalar_from_point_for_channel(
          scalar_type, 0, chroma_offsets,
          px, py, pz, domainwarp_time, use_time, vector_offset,
          11.5f, -13.2f, 5.6f, 31.7f, coord_scale);
    };

    const float gx = (scalar(x + eps, y, z) - scalar(x - eps, y, z)) * inv2e;
    const float gy = (scalar(x, y + eps, z) - scalar(x, y - eps, z)) * inv2e;
    const float gz = pref_space
                         ? (scalar(x, y, z + eps) - scalar(x, y, z - eps)) * inv2e
                         : 0.0f;

    const float radians_angle = radians(rotate_degrees);
    const float ca = std::cos(radians_angle);
    const float sa = std::sin(radians_angle);
    float vx = ca * gx - sa * gy;
    float vy = sa * gx + ca * gy;
    if (pref_space) {
      // Keep visible spiral behavior in 3D by prioritizing XY swirl.
      vx *= 2.0f;
      vy *= 2.0f;
    }
    const float vz = pref_space ? (gz * 0.15f) : 0.0f;

    return {soft_clip_signed(vx), soft_clip_signed(vy),
            pref_space ? soft_clip_signed(vz) : 0.0f};
  }

  static NoiseRGB evaluate_center_mode_vector(float x, float y, float z,
                                              bool pref_space, bool orbit_mode) {
    float vx = 0.0f;
    float vy = 0.0f;
    float vz = 0.0f;

    if (orbit_mode) {
      // Tangent field around origin.
      vx = -y;
      vy = x;
      vz = 0.0f;
    } else {
      // Radial field from origin.
      vx = x;
      vy = y;
      vz = pref_space ? z : 0.0f;
    }

    const float len = pref_space
                          ? std::sqrt(vx * vx + vy * vy + vz * vz)
                          : std::sqrt(vx * vx + vy * vy);
    if (len > 1e-6f) {
      const float inv = 1.0f / len;
      vx *= inv;
      vy *= inv;
      vz *= inv;
    } else if (orbit_mode) {
      vx = 0.0f;
      vy = 1.0f;
      vz = 0.0f;
    } else {
      vx = 1.0f;
      vy = 0.0f;
      vz = 0.0f;
    }

    return {vx, vy, pref_space ? vz : 0.0f};
  }

  NoiseRGB evaluate_domainwarp_single_unit_vector_from_point(float x, float y, float z,
                                                             bool pref_space,
                                                             float domainwarp_time,
                                                             bool use_time,
                                                             float vector_offset,
                                                             float coord_scale) const {
    if (domainwarp_vector_mode_ == DW_VECTOR_DERIVATIVE) {
      return evaluate_domainwarp_derivative_vector_from_point_params(
          x, y, z, pref_space, domainwarp_time, use_time,
          vector_offset, coord_scale,
          kDomainWarpSpiralNoiseAngle);
    }
    if (domainwarp_vector_mode_ == DW_VECTOR_RADIAL_CENTER) {
      return evaluate_center_mode_vector(x, y, z, pref_space, false);
    }
    if (domainwarp_vector_mode_ == DW_VECTOR_ORBIT_CENTER) {
      return evaluate_center_mode_vector(x, y, z, pref_space, true);
    }
    return evaluate_domainwarp_noncurl_vector_from_point_params(
        x, y, z, pref_space, domainwarp_time, use_time,
        vector_offset, coord_scale);
  }

  NoiseRGB evaluate_domainwarp_unit_vector_from_point(float x, float y, float z,
                                                      bool pref_space,
                                                      float domainwarp_time,
                                                      bool use_time) const {
    const float base_offset = kFixedUvOffset;
    return evaluate_domainwarp_single_unit_vector_from_point(
        x, y, z, pref_space, domainwarp_time, use_time, base_offset, 1.0f);
  }

  NoiseRGB evaluate_domainwarp_unit_vector_iterative(float sx, float sy, float sz,
                                                     bool pref_space,
                                                     float time_seed,
                                                     bool use_time) const {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float domainwarp_time = 0.0f;
    if (!evaluate_domainwarp_point(sx, sy, sz, pref_space, time_seed, use_time,
                                   x, y, z, domainwarp_time)) {
      return {0.0f, 0.0f, 0.0f};
    }
    return evaluate_domainwarp_unit_vector_from_point(
        x, y, z, pref_space, domainwarp_time, use_time);
  }

  NoiseRGB evaluate_domainwarp_vector_with_amount(float sx, float sy, float sz,
                                                  bool pref_space,
                                                  float time_seed, bool use_time,
                                                  float amount) const {
    if (amount <= 0.0f) {
      return {0.0f, 0.0f, 0.0f};
    }

    const NoiseRGB unit = evaluate_domainwarp_unit_vector_iterative(
        sx, sy, sz, pref_space, time_seed, use_time);
    return {unit.r * amount, unit.g * amount, unit.b * amount};
  }

  NoiseRGB evaluate_domainwarp_vector(float sx, float sy, float sz, bool pref_space,
                                      float time_seed, bool use_time) const {
    const float amount =
        domainwarp_amount_scale() * (pref_space ? 0.02f : 1.0f);
    return evaluate_domainwarp_vector_with_amount(
        sx, sy, sz, pref_space, time_seed, use_time, amount);
  }

  NoiseRGB evaluate_domainwarp_map(float sx, float sy, float sz, bool pref_space,
                                   float time_seed, bool use_time) const {
    return evaluate_domainwarp_unit_vector_iterative(
        sx, sy, sz, pref_space, time_seed, use_time);
  }

  NoiseRGB evaluate_domainwarp_map_pref_blurred(float sx, float sy, float sz,
                                                float time_seed, bool use_time,
                                                float blur_radius) const {
    const float r = std::max(0.0f, blur_radius);
    if (r <= 1e-6f) {
      return evaluate_domainwarp_map(sx, sy, sz, true, time_seed, use_time);
    }

    NoiseRGB sum = {0.0f, 0.0f, 0.0f};
    float weight_sum = 0.0f;
    auto accum = [&](float px, float py, float pz, float w) {
      if (w <= 0.0f) {
        return;
      }
      const NoiseRGB s = evaluate_domainwarp_map(px, py, pz, true, time_seed, use_time);
      sum.r += s.r * w;
      sum.g += s.g * w;
      sum.b += s.b * w;
      weight_sum += w;
    };

    // 3D Gaussian-like kernel: axis + diagonal taps to avoid blocky remnants.
    accum(sx, sy, sz, 0.20f);
    const float r1 = r;
    const float w_axis = 0.08f;
    accum(sx + r1, sy, sz, w_axis);
    accum(sx - r1, sy, sz, w_axis);
    accum(sx, sy + r1, sz, w_axis);
    accum(sx, sy - r1, sz, w_axis);
    accum(sx, sy, sz + r1, w_axis);
    accum(sx, sy, sz - r1, w_axis);

    const float w_diag = 0.02f;
    accum(sx + r1, sy + r1, sz, w_diag);
    accum(sx + r1, sy - r1, sz, w_diag);
    accum(sx - r1, sy + r1, sz, w_diag);
    accum(sx - r1, sy - r1, sz, w_diag);
    accum(sx + r1, sy, sz + r1, w_diag);
    accum(sx + r1, sy, sz - r1, w_diag);
    accum(sx - r1, sy, sz + r1, w_diag);
    accum(sx - r1, sy, sz - r1, w_diag);
    accum(sx, sy + r1, sz + r1, w_diag);
    accum(sx, sy + r1, sz - r1, w_diag);
    accum(sx, sy - r1, sz + r1, w_diag);
    accum(sx, sy - r1, sz - r1, w_diag);

    // Add a second ring only for larger blur values.
    if (r > 0.2f) {
      const float r2 = r * 2.0f;
      const float w2 = 0.01f;
      accum(sx + r2, sy, sz, w2);
      accum(sx - r2, sy, sz, w2);
      accum(sx, sy + r2, sz, w2);
      accum(sx, sy - r2, sz, w2);
      accum(sx, sy, sz + r2, w2);
      accum(sx, sy, sz - r2, w2);
    }

    if (weight_sum <= 1e-6f) {
      return evaluate_domainwarp_map(sx, sy, sz, true, time_seed, use_time);
    }
    const float inv_w = 1.0f / weight_sum;
    return {sum.r * inv_w, sum.g * inv_w, sum.b * inv_w};
  }

