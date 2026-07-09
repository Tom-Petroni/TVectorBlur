  explicit TVectorBlurBase(Node* node) : Iop(node) {
    inputs(1);
    formats_.format(nullptr);
    output_channels_[0] = Chan_Red;
    output_channels_[1] = Chan_Green;
    output_channels_[2] = Chan_Blue;
    output_channels_[3] = Chan_Alpha;
    pref_channels_[0] = Chan_Red;
    pref_channels_[1] = Chan_Green;
    pref_channels_[2] = Chan_Blue;
    pref_channels_[3] = Chan_Alpha;
    pref_keep_alpha_ = true;
  }

  void reset_center_to_format() {
    const Format& f = format();
    center_[0] = 0.5 * (static_cast<double>(f.x()) + static_cast<double>(f.r()));
    center_[1] = 0.5 * (static_cast<double>(f.y()) + static_cast<double>(f.t()));
  }

  void reset_domainwarp_center_to_format() {
    const Format& f = format();
    domainwarp_center_[0] = 0.5 * (static_cast<double>(f.x()) + static_cast<double>(f.r()));
    domainwarp_center_[1] = 0.5 * (static_cast<double>(f.y()) + static_cast<double>(f.t()));
  }

  static float clamp01(float v) {
    return std::max(0.0f, std::min(1.0f, v));
  }

  template <typename T>
  static T clamp_compat(T value, T lo, T hi) {
    return std::max(lo, std::min(hi, value));
  }

  static float noise4_compat(float x, float y, float z, float w) {
#if defined(kDDImageVersionMajorNum) && (kDDImageVersionMajorNum >= 14)
    return static_cast<float>(noise(x, y, z, w));
#else
    // Nuke 13.x only exposes 3D noise; fold time into XYZ with fixed offsets.
    const float wx = w * 0.754877666f;
    const float wy = w * 0.569840296f;
    const float wz = w * 0.438579212f;
    return static_cast<float>(noise(x + wx, y + wy, z + wz));
#endif
  }

  static float quantize_unit_value(float v, float levels) {
    const float clamped = clamp01(v);
    if (levels <= 1.0f) {
      return clamped;
    }
    const float steps = levels - 1.0f;
    return std::round(clamped * steps) / steps;
  }

  static float quantize_cache_key_component(float v, float step) {
    if (step <= 0.0f) {
      return v;
    }
    return std::round(v / step) * step;
  }

  static void build_fractal_octave_tables(int fractal_mode, int real_octaves,
                                          float lacunarity, float gain,
                                          std::vector<float>& out_freqs,
                                          std::vector<float>& out_amps,
                                          float& out_amp_sum) {
    const int oct = (fractal_mode == FRACTAL_NONE) ? 1 : std::max(1, real_octaves);
    out_freqs.resize(static_cast<size_t>(oct));
    out_amps.resize(static_cast<size_t>(oct));
    const double lac = std::max(1e-4, static_cast<double>(lacunarity));
    const double g = std::max(0.0, static_cast<double>(gain));
    double freq = 1.0;
    double amp = 1.0;
    double amp_sum = 0.0;
    for (int i = 0; i < oct; ++i) {
      out_freqs[static_cast<size_t>(i)] = static_cast<float>(freq);
      out_amps[static_cast<size_t>(i)] = static_cast<float>(amp);
      amp_sum += amp;
      freq *= lac;
      amp *= g;
    }
    out_amp_sum = static_cast<float>(amp_sum);
  }

  static bool fractal_params_match(int lhs_mode, int lhs_octaves,
                                   float lhs_lacunarity, float lhs_gain,
                                   int rhs_mode, int rhs_octaves,
                                   double rhs_lacunarity, double rhs_gain) {
    return lhs_mode == rhs_mode &&
           lhs_octaves == rhs_octaves &&
           std::fabs(static_cast<double>(lhs_lacunarity) - rhs_lacunarity) <= 1e-7 &&
           std::fabs(static_cast<double>(lhs_gain) - rhs_gain) <= 1e-7;
  }

  bool resolve_cached_octave_tables(int fractal_mode, int real_octaves,
                                    double lacunarity, double gain,
                                    const float*& out_freqs,
                                    const float*& out_amps,
                                    int& out_octaves,
                                    float& out_amp_sum) const {
    out_freqs = nullptr;
    out_amps = nullptr;
    out_octaves = 0;
    out_amp_sum = 0.0f;

    return false;
  }

  static bool is_supported_noise_type(int noise_type) {
    switch (noise_type) {
      case NOISE_PERLIN:
      case NOISE_SIMPLEX:
      case NOISE_VORONOI:
      case NOISE_PATTERN:
        return true;
      default:
        return false;
    }
  }

  static int sanitize_noise_type(int noise_type) {
    // Backward compatibility for archived scene values from removed types.
    switch (noise_type) {
      case 0:
        return NOISE_PERLIN;
      case 1:
        return NOISE_SIMPLEX;
      case 2:   // Legacy Curl
      case 13:  // Legacy Simplex2S
        return NOISE_SIMPLEX;
      case 3:
        return NOISE_VORONOI;
      case 9:   // Legacy Worley F2-F1
        return NOISE_VORONOI;
      case 4:
      case 5:   // Legacy Gyroid previously reused as Pattern
        return NOISE_PATTERN;
      default:
        return NOISE_PERLIN;
    }
  }

  static int noise_type_to_menu_index(int noise_type) {
    switch (sanitize_noise_type(noise_type)) {
      case NOISE_SIMPLEX:
        return 1;
      case NOISE_VORONOI:
        return 2;
      case NOISE_PATTERN:
        return 3;
      case NOISE_PERLIN:
      default:
        return 0;
    }
  }

  static int noise_type_from_menu_index(int menu_index) {
    switch (clamp_compat(menu_index, 0, 3)) {
      case 1:
        return NOISE_SIMPLEX;
      case 2:
        return NOISE_VORONOI;
      case 3:
        return NOISE_PATTERN;
      case 0:
      default:
        return NOISE_PERLIN;
    }
  }

  static int sanitize_pattern_type_mode(int pattern_type_mode) {
    // Legacy "Spiral" mode is folded into Concentric + Twist.
    if (pattern_type_mode == PATTERN_SPIRAL) {
      return PATTERN_CONCENTRIC;
    }
    return clamp_compat(pattern_type_mode,
                        static_cast<int>(PATTERN_CONCENTRIC),
                        static_cast<int>(PATTERN_RADIAL));
  }

  static int sanitize_pattern_shape_mode() {
    // Pattern metric is fixed to Round for stability.
    return PATTERN_SHAPE_ROUND;
  }

  static int snap_raster_coord_for_post_pixel(int coord, float pixel_step) {
    const float step = std::max(1.0f, pixel_step);
    const float snapped =
        std::floor(static_cast<float>(coord) / step) * step + 0.5f * step;
    return static_cast<int>(std::floor(snapped));
  }

  void apply_pref_pixelize_coords(float& sx, float& sy, float& sz) const {
    const float px = std::max(0.0f, static_cast<float>(pixel_size_));
    if (px <= 0.0f) {
      return;
    }
    const float base_scale =
        std::max(1.0f, static_cast<float>((std::max(1.0, xsize_) + std::max(1.0, ysize_)) * 0.5));
    const float step = std::max(1e-4f, (std::max(1.0f, px) * 10.0f) / base_scale);
    sx = std::floor(sx / step) * step + 0.5f * step;
    sy = std::floor(sy / step) * step + 0.5f * step;
    sz = std::floor(sz / step) * step + 0.5f * step;
  }

  struct ChannelOffsets {
    float dx[3];
    float dy[3];
    float dt[3];
  };

  static ChannelOffsets zero_channel_offsets() {
    ChannelOffsets offsets = {};
    for (int i = 0; i < 3; ++i) {
      offsets.dx[i] = 0.0f;
      offsets.dy[i] = 0.0f;
      offsets.dt[i] = 0.0f;
    }
    return offsets;
  }

  static bool channel_offsets_are_zero(const ChannelOffsets& offsets) {
    for (int i = 0; i < 3; ++i) {
      if (std::fabs(offsets.dx[i]) > 1e-6f ||
          std::fabs(offsets.dy[i]) > 1e-6f ||
          std::fabs(offsets.dt[i]) > 1e-6f) {
        return false;
      }
    }
    return true;
  }

  static void animated_translate_from_frame(float speed, float angle_degrees, float frame,
                                            float& tx, float& ty,
                                            float effect_scale = 0.5f) {
    const float distance =
        std::max(0.0f, speed) * std::max(0.0f, effect_scale) * frame;
    const float angle = radians(angle_degrees);
    tx = -std::cos(angle) * distance;
    ty = -std::sin(angle) * distance;
  }

  static float zspeed_scale_for_noise_type(int noise_type) {
    switch (noise_type) {
      case NOISE_PERLIN:
      case NOISE_SIMPLEX:
      case NOISE_VORONOI:
        return 0.1f;
      case NOISE_PATTERN:
        return 2.0f;
      default:
        return 1.0f;
    }
  }

  static float animated_seed_from_z_params(double zoffset, double zspeed,
                                           float frame, int noise_type,
                                           bool apply_2d_tuning = true) {
    const float z_anim = static_cast<float>(zspeed * 0.01) *
                         (apply_2d_tuning ? zspeed_scale_for_noise_type(noise_type) : 1.0f) *
                         frame;
    return static_cast<float>(zoffset) + z_anim;
  }

  static float remap_rgb_size_log(float size) {
    const float s = std::max(0.0f, size);
    const float normalized = s * 0.01f;
    // 100 -> large offset, <100 stays comparatively gentle.
    constexpr float kMaxAt100 = 20000.0f;
    const float mapped = std::expm1(normalized * std::log1p(kMaxAt100));
    return std::isfinite(mapped) ? mapped : 1.0e30f;
  }

  static float domainwarp_amount_scale_from_knob(double domainwarp_amount_value) {
    const float n = std::max(0.0f, static_cast<float>(domainwarp_amount_value) * 0.01f);
    return 3000.0f * n * n;
  }

  static float domainwarp_effective_map_blur_from_knob(double blur_value) {
    const float blur = std::max(0.0f, static_cast<float>(blur_value));
    // Ignore tiny blur values that cost a lot but are visually near-zero.
    return (blur <= 0.15f) ? 0.0f : blur;
  }

  static float domainwarp_pref_map_blur_radius_from_params(double blur_value,
                                                           double domainwarp_xsize,
                                                           double domainwarp_ysize) {
    const float blur = domainwarp_effective_map_blur_from_knob(blur_value);
    if (blur <= 1e-6f) {
      return 0.0f;
    }
    // Keep perceived blur stable across scale changes by expressing blur radius
    // in "point-space" units proportional to domain scale.
    const float base_scale = std::max(
        1.0f,
        static_cast<float>(
            (std::max(1.0, domainwarp_xsize) + std::max(1.0, domainwarp_ysize)) * 0.5));
    // 3D map blur was too aggressive and unstable at larger values.
    // Reduce effective radius by 100x to keep blur controllable and consistent.
    const float radius = blur * base_scale * 0.00001f;
    return std::max(0.0f, radius);
  }

  static constexpr float kFixedUvOffset = 50.0f;
  static constexpr float kDomainWarpSpiralNoiseAngle = 90.0f;
  static constexpr float kDomainwarpPatternGamma = 0.5f;

  static ChannelOffsets build_rgb_offsets_for_params(int rgb_mode, int chroma_mode,
                                                     bool invert_chroma, float size,
                                                     float angle_degrees,
                                                     float time_offset) {
    ChannelOffsets offsets = zero_channel_offsets();

    const bool use_offset = (rgb_mode == RGB_MODE_OFFSET || rgb_mode == RGB_MODE_BOTH);
    const bool use_time = (rgb_mode == RGB_MODE_TIME || rgb_mode == RGB_MODE_BOTH);
    const float offset_size = use_offset ? remap_rgb_size_log(size) : 0.0f;
    float dx = offset_size * std::cos(radians(angle_degrees));
    float dy = offset_size * std::sin(radians(angle_degrees));
    float dt = use_time ? (std::max(0.0f, time_offset) * 0.01f) : 0.0f;

    if (invert_chroma) {
      dx = -dx;
      dy = -dy;
      dt = -dt;
    }

    int positive_channel = 0;
    int negative_channel = 2;
    if (chroma_mode == CHROMA_RED_GREEN) {
      positive_channel = 0;
      negative_channel = 1;
    } else if (chroma_mode == CHROMA_BLUE_GREEN) {
      positive_channel = 1;
      negative_channel = 2;
    }

    offsets.dx[positive_channel] = dx;
    offsets.dy[positive_channel] = dy;
    offsets.dt[positive_channel] = dt;

    offsets.dx[negative_channel] = -dx;
    offsets.dy[negative_channel] = -dy;
    offsets.dt[negative_channel] = -dt;

    return offsets;
  }

  ChannelOffsets domainwarp_rgb_offsets() const {
    ChannelOffsets offsets = zero_channel_offsets();
    for (int i = 0; i < 3; ++i) {
      offsets.dx[i] = domainwarp_rgb_dx_[i];
      offsets.dy[i] = domainwarp_rgb_dy_[i];
      offsets.dt[i] = domainwarp_rgb_dt_[i];
    }
    return offsets;
  }

  void set_knob_visibility(const char* name, bool visible) {
    Knob* k = knob(name);
    if (!k) {
      return;
    }
    k->visible(visible);
    k->updateWidgets();
  }

  void update_noise_type_ui() {
    const bool output_vectors = (output_mode_ == OUTPUT_VECTORS);
    const bool output_stmap = (output_mode_ == OUTPUT_STMAP);
    const bool output_normal = (output_mode_ == OUTPUT_NORMAL);
    set_knob_visibility("output_vectors_group", output_vectors);
    set_knob_visibility("output_stmap_group", output_stmap);
    set_knob_visibility("output_normal_group", output_normal);

    const bool rgb_offset = (rgb_mode_ == RGB_MODE_OFFSET || rgb_mode_ == RGB_MODE_BOTH);
    const bool rgb_time = (rgb_mode_ == RGB_MODE_TIME || rgb_mode_ == RGB_MODE_BOTH);
    set_knob_visibility("rgb_size", rgb_offset);
    set_knob_visibility("rgb_angle", rgb_offset);
    set_knob_visibility("rgb_time_offset", rgb_time);

    const bool voronoi_type = (type_ == NOISE_VORONOI);
    const bool pattern_type = (type_ == NOISE_PATTERN);
    set_knob_visibility("octaves", !voronoi_type && !pattern_type);
    set_knob_visibility("octaves_voronoi", voronoi_type);
    set_knob_visibility("octaves_pattern", pattern_type);

    set_knob_visibility("voronoi_group", voronoi_type);
    set_knob_visibility("voronoi_metric", voronoi_type);
    set_knob_visibility("voronoi_shape_mode", voronoi_type);
    const bool voronoi_minkowski_visible =
        voronoi_type && (voronoi_metric_ == VORONOI_MINKOWSKI);
    set_knob_visibility("voronoi_minkowski_exp", voronoi_minkowski_visible);
    set_knob_visibility("voronoi_minkowski_divider", voronoi_minkowski_visible);
    set_knob_visibility("voronoi_randomness", voronoi_type);
    set_knob_visibility("voronoi_color_offset", voronoi_type);
    set_knob_visibility("voronoi_saturation", voronoi_type);

    set_knob_visibility("pattern_group", pattern_type);
    set_knob_visibility("pattern_type_mode", pattern_type);
    set_knob_visibility("pattern_shape_mode", false);
    const bool pattern_radial = (pattern_type_mode_ == PATTERN_RADIAL);
    const bool pattern_concentric = (pattern_type_mode_ == PATTERN_CONCENTRIC);
    set_knob_visibility("pattern_shape_divider",
                        pattern_type && (pattern_radial || pattern_concentric));
    set_knob_visibility("pattern_segment_count", pattern_type && pattern_radial);
    set_knob_visibility("pattern_twist", pattern_type && pattern_concentric);

    const bool use_custom_warp_map = has_warp_input();
    const bool show_internal_domainwarp_ui = !use_custom_warp_map;
    set_knob_visibility("domainwarp_vector_mode", show_internal_domainwarp_ui);
    set_knob_visibility("domainwarp_output_divider_mode", show_internal_domainwarp_ui);
    set_knob_visibility("domainwarp_show_map", show_internal_domainwarp_ui);
    set_knob_visibility("domainwarp_output_divider_1", show_internal_domainwarp_ui);
    set_knob_visibility("domainwarp_map_blur", show_internal_domainwarp_ui);
    set_knob_visibility("domainwarp_output_divider_2", show_internal_domainwarp_ui);
    set_knob_visibility("domainwarp_output_divider_3", show_internal_domainwarp_ui);
    set_knob_visibility("domainwarp_noise_group", show_internal_domainwarp_ui);
    set_knob_visibility("domainwarp_transform_group", show_internal_domainwarp_ui);
    set_knob_visibility("restore_domainwarp_defaults", show_internal_domainwarp_ui);

    const bool domainwarp_voronoi_type = (domainwarp_type_ == NOISE_VORONOI);
    const bool domainwarp_pattern_type = (domainwarp_type_ == NOISE_PATTERN);
    set_knob_visibility("domainwarp_octaves",
                        show_internal_domainwarp_ui &&
                            !domainwarp_voronoi_type && !domainwarp_pattern_type);
    set_knob_visibility("domainwarp_octaves_voronoi",
                        show_internal_domainwarp_ui && domainwarp_voronoi_type);
    set_knob_visibility("domainwarp_octaves_pattern",
                        show_internal_domainwarp_ui && domainwarp_pattern_type);
    set_knob_visibility("domainwarp_voronoi_group",
                        show_internal_domainwarp_ui && domainwarp_voronoi_type);
    set_knob_visibility("domainwarp_voronoi_metric",
                        show_internal_domainwarp_ui && domainwarp_voronoi_type);
    set_knob_visibility("domainwarp_voronoi_shape_mode",
                        show_internal_domainwarp_ui && domainwarp_voronoi_type);
    const bool domainwarp_voronoi_minkowski_visible =
        show_internal_domainwarp_ui && domainwarp_voronoi_type &&
        (domainwarp_voronoi_metric_ == VORONOI_MINKOWSKI);
    set_knob_visibility("domainwarp_voronoi_minkowski_exp",
                        domainwarp_voronoi_minkowski_visible);
    set_knob_visibility("domainwarp_voronoi_minkowski_divider",
                        domainwarp_voronoi_minkowski_visible);
    set_knob_visibility("domainwarp_voronoi_randomness",
                        show_internal_domainwarp_ui && domainwarp_voronoi_type);
    set_knob_visibility("domainwarp_pattern_group",
                        show_internal_domainwarp_ui && domainwarp_pattern_type);
    set_knob_visibility("domainwarp_pattern_type_mode",
                        show_internal_domainwarp_ui && domainwarp_pattern_type);
    set_knob_visibility("domainwarp_pattern_shape_mode", false);
    const bool domainwarp_pattern_radial =
        (domainwarp_pattern_type_mode_ == PATTERN_RADIAL);
    const bool domainwarp_pattern_concentric =
        (domainwarp_pattern_type_mode_ == PATTERN_CONCENTRIC);
    set_knob_visibility("domainwarp_pattern_shape_divider",
                        show_internal_domainwarp_ui &&
                            domainwarp_pattern_type &&
                            (domainwarp_pattern_radial || domainwarp_pattern_concentric));
    set_knob_visibility("domainwarp_pattern_segment_count",
                        show_internal_domainwarp_ui &&
                            domainwarp_pattern_type && domainwarp_pattern_radial);
    set_knob_visibility("domainwarp_pattern_twist",
                        show_internal_domainwarp_ui &&
                            domainwarp_pattern_type && domainwarp_pattern_concentric);
    const bool curl_type = false;
    set_knob_visibility("domainwarp_divergence_mix", curl_type);
    set_knob_visibility("domainwarp_divergence_divider", curl_type);
    set_knob_visibility("halftone_mode", halftone_enable_);
    set_knob_visibility("halftone_smoothness", halftone_enable_);
    set_knob_visibility("halftone_invert", halftone_enable_);
    const bool halftone_hatches_enabled =
        halftone_enable_ && (halftone_mode_ == HALFTONE_HATCHES);
    set_knob_visibility("halftone_hatch_angle", halftone_hatches_enabled);
    set_knob_visibility("halftone_hatch_count", halftone_hatches_enabled);
    set_knob_visibility("halftone_cell_size", halftone_enable_);
    set_knob_visibility("halftone_strength", halftone_enable_);
    set_knob_visibility("post_divider_halftone_mode", halftone_enable_);
    set_knob_visibility("post_divider_halftone_size", halftone_enable_);
    set_knob_visibility("post_divider_halftone_smooth", halftone_enable_);
    set_knob_visibility("post_divider_halftone_mix", halftone_enable_);
    if (Knob* group = knob("noise_group")) {
      group->updateWidgets();
    }
    if (Knob* group = knob("rgb_group")) {
      group->updateWidgets();
    }
    if (Knob* group = knob("domainwarp_noise_group")) {
      group->updateWidgets();
    }
    if (Knob* group = knob("domainwarp_voronoi_group")) {
      group->updateWidgets();
    }
    if (Knob* group = knob("domainwarp_pattern_group")) {
      group->updateWidgets();
    }
    if (Knob* group = knob("domainwarp_transform_group")) {
      group->updateWidgets();
    }
    if (Knob* group = knob("postprocess_group")) {
      group->updateWidgets();
    }
  }

  static bool domainwarp_flow_cache_key_equal(const DomainwarpFlowCacheKey& a,
                                              const DomainwarpFlowCacheKey& b) {
    return a.format_x == b.format_x &&
           a.format_y == b.format_y &&
           a.format_r == b.format_r &&
           a.format_t == b.format_t &&
           a.projection_mode == b.projection_mode &&
           a.domain_type == b.domain_type &&
           a.fractal_mode == b.fractal_mode &&
           a.octaves == b.octaves &&
           a.vector_mode == b.vector_mode &&
           a.vor_metric == b.vor_metric &&
           a.vor_shape == b.vor_shape &&
           a.pattern_type_mode == b.pattern_type_mode &&
           a.pattern_shape_mode == b.pattern_shape_mode &&
           a.pattern_segment_count == b.pattern_segment_count &&
           a.pattern_twist == b.pattern_twist &&
           a.use_time == b.use_time &&
           a.rgb_has_offsets == b.rgb_has_offsets &&
           a.seed == b.seed &&
           a.relative_tx == b.relative_tx &&
           a.relative_ty == b.relative_ty &&
           a.lacunarity == b.lacunarity &&
           a.gain == b.gain &&
           a.vector_offset == b.vector_offset &&
           a.map_blur == b.map_blur &&
           a.divergence_mix == b.divergence_mix &&
           a.vor_jitter == b.vor_jitter &&
           a.vor_minkowski_exp == b.vor_minkowski_exp &&
           a.center_x == b.center_x &&
           a.center_y == b.center_y &&
           a.translate_x == b.translate_x &&
           a.translate_y == b.translate_y &&
           a.translate_z == b.translate_z &&
           std::memcmp(a.inv_matrix, b.inv_matrix, sizeof(a.inv_matrix)) == 0 &&
           std::memcmp(a.rgb_dx, b.rgb_dx, sizeof(a.rgb_dx)) == 0 &&
           std::memcmp(a.rgb_dy, b.rgb_dy, sizeof(a.rgb_dy)) == 0 &&
           std::memcmp(a.rgb_dt, b.rgb_dt, sizeof(a.rgb_dt)) == 0;
  }

  static bool domainwarp_flow_base_cache_key_equal(const DomainwarpFlowCacheKey& a,
                                                   const DomainwarpFlowCacheKey& b) {
    DomainwarpFlowCacheKey ak = a;
    DomainwarpFlowCacheKey bk = b;
    ak.map_blur = 0.0f;
    bk.map_blur = 0.0f;
    return domainwarp_flow_cache_key_equal(ak, bk);
  }

  void invalidate_domainwarp_flow_cache() {
    Guard guard(domainwarp_flow_cache_lock_);
    domainwarp_flow_cache_valid_ = false;
    domainwarp_flow_cache_width_ = 0;
    domainwarp_flow_cache_height_ = 0;
    domainwarp_flow_cache_data_.reset();
    domainwarp_flow_base_cache_valid_ = false;
    domainwarp_flow_base_cache_width_ = 0;
    domainwarp_flow_base_cache_height_ = 0;
    domainwarp_flow_base_cache_data_.reset();
  }

  struct ProjectionLonCacheResult {
    std::shared_ptr<const std::vector<float>> cos_storage;
    std::shared_ptr<const std::vector<float>> sin_storage;
    const float* cos_data = nullptr;
    const float* sin_data = nullptr;
  };

  ProjectionLonCacheResult ensure_projection_lon_cache_for_format() const {
    static const float kTwoPi = 6.28318530717958647692f;
    const Format& f = format();
    const int min_x = f.x();
    const int width = std::max(1, f.r() - f.x());
    const size_t cache_size = static_cast<size_t>(width) + 1u;

    Guard guard(projection_lon_cache_lock_);
    if (!(projection_lon_cache_valid_ &&
        projection_lon_cache_min_x_ == min_x &&
        projection_lon_cache_width_ == width &&
        projection_lon_cos_cache_ &&
        projection_lon_sin_cache_ &&
        projection_lon_cos_cache_->size() == cache_size &&
        projection_lon_sin_cache_->size() == cache_size)) {
      auto cos_cache = std::make_shared<std::vector<float>>(cache_size, -1.0f);
      auto sin_cache = std::make_shared<std::vector<float>>(cache_size, 0.0f);
      projection_lon_cache_min_x_ = min_x;
      projection_lon_cache_width_ = width;
      const float inv_w = 1.0f / static_cast<float>(width);
      for (int i = 0; i <= width; ++i) {
        const float u = clamp_compat(static_cast<float>(i) * inv_w, 0.0f, 1.0f);
        const float lon = (u - 0.5f) * kTwoPi;
        (*cos_cache)[static_cast<size_t>(i)] = std::cos(lon);
        (*sin_cache)[static_cast<size_t>(i)] = std::sin(lon);
      }
      projection_lon_cos_cache_ = cos_cache;
      projection_lon_sin_cache_ = sin_cache;
      projection_lon_cache_valid_ = true;
    }

    ProjectionLonCacheResult result;
    result.cos_storage = projection_lon_cos_cache_;
    result.sin_storage = projection_lon_sin_cache_;
    result.cos_data = (result.cos_storage && !result.cos_storage->empty())
                          ? result.cos_storage->data()
                          : nullptr;
    result.sin_data = (result.sin_storage && !result.sin_storage->empty())
                          ? result.sin_storage->data()
                          : nullptr;
    return result;
  }

  DomainwarpFlowCacheKey build_domainwarp_flow_cache_key(float domainwarp_seed,
                                                         bool use_time,
                                                         float relative_tx,
                                                         float relative_ty) const {
    DomainwarpFlowCacheKey key = {};
    const Format& f = format();
    key.format_x = f.x();
    key.format_y = f.y();
    key.format_r = f.r();
    key.format_t = f.t();
    key.projection_mode = projection_2d_;
    key.domain_type = domainwarp_type_;
    key.fractal_mode = domainwarp_fractal_mode_;
    key.octaves = domainwarp_real_octaves_;
    key.vector_mode = domainwarp_vector_mode_;
    key.vor_metric = domainwarp_voronoi_metric_;
    key.vor_shape = domainwarp_voronoi_shape_mode_;
    key.pattern_type_mode = domainwarp_pattern_type_mode_;
    key.pattern_shape_mode = domainwarp_pattern_shape_mode_;
    key.pattern_segment_count = domainwarp_pattern_segment_count_;
    key.pattern_twist = domainwarp_pattern_twist_;
    key.use_time = use_time ? 1 : 0;
    key.rgb_has_offsets = domainwarp_rgb_has_offsets_ ? 1 : 0;
    // Fine quantization avoids cache thrash from sub-float jitter while remaining
    // visually identical.
    key.seed = quantize_cache_key_component(domainwarp_seed, 1e-5f);
    key.relative_tx = quantize_cache_key_component(relative_tx, 1e-5f);
    key.relative_ty = quantize_cache_key_component(relative_ty, 1e-5f);
    key.lacunarity = static_cast<float>(domainwarp_lacunarity_);
    key.gain = static_cast<float>(domainwarp_gain_);
    key.vector_offset = kFixedUvOffset;
    key.map_blur = domainwarp_effective_map_blur_from_knob(domainwarp_map_blur_);
    key.divergence_mix = static_cast<float>(domainwarp_divergence_mix_);
    key.vor_jitter = static_cast<float>(domainwarp_voronoi_randomness_);
    key.vor_minkowski_exp = static_cast<float>(domainwarp_voronoi_minkowski_exp_);
    key.center_x = static_cast<float>(domainwarp_center_[0]);
    key.center_y = static_cast<float>(domainwarp_center_[1]);
    key.translate_x = static_cast<float>(domainwarp_translate_[0]);
    key.translate_y = static_cast<float>(domainwarp_translate_[1]);
    key.translate_z = static_cast<float>(domainwarp_translate_[2]);
    for (int i = 0; i < 16; ++i) {
      key.inv_matrix[i] = domainwarp_invmatrix_.array()[i];
    }
    for (int c = 0; c < 3; ++c) {
      key.rgb_dx[c] = domainwarp_rgb_dx_[c];
      key.rgb_dy[c] = domainwarp_rgb_dy_[c];
      key.rgb_dt[c] = domainwarp_rgb_dt_[c];
    }
    return key;
  }

  void knobs(Knob_Callback f) override {
    Tab_knob(f, "Settings");

    BeginGroup(f, "output_group", "Output");
    Format_knob(f, &formats_, "format");
    Divider(f);
    Channel_knob(f, output_channels_, 4, "channels");
    Tooltip(f, "Output layer/channels for generated noise.");
    Bool_knob(f, &output_invert_, "output_invert", "invert");
    ClearFlags(f, Knob::STARTLINE);
    Tooltip(f, "Invert final output values.");
    Channel_knob(f, pref_channels_, 4, "pref_channels", "P channels");
    Tooltip(f, "Channels used as PRef XYZ(A). First three channels drive position.");
    Bool_knob(f, &pref_keep_alpha_, "pref_keep_alpha", "Keep Alpha");
    ClearFlags(f, Knob::STARTLINE);
    Tooltip(f, "Use selected P alpha as validity mask. If alpha channel is missing, alpha is treated as 1.");
    MultiFloat_knob(f, pref_center_latched_, 3, "pref_center_latched");
    SetFlags(f, Knob::INVISIBLE);
    Bool_knob(f, &pref_center_latched_valid_, "pref_center_latched_valid");
    SetFlags(f, Knob::INVISIBLE);
    MultiFloat_knob(f, domainwarp_pref_center_latched_, 3,
                    "domainwarp_pref_center_latched");
    SetFlags(f, Knob::INVISIBLE);
    Bool_knob(f, &domainwarp_pref_center_latched_valid_,
              "domainwarp_pref_center_latched_valid");
    SetFlags(f, Knob::INVISIBLE);
    Divider(f);
    Enumeration_knob(f, &output_mode_, kOutputModes, "output_mode", "Output");
    Tooltip(f, "Final output encoding: grayscale noise, vectors, STMap, or normal map.");
    Enumeration_knob(f, &projection_2d_, kProjectionModes, "projection_2d", "2D projection");
    ClearFlags(f, Knob::STARTLINE);
    Tooltip(f, "2D coordinate projection used when PRef is disconnected.");

    BeginGroup(f, "output_vectors_group", "Vector Output");
    Enumeration_knob(f, &output_vectors_mode_, kDomainWarpVectorModes,
                     "output_vectors_mode", "vectors mode");
    Tooltip(f, "Vector-field mode for Vectors output.");
    if (Knob* output_vectors_divider_mode = Divider(f)) {
      output_vectors_divider_mode->name("output_vectors_divider_mode");
      output_vectors_divider_mode->label("");
    }
    Double_knob(f, &output_vectors_multiply_, "output_vectors_multiply", "vectors multiply");
    SetRange(f, 0.0, 10.0);
    Tooltip(f, "Global multiplier for vector-mode output.");
    Divider(f);
    EndGroup(f);

    BeginGroup(f, "output_stmap_group", "STMap Output");
    Enumeration_knob(f, &output_stmap_mode_, kDomainWarpVectorModes,
                     "output_stmap_mode", "stmap mode");
    Tooltip(f, "Vector-field mode used before STMap remapping.");
    if (Knob* output_stmap_divider_mode = Divider(f)) {
      output_stmap_divider_mode->name("output_stmap_divider_mode");
      output_stmap_divider_mode->label("");
    }
    Double_knob(f, &output_stmap_multiply_, "output_stmap_multiply", "stmap multiply");
    SetRange(f, 0.0, 10.0);
    Tooltip(f, "Scale of vector-to-ST conversion around 0.5.");
    Divider(f);
    EndGroup(f);

    BeginGroup(f, "output_normal_group", "Normal Output");
    Double_knob(f, &output_normal_strength_, "output_normal_strength", "normal strength");
    SetRange(f, 0.0, 20.0);
    Tooltip(f, "Strength used to derive normal-map gradients.");
    Divider(f);
    EndGroup(f);

    EndGroup(f);

    BeginGroup(f, "noise_group", "Noise");
    Int_knob(f, &type_, "type");
    SetFlags(f, Knob::INVISIBLE);
    Enumeration_knob(f, &type_menu_, kNoiseTypes, "type_menu", "type");
    Tooltip(f, "Noise mode.");
    Enumeration_knob(f, &fractal_mode_, kFractalModes, "fractal", "fractal");
    ClearFlags(f, Knob::STARTLINE);
    Tooltip(f, "Fractal style applied to compatible base noises.");
    Divider(f);

    Scale_knob(f, &xsize_, "size", "x/ysize");
    SetRange(f, 1.0, 1000.0);
    Tooltip(f, "Lowest noise frequency.");

    Float_knob(f, &zsize_, "zoffset", "z");
    SetRange(f, 0.0, 5.0);
    Tooltip(f, "Base Z offset. In 2D it offsets Z, in 3D PRef it offsets 4D time (W).");
    Float_knob(f, &zspeed_, "zspeed", "z speed");
    SetRange(f, 0.0, 100.0);
    Tooltip(f, "Auto animation speed for Z/time over frames. Effective Z = z + frame * (z speed / 100).");
    Divider(f);

    Int_knob(f, &octaves_, "octaves");
    SetRange(f, 1.0, 10.0);
    Tooltip(f, "Number of octaves.");
    Int_knob(f, &voronoi_octaves_, "octaves_voronoi", "octaves");
    SetRange(f, 1.0, 10.0);
    Tooltip(f, "Voronoi-specific octave count.");
    Int_knob(f, &pattern_octaves_, "octaves_pattern", "octaves");
    SetRange(f, 1.0, 10.0);
    Tooltip(f, "Pattern-specific octave count.");

    Double_knob(f, &lacunarity_, "lacunarity");
    SetRange(f, 0.0, 10.0);
    Tooltip(f, "Frequency multiplier per octave.");

    Double_knob(f, &gain_, "gain");
    SetRange(f, 0.0, 2.0);
    Tooltip(f, "Amplitude multiplier per octave.");
    Divider(f);

    Float_knob(f, &gamma_, "gamma");
    SetRange(f, 0.0, 2.0);
    Divider(f);

    BeginGroup(f, "voronoi_group", "Voronoi");
    Enumeration_knob(f, &voronoi_shape_mode_, kVoronoiShapeModes, "voronoi_shape_mode", "shape");
    Tooltip(f, "Voronoi style: color Voronoi or topology variants.");
    Divider(f);
    Enumeration_knob(f, &voronoi_metric_, kVoronoiMetrics, "voronoi_metric", "distance function");
    Tooltip(f, "Voronoi distance model.");
    Divider(f);
    Double_knob(f, &voronoi_minkowski_exp_,
                "voronoi_minkowski_exp", "minkowski exp");
    SetRange(f, 0.1, 8.0);
    Tooltip(f, "Exponent used only by Minkowski distance.");
    if (Knob* voronoi_minkowski_divider = Divider(f)) {
      voronoi_minkowski_divider->name("voronoi_minkowski_divider");
      voronoi_minkowski_divider->label("");
    }
    Double_knob(f, &voronoi_randomness_, "voronoi_randomness", "jitter");
    SetRange(f, 0.0, 2.0);
    Tooltip(f, "Voronoi jitter modifier. Values above 1 can create artifacts.");
    Divider(f);
    Double_knob(f, &voronoi_color_offset_, "voronoi_color_offset", "color offset");
    SetRange(f, 0.0, 100.0);
    Tooltip(f, "Phase offset for Voronoi color mode.");
    Double_knob(f, &voronoi_saturation_, "voronoi_saturation", "saturation");
    SetRange(f, 0.0, 1.0);
    Tooltip(f, "Voronoi color saturation (0 = grayscale, 1 = full color).");
    Divider(f);
    EndGroup(f);

    BeginGroup(f, "pattern_group", "Pattern");
    Enumeration_knob(f, &pattern_type_mode_, kPatternTypeModes, "pattern_type_mode", "shape");
    Tooltip(f, "Pattern algorithm.");
    Enumeration_knob(f, &pattern_shape_mode_, kPatternShapeModes, "pattern_shape_mode", "metric");
    ClearFlags(f, Knob::STARTLINE);
    Tooltip(f, "Pattern metric (Round only).");
    if (Knob* pattern_shape_divider = Divider(f)) {
      pattern_shape_divider->name("pattern_shape_divider");
      pattern_shape_divider->label("");
    }
    Int_knob(f, &pattern_segment_count_, "pattern_segment_count", "segments");
    SetRange(f, 1.0, 100.0);
    Tooltip(f, "Number of segments for radial mode.");
    Int_knob(f, &pattern_twist_, "pattern_twist", "twist");
    SetRange(f, -10.0, 10.0);
    Tooltip(f, "Twist amount for concentric mode (0 = pure concentric, non-zero = spiralized).");
    Divider(f);
    EndGroup(f);

    EndGroup(f);

    BeginGroup(f, "transform_group", "Transform");
    Knob* translate_knob = MultiFloat_knob(f, pref_translate_, 3, "translate", "translate");
    SetRange(f, -1000.0, 1000.0);
    SetFlags(f, Knob::SLIDER | Knob::MAGNITUDE | Knob::NO_HANDLES |
                    Knob::NO_PROXYSCALE);
    Tooltip(f, "Unified translate XYZ. In 2D mode only X/Y are used.");
    Divider(f);
    Double_knob(f, &translate_speed_, "translate_speed", "translate speed");
    SetRange(f, 0.0, 100.0);
    Tooltip(f, "Auto translation speed in XY. In 3D (PRef), effective speed is scaled by /50.");
    Double_knob(f, &translate_angle_, "translate_angle", "translate angle");
    SetRange(f, -180.0, 180.0);
    Tooltip(f, "Direction angle for translate speed in degrees.");

    Divider(f);

    Knob* rotate_knob = MultiFloat_knob(f, pref_rotate_, 3, "rotate", "rotate");
    SetRange(f, -180.0, 180.0);
    SetFlags(f, Knob::SLIDER | Knob::MAGNITUDE | Knob::NO_HANDLES |
                    Knob::NO_PROXYSCALE);
    Tooltip(f, "Unified rotate XYZ. In 2D mode only Z is used. In PRef mode XYZ are used.");

    Knob* scale_knob = MultiFloat_knob(f, pref_scale_, 3, "scale", "scale");
    SetRange(f, 0.001, 1000.0);
    SetFlags(f, Knob::SLIDER | Knob::LOG_SLIDER | Knob::MAGNITUDE |
                    Knob::NO_HANDLES | Knob::NO_PROXYSCALE);
    Tooltip(f, "Unified scale XYZ. In 2D mode only X/Y are used.");

    Divider(f);

    Knob* skew_knob = MultiFloat_knob(f, pref_skew_, 3, "skew", "skew");
    SetRange(f, -10.0, 10.0);
    SetFlags(f, Knob::SLIDER | Knob::MAGNITUDE | Knob::NO_HANDLES |
                    Knob::NO_PROXYSCALE);
    Tooltip(f, "Unified skew XYZ. In 2D mode only X/Y are used. In 3D (PRef), skew is boosted x10.");

    Divider(f);

    Knob* center_knob = XY_knob(f, center_, "center", "center");
    SetFlags(f, Knob::NO_PROXYSCALE);
    Tooltip(f, "Center pivot for 2D mode and PRef recenter sampling.");

    Button(f, "reset_center", "Reset Center");
    ClearFlags(f, Knob::STARTLINE);

    Divider(f);

    Knob* rotate2d_knob = XY_knob(f, rotate2d_xy_, "xyrotate", "x/yrotate");
    SetRange(f, -180.0, 180.0);
    SetFlags(f, Knob::SLIDER | Knob::MAGNITUDE | Knob::NO_HANDLES |
                    Knob::NO_PROXYSCALE);
    Tooltip(f, "2D-only X/Y rotation. Used only when PRef input is disconnected.");

    Divider(f);
    EndGroup(f);

    BeginGroup(f, "rgb_group", "RGB");
    Enumeration_knob(f, &rgb_mode_, kRgbTypeModes, "rgb_mode", "type");
    Enumeration_knob(f, &chroma_mode_, kChromaModes, "chroma_mode", "chroma mode");
    Bool_knob(f, &rgb_invert_chroma_, "rgb_invert_chroma", "invert chroma");
    Double_knob(f, &rgb_size_, "rgb_size", "size");
    SetRange(f, 0.0, 100.0);
    Double_knob(f, &rgb_angle_, "rgb_angle", "angle");
    SetRange(f, -180.0, 180.0);
    Double_knob(f, &rgb_time_offset_, "rgb_time_offset", "offset");
    SetRange(f, 0.0, 100.0);
    Divider(f);
    EndGroup(f);

    Button(f, "restore_settings_defaults", "Restore Default");
    Divider(f);
    Python_knob(
        f,
        "__import__('TVectorBlur._credits_link', fromlist=['*']).TVectorBlurCreditsLinkKnob()",
        "credits_settings",
        "");
    SetFlags(f, Knob::STARTLINE);

    Tab_knob(f, "Domain Warping");

    BeginGroup(f, "domainwarp_group", "Output");
    Enumeration_knob(f, &domainwarp_vector_mode_, kDomainWarpVectorModes,
                     "domainwarp_vector_mode", "vector mode");
    Tooltip(f, "Field uses direct vector field. Spiral Noise uses derivative vectors with fixed 90deg rotation.");
    if (Knob* domainwarp_output_divider_mode = Divider(f)) {
      domainwarp_output_divider_mode->name("domainwarp_output_divider_mode");
      domainwarp_output_divider_mode->label("");
    }
    Double_knob(f, &domainwarp_amount_, "domainwarp_amount", "amount");
    SetRange(f, 0.0, 100.0);
    Tooltip(f, "Domain-warp strength. In 3D (PRef), effective amount is scaled by /50.");
    Bool_knob(f, &domainwarp_show_map_, "domainwarp_show_map", "show map");
    ClearFlags(f, Knob::STARTLINE);
    Tooltip(f, "Display the domain-warp vector map instead of the final noise.");
    if (Knob* domainwarp_output_divider_1 = Divider(f)) {
      domainwarp_output_divider_1->name("domainwarp_output_divider_1");
      domainwarp_output_divider_1->label("");
    }
    Double_knob(f, &domainwarp_map_blur_, "domainwarp_map_blur", "blur size");
    SetRange(f, 0.0, 100.0);
    Tooltip(f, "Gaussian blur size applied to the domain warp map.");
    if (Knob* domainwarp_output_divider_2 = Divider(f)) {
      domainwarp_output_divider_2->name("domainwarp_output_divider_2");
      domainwarp_output_divider_2->label("");
    }
    if (Knob* domainwarp_output_divider_3 = Divider(f)) {
      domainwarp_output_divider_3->name("domainwarp_output_divider_3");
      domainwarp_output_divider_3->label("");
    }
    EndGroup(f);

    BeginGroup(f, "domainwarp_noise_group", "Noise");
    Int_knob(f, &domainwarp_type_, "domainwarp_type");
    SetFlags(f, Knob::INVISIBLE);
    Enumeration_knob(f, &domainwarp_type_menu_, kNoiseTypes, "domainwarp_type_menu", "type");
    Tooltip(f, "Domain-warp noise mode.");
    Enumeration_knob(f, &domainwarp_fractal_mode_, kFractalModes,
                     "domainwarp_fractal", "fractal");
    ClearFlags(f, Knob::STARTLINE);
    Tooltip(f, "Fractal style for domain-warp noise.");
    Divider(f);

    Scale_knob(f, &domainwarp_xsize_, "domainwarp_size", "x/ysize");
    SetRange(f, 1.0, 1000.0);
    Tooltip(f, "Domain-warp base frequency.");

    Float_knob(f, &domainwarp_zsize_, "domainwarp_zoffset", "z");
    SetRange(f, 0.0, 5.0);
    Tooltip(f, "Domain-warp base Z/time offset.");
    Float_knob(f, &domainwarp_zspeed_, "domainwarp_zspeed", "z speed");
    SetRange(f, 0.0, 100.0);
    Tooltip(f, "Domain-warp auto animation speed. Effective Z = z + frame * (z speed / 100).");
    Divider(f);

    Int_knob(f, &domainwarp_octaves_, "domainwarp_octaves", "octaves");
    SetRange(f, 1.0, 10.0);
    Tooltip(f, "Number of octaves.");
    Int_knob(f, &domainwarp_voronoi_octaves_, "domainwarp_octaves_voronoi", "octaves");
    SetRange(f, 1.0, 10.0);
    Tooltip(f, "Voronoi-specific octave count for domain warp.");
    Int_knob(f, &domainwarp_pattern_octaves_, "domainwarp_octaves_pattern", "octaves");
    SetRange(f, 1.0, 10.0);
    Tooltip(f, "Pattern-specific octave count for domain warp.");
    Double_knob(f, &domainwarp_lacunarity_, "domainwarp_lacunarity", "lacunarity");
    SetRange(f, 0.0, 10.0);
    Tooltip(f, "Frequency multiplier per octave for the warp vector field.");
    Double_knob(f, &domainwarp_gain_, "domainwarp_gain", "gain");
    SetRange(f, 0.0, 2.0);
    Tooltip(f, "Amplitude multiplier per octave for the warp vector field.");
    Divider(f);
    BeginGroup(f, "domainwarp_voronoi_group", "Voronoi");
    Enumeration_knob(f, &domainwarp_voronoi_shape_mode_, kVoronoiShapeModes,
                     "domainwarp_voronoi_shape_mode", "shape");
    Tooltip(f, "Voronoi style used in the warp field.");
    Divider(f);
    Enumeration_knob(f, &domainwarp_voronoi_metric_, kVoronoiMetrics,
                     "domainwarp_voronoi_metric", "distance function");
    Tooltip(f, "Voronoi distance model for the warp field.");
    Divider(f);
    Double_knob(f, &domainwarp_voronoi_minkowski_exp_,
                "domainwarp_voronoi_minkowski_exp", "minkowski exp");
    SetRange(f, 0.1, 8.0);
    Tooltip(f, "Exponent used only by Minkowski distance.");
    if (Knob* domainwarp_voronoi_minkowski_divider = Divider(f)) {
      domainwarp_voronoi_minkowski_divider->name("domainwarp_voronoi_minkowski_divider");
      domainwarp_voronoi_minkowski_divider->label("");
    }
    Double_knob(f, &domainwarp_voronoi_randomness_,
                "domainwarp_voronoi_randomness", "jitter");
    SetRange(f, 0.0, 2.0);
    Tooltip(f, "Voronoi jitter modifier for domain warp.");
    Divider(f);
    EndGroup(f);

    BeginGroup(f, "domainwarp_pattern_group", "Pattern");
    Enumeration_knob(f, &domainwarp_pattern_type_mode_, kPatternTypeModes,
                     "domainwarp_pattern_type_mode", "shape");
    Tooltip(f, "Domain-warp pattern algorithm.");
    Enumeration_knob(f, &domainwarp_pattern_shape_mode_, kPatternShapeModes,
                     "domainwarp_pattern_shape_mode", "metric");
    ClearFlags(f, Knob::STARTLINE);
    Tooltip(f, "Domain-warp pattern metric (Round only).");
    if (Knob* domainwarp_pattern_shape_divider = Divider(f)) {
      domainwarp_pattern_shape_divider->name("domainwarp_pattern_shape_divider");
      domainwarp_pattern_shape_divider->label("");
    }
    Int_knob(f, &domainwarp_pattern_segment_count_,
             "domainwarp_pattern_segment_count", "segments");
    SetRange(f, 1.0, 100.0);
    Tooltip(f, "Number of segments for radial mode.");
    Int_knob(f, &domainwarp_pattern_twist_, "domainwarp_pattern_twist", "twist");
    SetRange(f, -10.0, 10.0);
    Tooltip(f, "Twist amount for concentric mode (0 = pure concentric, non-zero = spiralized).");
    Divider(f);
    EndGroup(f);

    Double_knob(f, &domainwarp_divergence_mix_, "domainwarp_divergence_mix", "divergence mix");
    SetRange(f, 0.0, 1.0);
    Tooltip(f, "0 = incompressible curl. 1 = compressive gradient field.");
    if (Knob* domainwarp_divergence_divider = Divider(f)) {
      domainwarp_divergence_divider->name("domainwarp_divergence_divider");
      domainwarp_divergence_divider->label("");
    }
    EndGroup(f);

    BeginGroup(f, "domainwarp_transform_group", "Transform");
    Knob* domainwarp_translate_knob =
        MultiFloat_knob(f, domainwarp_translate_, 3, "domainwarp_translate", "translate");
    SetRange(f, -1000.0, 1000.0);
    SetFlags(f, Knob::SLIDER | Knob::MAGNITUDE | Knob::NO_HANDLES |
                    Knob::NO_PROXYSCALE);
    Tooltip(f, "Unified translate XYZ. In 2D mode only X/Y are used.");
    Divider(f);
    Double_knob(f, &domainwarp_translate_speed_,
                "domainwarp_translate_speed", "translate speed");
    SetRange(f, 0.0, 100.0);
    Tooltip(f, "Domain-warp auto translation speed. In 3D (PRef), effective speed is scaled by /50.");
    Double_knob(f, &domainwarp_translate_angle_,
                "domainwarp_translate_angle", "translate angle");
    SetRange(f, -180.0, 180.0);

    Divider(f);

    Knob* domainwarp_rotate_knob =
        MultiFloat_knob(f, domainwarp_rotate_, 3, "domainwarp_rotate", "rotate");
    SetRange(f, -180.0, 180.0);
    SetFlags(f, Knob::SLIDER | Knob::MAGNITUDE | Knob::NO_HANDLES |
                    Knob::NO_PROXYSCALE);
    Tooltip(f, "Unified rotate XYZ. In 2D mode only Z is used. In PRef mode XYZ are used.");

    Knob* domainwarp_scale_knob =
        MultiFloat_knob(f, domainwarp_scale_, 3, "domainwarp_scale", "scale");
    SetRange(f, 0.001, 1000.0);
    SetFlags(f, Knob::SLIDER | Knob::LOG_SLIDER | Knob::MAGNITUDE |
                    Knob::NO_HANDLES | Knob::NO_PROXYSCALE);
    Tooltip(f, "Unified scale XYZ. In 2D mode only X/Y are used.");

    Divider(f);

    Knob* domainwarp_skew_knob =
        MultiFloat_knob(f, domainwarp_skew_, 3, "domainwarp_skew", "skew");
    SetRange(f, -10.0, 10.0);
    SetFlags(f, Knob::SLIDER | Knob::MAGNITUDE | Knob::NO_HANDLES |
                    Knob::NO_PROXYSCALE);
    Tooltip(f, "Unified skew XYZ. In 2D mode only X/Y are used. In 3D (PRef), skew is boosted x10.");

    Divider(f);

    Knob* domainwarp_center_knob = XY_knob(f, domainwarp_center_, "domainwarp_center", "center");
    SetFlags(f, Knob::NO_PROXYSCALE);
    Tooltip(f, "Center pivot for 2D mode and PRef recenter sampling.");
    Button(f, "domainwarp_reset_center", "Reset Center");
    ClearFlags(f, Knob::STARTLINE);

    Divider(f);

    Knob* domainwarp_rotate2d_knob = XY_knob(f, domainwarp_rotate2d_xy_, "domainwarp_xyrotate", "x/yrotate");
    SetRange(f, -180.0, 180.0);
    SetFlags(f, Knob::SLIDER | Knob::MAGNITUDE | Knob::NO_HANDLES |
                    Knob::NO_PROXYSCALE);
    Tooltip(f, "2D-only X/Y rotation. Used only when PRef input is disconnected.");

    Divider(f);
    EndGroup(f);

    Button(f, "restore_domainwarp_defaults", "Restore Default");
    Divider(f);
    Python_knob(
        f,
        "__import__('TVectorBlur._credits_link', fromlist=['*']).TVectorBlurCreditsLinkKnob()",
        "credits_domainwarp",
        "");
    SetFlags(f, Knob::STARTLINE);

    Tab_knob(f, "Post Process");
    BeginGroup(f, "postprocess_group", "Post Process");
    Double_knob(f, &quantize_levels_, "quantize", "quantize");
    SetRange(f, 0.0, 100.0);
    Tooltip(f, "Quantize strength. 0 disables, higher values increase posterization.");
    if (Knob* post_divider_quantize = Divider(f)) {
      post_divider_quantize->name("post_divider_quantize");
      post_divider_quantize->label("");
    }

    Double_knob(f, &pixel_size_, "pixel_size", "pixelate");
    SetRange(f, 0.0, 100.0);
    Tooltip(f, "2D: post pixelization. 3D PRef: voxelized at noise generation.");
    if (Knob* post_divider_pixelate = Divider(f)) {
      post_divider_pixelate->name("post_divider_pixelate");
      post_divider_pixelate->label("");
    }
    Bool_knob(f, &halftone_enable_, "halftone_enable", "halftone");
    Tooltip(f, "Enable halftone post effect.");
    if (Knob* post_divider_halftone_toggle = Divider(f)) {
      post_divider_halftone_toggle->name("post_divider_halftone_toggle");
      post_divider_halftone_toggle->label("");
    }

    Enumeration_knob(f, &halftone_mode_, kHalftoneModes, "halftone_mode", "mode");
    Tooltip(f, "Halftone pattern mode.");
    Bool_knob(f, &halftone_invert_, "halftone_invert", "invert");
    ClearFlags(f, Knob::STARTLINE);
    Tooltip(f, "Invert halftone pattern values.");
    Int_knob(f, &halftone_hatch_count_, "halftone_hatch_count", "hatches");
    SetRange(f, 1.0, 8.0);
    Tooltip(f, "Number of hatch directions.");
    if (Knob* post_divider_halftone_mode = Divider(f)) {
      post_divider_halftone_mode->name("post_divider_halftone_mode");
      post_divider_halftone_mode->label("");
    }

    Double_knob(f, &halftone_cell_size_, "halftone_cell_size", "size");
    SetRange(f, 1.0, 128.0);
    Tooltip(f, "Halftone cell size in pixels.");
    Double_knob(f, &halftone_hatch_angle_, "halftone_hatch_angle", "rotate");
    SetRange(f, -180.0, 180.0);
    Tooltip(f, "Rotation angle for hatch lines.");
    if (Knob* post_divider_halftone_size = Divider(f)) {
      post_divider_halftone_size->name("post_divider_halftone_size");
      post_divider_halftone_size->label("");
    }

    Double_knob(f, &halftone_smoothness_, "halftone_smoothness", "smooth");
    SetRange(f, 0.0, 1.0);
    Tooltip(f, "Edge smoothing amount for dots/hatches.");
    if (Knob* post_divider_halftone_smooth = Divider(f)) {
      post_divider_halftone_smooth->name("post_divider_halftone_smooth");
      post_divider_halftone_smooth->label("");
    }

    Double_knob(f, &halftone_strength_, "halftone_strength", "mix");
    SetRange(f, 0.0, 1.0);
    Tooltip(f, "Blend between original output and halftone (0..1).");
    if (Knob* post_divider_halftone_mix = Divider(f)) {
      post_divider_halftone_mix->name("post_divider_halftone_mix");
      post_divider_halftone_mix->label("");
    }

    EndGroup(f);
    Python_knob(
        f,
        "__import__('TVectorBlur._credits_link', fromlist=['*']).TVectorBlurCreditsLinkKnob()",
        "credits_postprocess",
        "");
    SetFlags(f, Knob::STARTLINE);

    // Apply compact default directly on freshly created transform knobs.
    // This must run in makeKnobs() to avoid store-pass timing issues.
    if (f.makeKnobs()) {
      type_ = sanitize_noise_type(type_);
      domainwarp_type_ = sanitize_noise_type(domainwarp_type_);
      type_menu_ = noise_type_to_menu_index(type_);
      domainwarp_type_menu_ = noise_type_to_menu_index(domainwarp_type_);
      if (Knob* kk = knob("type")) {
        kk->set_value(type_);
      }
      if (Knob* kk = knob("domainwarp_type")) {
        kk->set_value(domainwarp_type_);
      }
      if (Knob* kk = knob("type_menu")) {
        kk->set_value(type_menu_);
        kk->updateWidgets();
      }
      if (Knob* kk = knob("domainwarp_type_menu")) {
        kk->set_value(domainwarp_type_menu_);
        kk->updateWidgets();
      }

      struct KnobInit {
        Knob* knob;
        const char* value;
      };
      const KnobInit compact_knobs[] = {
          {translate_knob, "0"},
          {rotate_knob, "0"},
          {scale_knob, "1"},
          {skew_knob, "0"},
          {rotate2d_knob, "0 0"},
          {domainwarp_translate_knob, "0"},
          {domainwarp_rotate_knob, "0"},
          {domainwarp_scale_knob, "1"},
          {domainwarp_skew_knob, "0"},
          {domainwarp_rotate2d_knob, "0 0"},
      };

      center_[0] = 0.0;
      center_[1] = 0.0;
      domainwarp_center_[0] = 0.0;
      domainwarp_center_[1] = 0.0;
      center_initialized_ = true;
      domainwarp_center_initialized_ = true;
      if (center_knob) {
        center_knob->set_value(center_[0], 0);
        center_knob->set_value(center_[1], 1);
        center_knob->updateWidgets();
      }
      if (domainwarp_center_knob) {
        domainwarp_center_knob->set_value(domainwarp_center_[0], 0);
        domainwarp_center_knob->set_value(domainwarp_center_[1], 1);
        domainwarp_center_knob->updateWidgets();
      }

      for (const KnobInit& init : compact_knobs) {
        if (init.knob) {
          init.knob->from_script(init.value);
          init.knob->updateWidgets();
        }
      }
      update_noise_type_ui();
    }
  }

  void set_knob_scalar(const char* name, double value) {
    if (Knob* kk = knob(name)) {
      kk->set_value(value);
      kk->updateWidgets();
    }
  }

  void set_knob_integer(const char* name, int value) {
    if (Knob* kk = knob(name)) {
      kk->set_value(value);
      kk->updateWidgets();
    }
  }

  void set_knob_bool(const char* name, bool value) {
    if (Knob* kk = knob(name)) {
      kk->set_value(value ? 1.0 : 0.0);
      kk->updateWidgets();
    }
  }

  void set_knob_xy(const char* name, double x, double y) {
    if (Knob* kk = knob(name)) {
      kk->set_value(x, 0);
      kk->set_value(y, 1);
      kk->updateWidgets();
    }
  }

  void set_knob_script(const char* name, const char* value) {
    if (!value) {
      return;
    }
    if (Knob* kk = knob(name)) {
      kk->from_script(value);
      kk->updateWidgets();
    }
  }

  void set_knob_xyz(const char* name, double x, double y, double z) {
    if (Knob* kk = knob(name)) {
      kk->set_value(x, 0);
      kk->set_value(y, 1);
      kk->set_value(z, 2);
      kk->updateWidgets();
    }
  }

  void set_knob_channels4(const char* name, const Channel channels[4]) {
    if (Knob* kk = knob(name)) {
      for (int i = 0; i < 4; ++i) {
        kk->set_value(static_cast<double>(channels[i]), i);
      }
      kk->updateWidgets();
    }
  }

  void set_knob_float3(const char* name, const float values[3]) {
    if (Knob* kk = knob(name)) {
      for (int i = 0; i < 3; ++i) {
        kk->set_value(static_cast<double>(values[i]), i);
      }
      kk->updateWidgets();
    }
  }

  void store_latched_pref_center(const char* value_knob,
                                 const char* valid_knob,
                                 float stored[3],
                                 bool& stored_valid,
                                 const float sampled[3],
                                 bool sampled_valid) {
    if (sampled_valid && sampled) {
      stored[0] = sampled[0];
      stored[1] = sampled[1];
      stored[2] = sampled[2];
      stored_valid = true;
    } else {
      stored[0] = 0.0f;
      stored[1] = 0.0f;
      stored[2] = 0.0f;
      stored_valid = false;
    }
    set_knob_float3(value_knob, stored);
    set_knob_bool(valid_knob, stored_valid);
  }

  void restore_settings_defaults() {
    output_invert_ = false;
    output_mode_ = OUTPUT_NOISE;
    projection_2d_ = PROJECTION_PLANAR;
    output_vectors_mode_ = DW_VECTOR_FIELD;
    output_vectors_multiply_ = 1.0;
    output_stmap_mode_ = DW_VECTOR_FIELD;
    output_stmap_multiply_ = 1.0;
    output_normal_strength_ = 1.0;

    type_ = NOISE_PERLIN;
    xsize_ = 350.0;
    ysize_ = 350.0;
    zsize_ = 0.0;
    zspeed_ = 0.0;
    fractal_mode_ = FRACTAL_FBM;
    octaves_ = 10;
    voronoi_octaves_ = 1;
    pattern_octaves_ = 1;
    lacunarity_ = 2.0;
    gain_ = 0.5;
    gamma_ = 0.5f;

    voronoi_shape_mode_ = VORONOI_SHAPE_VORONOI;
    voronoi_metric_ = VORONOI_EUCLIDEAN;
    voronoi_minkowski_exp_ = 1.0;
    voronoi_randomness_ = 1.0;
    voronoi_color_offset_ = 0.0;
    voronoi_saturation_ = 1.0;
    voronoi_color_seed_ = 750;

    pattern_type_mode_ = PATTERN_CONCENTRIC;
    pattern_shape_mode_ = PATTERN_SHAPE_ROUND;
    pattern_segment_count_ = 10;
    pattern_twist_ = 0;

    pref_translate_[0] = 0.0;
    pref_translate_[1] = 0.0;
    pref_translate_[2] = 0.0;
    pref_rotate_[0] = 0.0;
    pref_rotate_[1] = 0.0;
    pref_rotate_[2] = 0.0;
    pref_scale_[0] = 1.0;
    pref_scale_[1] = 1.0;
    pref_scale_[2] = 1.0;
    pref_skew_[0] = 0.0;
    pref_skew_[1] = 0.0;
    pref_skew_[2] = 0.0;
    rotate2d_xy_[0] = 0.0;
    rotate2d_xy_[1] = 0.0;
    translate_speed_ = 0.0;
    translate_angle_ = 0.0;
    center_[0] = 0.0;
    center_[1] = 0.0;
    center_initialized_ = true;
    pref_center_latched_[0] = 0.0f;
    pref_center_latched_[1] = 0.0f;
    pref_center_latched_[2] = 0.0f;
    pref_center_latched_valid_ = false;

    rgb_mode_ = RGB_MODE_OFFSET;
    chroma_mode_ = CHROMA_RED_BLUE;
    rgb_invert_chroma_ = false;
    rgb_size_ = 0.0;
    rgb_angle_ = 0.0;
    rgb_time_offset_ = 0.0;

    output_channels_[0] = Chan_Red;
    output_channels_[1] = Chan_Green;
    output_channels_[2] = Chan_Blue;
    output_channels_[3] = Chan_Alpha;

    set_knob_bool("output_invert", output_invert_);
    set_knob_integer("output_mode", output_mode_);
    set_knob_integer("projection_2d", projection_2d_);
    set_knob_integer("output_vectors_mode", output_vectors_mode_);
    set_knob_scalar("output_vectors_multiply", output_vectors_multiply_);
    set_knob_integer("output_stmap_mode", output_stmap_mode_);
    set_knob_scalar("output_stmap_multiply", output_stmap_multiply_);
    set_knob_scalar("output_normal_strength", output_normal_strength_);

    type_menu_ = noise_type_to_menu_index(type_);
    set_knob_integer("type", type_);
    set_knob_integer("type_menu", type_menu_);
    set_knob_script("size", "350");
    set_knob_scalar("zoffset", zsize_);
    set_knob_scalar("zspeed", zspeed_);
    set_knob_integer("fractal", fractal_mode_);
    set_knob_integer("octaves", octaves_);
    set_knob_integer("octaves_voronoi", voronoi_octaves_);
    set_knob_integer("octaves_pattern", pattern_octaves_);
    set_knob_scalar("lacunarity", lacunarity_);
    set_knob_scalar("gain", gain_);
    set_knob_scalar("gamma", gamma_);

    set_knob_integer("voronoi_shape_mode", voronoi_shape_mode_);
    set_knob_integer("voronoi_metric", voronoi_metric_);
    set_knob_scalar("voronoi_minkowski_exp", voronoi_minkowski_exp_);
    set_knob_scalar("voronoi_randomness", voronoi_randomness_);
    set_knob_scalar("voronoi_color_offset", voronoi_color_offset_);
    set_knob_scalar("voronoi_saturation", voronoi_saturation_);

    set_knob_integer("pattern_type_mode", pattern_type_mode_);
    set_knob_integer("pattern_shape_mode", pattern_shape_mode_);
    set_knob_integer("pattern_segment_count", pattern_segment_count_);
    set_knob_integer("pattern_twist", pattern_twist_);

    set_knob_script("translate", "0");
    set_knob_script("rotate", "0");
    set_knob_script("scale", "1");
    set_knob_script("skew", "0");
    set_knob_scalar("translate_speed", translate_speed_);
    set_knob_scalar("translate_angle", translate_angle_);
    set_knob_xy("center", center_[0], center_[1]);
    set_knob_script("xyrotate", "0 0");

    set_knob_integer("rgb_mode", rgb_mode_);
    set_knob_integer("chroma_mode", chroma_mode_);
    set_knob_bool("rgb_invert_chroma", rgb_invert_chroma_);
    set_knob_scalar("rgb_size", rgb_size_);
    set_knob_scalar("rgb_angle", rgb_angle_);
    set_knob_scalar("rgb_time_offset", rgb_time_offset_);
    set_knob_channels4("pref_channels", pref_channels_);
    set_knob_bool("pref_keep_alpha", pref_keep_alpha_);
    set_knob_float3("pref_center_latched", pref_center_latched_);
    set_knob_bool("pref_center_latched_valid", pref_center_latched_valid_);
    relatch_main_pref_center_from_ui();
  }

  void restore_domainwarp_defaults() {
    domainwarp_amount_ = 0.0;
    domainwarp_show_map_ = false;
    domainwarp_vector_mode_ = DW_VECTOR_FIELD;
    domainwarp_map_blur_ = 0.0;
    domainwarp_divergence_mix_ = 0.0;
    domainwarp_type_ = NOISE_PERLIN;
    domainwarp_fractal_mode_ = FRACTAL_FBM;
    domainwarp_xsize_ = 350.0;
    domainwarp_ysize_ = 350.0;
    domainwarp_zsize_ = 0.0;
    domainwarp_zspeed_ = 0.0;
    domainwarp_octaves_ = 10;
    domainwarp_voronoi_octaves_ = 1;
    domainwarp_pattern_octaves_ = 1;
    domainwarp_lacunarity_ = 2.0;
    domainwarp_gain_ = 0.5;

    domainwarp_voronoi_shape_mode_ = VORONOI_SHAPE_VORONOI;
    domainwarp_voronoi_metric_ = VORONOI_EUCLIDEAN;
    domainwarp_voronoi_minkowski_exp_ = 1.0;
    domainwarp_voronoi_randomness_ = 1.0;

    domainwarp_pattern_type_mode_ = PATTERN_CONCENTRIC;
    domainwarp_pattern_shape_mode_ = PATTERN_SHAPE_ROUND;
    domainwarp_pattern_segment_count_ = 10;
    domainwarp_pattern_twist_ = 0;

    domainwarp_translate_[0] = 0.0;
    domainwarp_translate_[1] = 0.0;
    domainwarp_translate_[2] = 0.0;
    domainwarp_rotate_[0] = 0.0;
    domainwarp_rotate_[1] = 0.0;
    domainwarp_rotate_[2] = 0.0;
    domainwarp_scale_[0] = 1.0;
    domainwarp_scale_[1] = 1.0;
    domainwarp_scale_[2] = 1.0;
    domainwarp_skew_[0] = 0.0;
    domainwarp_skew_[1] = 0.0;
    domainwarp_skew_[2] = 0.0;
    domainwarp_rotate2d_xy_[0] = 0.0;
    domainwarp_rotate2d_xy_[1] = 0.0;
    domainwarp_translate_speed_ = 0.0;
    domainwarp_translate_angle_ = 0.0;
    domainwarp_center_[0] = 0.0;
    domainwarp_center_[1] = 0.0;
    domainwarp_center_initialized_ = true;
    domainwarp_pref_center_latched_[0] = 0.0f;
    domainwarp_pref_center_latched_[1] = 0.0f;
    domainwarp_pref_center_latched_[2] = 0.0f;
    domainwarp_pref_center_latched_valid_ = false;

    set_knob_scalar("domainwarp_amount", domainwarp_amount_);
    set_knob_bool("domainwarp_show_map", domainwarp_show_map_);
    set_knob_integer("domainwarp_vector_mode", domainwarp_vector_mode_);
    set_knob_scalar("domainwarp_map_blur", domainwarp_map_blur_);
    set_knob_scalar("domainwarp_divergence_mix", domainwarp_divergence_mix_);
    domainwarp_type_menu_ = noise_type_to_menu_index(domainwarp_type_);
    set_knob_integer("domainwarp_type", domainwarp_type_);
    set_knob_integer("domainwarp_type_menu", domainwarp_type_menu_);
    set_knob_integer("domainwarp_fractal", domainwarp_fractal_mode_);
    set_knob_script("domainwarp_size", "350");
    set_knob_scalar("domainwarp_zoffset", domainwarp_zsize_);
    set_knob_scalar("domainwarp_zspeed", domainwarp_zspeed_);
    set_knob_integer("domainwarp_octaves", domainwarp_octaves_);
    set_knob_integer("domainwarp_octaves_voronoi", domainwarp_voronoi_octaves_);
    set_knob_integer("domainwarp_octaves_pattern", domainwarp_pattern_octaves_);
    set_knob_scalar("domainwarp_lacunarity", domainwarp_lacunarity_);
    set_knob_scalar("domainwarp_gain", domainwarp_gain_);

    set_knob_integer("domainwarp_voronoi_shape_mode", domainwarp_voronoi_shape_mode_);
    set_knob_integer("domainwarp_voronoi_metric", domainwarp_voronoi_metric_);
    set_knob_scalar("domainwarp_voronoi_minkowski_exp", domainwarp_voronoi_minkowski_exp_);
    set_knob_scalar("domainwarp_voronoi_randomness", domainwarp_voronoi_randomness_);

    set_knob_integer("domainwarp_pattern_type_mode", domainwarp_pattern_type_mode_);
    set_knob_integer("domainwarp_pattern_shape_mode", domainwarp_pattern_shape_mode_);
    set_knob_integer("domainwarp_pattern_segment_count", domainwarp_pattern_segment_count_);
    set_knob_integer("domainwarp_pattern_twist", domainwarp_pattern_twist_);

    set_knob_script("domainwarp_translate", "0");
    set_knob_script("domainwarp_rotate", "0");
    set_knob_script("domainwarp_scale", "1");
    set_knob_script("domainwarp_skew", "0");
    set_knob_scalar("domainwarp_translate_speed", domainwarp_translate_speed_);
    set_knob_scalar("domainwarp_translate_angle", domainwarp_translate_angle_);
    set_knob_xy("domainwarp_center", domainwarp_center_[0], domainwarp_center_[1]);
    set_knob_script("domainwarp_xyrotate", "0 0");
    set_knob_float3(
        "domainwarp_pref_center_latched", domainwarp_pref_center_latched_);
    set_knob_bool(
        "domainwarp_pref_center_latched_valid",
        domainwarp_pref_center_latched_valid_);
    relatch_domainwarp_pref_center_from_ui();
  }

  int knob_changed(Knob* k) override {
    if (!k) {
      return Iop::knob_changed(k);
    }
    const std::string knob_name = k->name();
    if (knob_name.empty()) {
      return Iop::knob_changed(k);
    }

    if (k == &Knob::showPanel) {
      update_noise_type_ui();
      return 1;
    }

    if (knob_name == "type_menu") {
      type_menu_ = clamp_compat(type_menu_, 0, 3);
      type_ = noise_type_from_menu_index(type_menu_);
      if (Knob* type_knob = knob("type")) {
        type_knob->set_value(type_);
      }
      invalidate_domainwarp_flow_cache();
      update_noise_type_ui();
      node_redraw();
      return 1;
    }

    if (knob_name == "domainwarp_type_menu") {
      domainwarp_type_menu_ = clamp_compat(domainwarp_type_menu_, 0, 3);
      domainwarp_type_ = noise_type_from_menu_index(domainwarp_type_menu_);
      if (domainwarp_type_ == NOISE_VORONOI) {
        domainwarp_voronoi_shape_mode_ = VORONOI_SHAPE_VORONOI;
        set_knob_integer("domainwarp_voronoi_shape_mode", domainwarp_voronoi_shape_mode_);
      }
      if (Knob* domain_type_knob = knob("domainwarp_type")) {
        domain_type_knob->set_value(domainwarp_type_);
      }
      invalidate_domainwarp_flow_cache();
      update_noise_type_ui();
      node_redraw();
      return 1;
    }

    const int sanitized_type = sanitize_noise_type(type_);
    if (sanitized_type != type_) {
      type_ = sanitized_type;
      if (Knob* type_knob = knob("type")) {
        type_knob->set_value(type_);
      }
    }
    const int current_type_menu = noise_type_to_menu_index(type_);
    if (current_type_menu != type_menu_) {
      type_menu_ = current_type_menu;
      if (Knob* type_menu_knob = knob("type_menu")) {
        type_menu_knob->set_value(type_menu_);
      }
    }
    const int sanitized_domainwarp_type = sanitize_noise_type(domainwarp_type_);
    if (sanitized_domainwarp_type != domainwarp_type_) {
      domainwarp_type_ = sanitized_domainwarp_type;
      if (Knob* domain_type_knob = knob("domainwarp_type")) {
        domain_type_knob->set_value(domainwarp_type_);
      }
    }
    const int current_domainwarp_type_menu = noise_type_to_menu_index(domainwarp_type_);
    if (current_domainwarp_type_menu != domainwarp_type_menu_) {
      domainwarp_type_menu_ = current_domainwarp_type_menu;
      if (Knob* domain_type_menu_knob = knob("domainwarp_type_menu")) {
        domain_type_menu_knob->set_value(domainwarp_type_menu_);
      }
    }

    invalidate_domainwarp_flow_cache();

    update_noise_type_ui();

    if (knob_name == "center" || knob_name.rfind("center.", 0) == 0) {
      center_initialized_ = true;
      relatch_main_pref_center_from_ui();
      node_redraw();
      return 1;
    }

    if (knob_name == "domainwarp_center" || knob_name.rfind("domainwarp_center.", 0) == 0) {
      domainwarp_center_initialized_ = true;
      relatch_domainwarp_pref_center_from_ui();
      node_redraw();
      return 1;
    }

    if (knob_name == "type") {
      type_ = sanitize_noise_type(type_);
      type_menu_ = noise_type_to_menu_index(type_);
      if (Knob* type_menu_knob = knob("type_menu")) {
        type_menu_knob->set_value(type_menu_);
      }
      update_noise_type_ui();
      node_redraw();
      return 1;
    }

    if (knob_name == "output_mode") {
      if (output_mode_ < OUTPUT_NOISE || output_mode_ > OUTPUT_NORMAL) {
        output_mode_ = OUTPUT_NOISE;
      }
      update_noise_type_ui();
      node_redraw();
      return 1;
    }

    if (knob_name == "projection_2d") {
      if (projection_2d_ < PROJECTION_PLANAR || projection_2d_ > PROJECTION_CYLINDRICAL) {
        projection_2d_ = PROJECTION_PLANAR;
      }
      node_redraw();
      return 1;
    }

    if (knob_name == "pref_channels" ||
        knob_name.rfind("pref_channels.", 0) == 0) {
      if (!pref_channels_[0]) {
        pref_channels_[0] = Chan_Red;
      }
      if (!pref_channels_[1]) {
        pref_channels_[1] = Chan_Green;
      }
      if (!pref_channels_[2]) {
        pref_channels_[2] = Chan_Blue;
      }
      relatch_main_pref_center_from_ui();
      relatch_domainwarp_pref_center_from_ui();
      node_redraw();
      return 1;
    }

    if (knob_name == "domainwarp_type") {
      domainwarp_type_ = sanitize_noise_type(domainwarp_type_);
      domainwarp_type_menu_ = noise_type_to_menu_index(domainwarp_type_);
      if (domainwarp_type_ == NOISE_VORONOI) {
        domainwarp_voronoi_shape_mode_ = VORONOI_SHAPE_VORONOI;
        set_knob_integer("domainwarp_voronoi_shape_mode", domainwarp_voronoi_shape_mode_);
      }
      if (Knob* domain_type_menu_knob = knob("domainwarp_type_menu")) {
        domain_type_menu_knob->set_value(domainwarp_type_menu_);
      }
      update_noise_type_ui();
      node_redraw();
      return 1;
    }

    if (knob_name == "domainwarp_show_map") {
      update_noise_type_ui();
      node_redraw();
      return 1;
    }

    if (knob_name == "restore_settings_defaults") {
      restore_settings_defaults();
      invalidate_domainwarp_flow_cache();
      update_noise_type_ui();
      node_redraw();
      return 1;
    }

    if (knob_name == "restore_domainwarp_defaults") {
      restore_domainwarp_defaults();
      invalidate_domainwarp_flow_cache();
      update_noise_type_ui();
      node_redraw();
      return 1;
    }

    if (knob_name == "voronoi_metric" ||
        knob_name == "voronoi_shape_mode" ||
        knob_name == "voronoi_minkowski_exp" ||
        knob_name == "voronoi_color_seed" ||
        knob_name == "voronoi_color_offset" ||
        knob_name == "voronoi_randomness" ||
        knob_name == "voronoi_saturation" ||
        knob_name == "domainwarp_voronoi_metric" ||
        knob_name == "domainwarp_voronoi_shape_mode" ||
        knob_name == "domainwarp_voronoi_minkowski_exp" ||
        knob_name == "domainwarp_voronoi_randomness") {
      voronoi_metric_ = clamp_compat(voronoi_metric_,
                                   static_cast<int>(VORONOI_EUCLIDEAN),
                                   static_cast<int>(VORONOI_MINKOWSKI));
      voronoi_shape_mode_ = clamp_compat(voronoi_shape_mode_,
                                       static_cast<int>(VORONOI_SHAPE_VORONOI),
                                       static_cast<int>(VORONOI_SHAPE_BUBBLES));
      voronoi_minkowski_exp_ = std::max(0.1, voronoi_minkowski_exp_);
      voronoi_color_seed_ = 750;
      voronoi_color_offset_ = std::max(0.0, voronoi_color_offset_);
      voronoi_randomness_ = std::max(0.0, voronoi_randomness_);
      voronoi_saturation_ = std::max(0.0, voronoi_saturation_);
      domainwarp_voronoi_metric_ = clamp_compat(domainwarp_voronoi_metric_,
                                              static_cast<int>(VORONOI_EUCLIDEAN),
                                              static_cast<int>(VORONOI_MINKOWSKI));
      domainwarp_voronoi_shape_mode_ = clamp_compat(domainwarp_voronoi_shape_mode_,
                                                  static_cast<int>(VORONOI_SHAPE_VORONOI),
                                                  static_cast<int>(VORONOI_SHAPE_BUBBLES));
      domainwarp_voronoi_minkowski_exp_ = std::max(0.1, domainwarp_voronoi_minkowski_exp_);
      domainwarp_voronoi_randomness_ = std::max(0.0, domainwarp_voronoi_randomness_);
      update_noise_type_ui();
      node_redraw();
      return 1;
    }

    if (knob_name == "pattern_type_mode" ||
        knob_name == "pattern_shape_mode" ||
        knob_name == "pattern_segment_count" ||
        knob_name == "pattern_twist") {
      pattern_type_mode_ = sanitize_pattern_type_mode(pattern_type_mode_);
      pattern_shape_mode_ = sanitize_pattern_shape_mode();
      pattern_segment_count_ = std::max(1, pattern_segment_count_);
      update_noise_type_ui();
      node_redraw();
      return 1;
    }

    if (knob_name == "domainwarp_pattern_type_mode" ||
        knob_name == "domainwarp_pattern_shape_mode" ||
        knob_name == "domainwarp_pattern_segment_count" ||
        knob_name == "domainwarp_pattern_twist") {
      domainwarp_pattern_type_mode_ =
          sanitize_pattern_type_mode(domainwarp_pattern_type_mode_);
      domainwarp_pattern_shape_mode_ =
          sanitize_pattern_shape_mode();
      domainwarp_pattern_segment_count_ = std::max(1, domainwarp_pattern_segment_count_);
      update_noise_type_ui();
      node_redraw();
      return 1;
    }

    if (knob_name == "zoffset" ||
        knob_name == "zspeed" ||
        knob_name == "translate_speed" ||
        knob_name == "translate_angle" ||
        knob_name == "rgb_mode" ||
        knob_name == "chroma_mode" ||
        knob_name == "rgb_invert_chroma" ||
        knob_name == "rgb_size" ||
        knob_name == "rgb_angle" ||
        knob_name == "rgb_time_offset") {
      zspeed_ = std::max(0.0, zspeed_);
      translate_speed_ = std::max(0.0, translate_speed_);
      if (rgb_mode_ < RGB_MODE_OFFSET || rgb_mode_ > RGB_MODE_BOTH) {
        rgb_mode_ = RGB_MODE_OFFSET;
      }
      if (chroma_mode_ < CHROMA_RED_BLUE || chroma_mode_ > CHROMA_BLUE_GREEN) {
        chroma_mode_ = CHROMA_RED_BLUE;
      }
      rgb_size_ = std::max(0.0, rgb_size_);
      rgb_time_offset_ = std::max(0.0, rgb_time_offset_);
      update_noise_type_ui();
      node_redraw();
      return 1;
    }

    if (knob_name == "quantize" ||
        knob_name == "pixel_size" ||
        knob_name == "halftone_enable" ||
        knob_name == "halftone_mode" ||
        knob_name == "halftone_smoothness" ||
        knob_name == "halftone_invert" ||
        knob_name == "halftone_hatch_angle" ||
        knob_name == "halftone_hatch_count" ||
        knob_name == "halftone_cell_size" ||
        knob_name == "halftone_strength") {
      quantize_levels_ = std::max(0.0, quantize_levels_);
      pixel_size_ = std::max(0.0, pixel_size_);
      if (halftone_mode_ < HALFTONE_DOTS || halftone_mode_ > HALFTONE_HATCHES) {
        halftone_mode_ = HALFTONE_DOTS;
      }
      halftone_hatch_count_ = clamp_compat(halftone_hatch_count_, 1, 8);
      halftone_smoothness_ = clamp_compat(halftone_smoothness_, 0.0, 1.0);
      halftone_cell_size_ = std::max(1.0, halftone_cell_size_);
      halftone_strength_ = std::max(0.0, halftone_strength_);
      update_noise_type_ui();
      node_redraw();
      return 1;
    }

    if (knob_name == "octaves" ||
        knob_name == "octaves_voronoi" ||
        knob_name == "octaves_pattern") {
      octaves_ = std::max(1, octaves_);
      voronoi_octaves_ = std::max(1, voronoi_octaves_);
      pattern_octaves_ = std::max(1, pattern_octaves_);
      update_noise_type_ui();
      node_redraw();
      return 1;
    }

    if (knob_name == "domainwarp_octaves" ||
        knob_name == "domainwarp_octaves_voronoi" ||
        knob_name == "domainwarp_octaves_pattern") {
      domainwarp_octaves_ = std::max(1, domainwarp_octaves_);
      domainwarp_voronoi_octaves_ = std::max(1, domainwarp_voronoi_octaves_);
      domainwarp_pattern_octaves_ = std::max(1, domainwarp_pattern_octaves_);
      update_noise_type_ui();
      node_redraw();
      return 1;
    }

    if (knob_name == "domainwarp_zoffset" ||
        knob_name == "domainwarp_zspeed" ||
        knob_name == "domainwarp_translate_speed" ||
        knob_name == "domainwarp_translate_angle") {
      domainwarp_zspeed_ = std::max(0.0, domainwarp_zspeed_);
      domainwarp_translate_speed_ = std::max(0.0, domainwarp_translate_speed_);
      update_noise_type_ui();
      node_redraw();
      return 1;
    }

    if (knob_name == "domainwarp_amount" ||
        knob_name == "domainwarp_vector_mode" ||
        knob_name == "domainwarp_map_blur" ||
        knob_name == "domainwarp_divergence_mix" ||
        knob_name == "output_invert" ||
        knob_name == "output_vectors_mode" ||
        knob_name == "output_vectors_multiply" ||
        knob_name == "output_stmap_mode" ||
        knob_name == "output_stmap_multiply" ||
        knob_name == "output_normal_strength") {
      domainwarp_amount_ = std::max(0.0, domainwarp_amount_);
      if (domainwarp_vector_mode_ < DW_VECTOR_FIELD ||
          domainwarp_vector_mode_ > DW_VECTOR_ORBIT_CENTER) {
        domainwarp_vector_mode_ = DW_VECTOR_FIELD;
      }
      domainwarp_map_blur_ = std::max(0.0, domainwarp_map_blur_);
      domainwarp_divergence_mix_ = std::max(0.0, domainwarp_divergence_mix_);
      output_vectors_multiply_ = std::max(0.0, output_vectors_multiply_);
      output_vectors_mode_ = clamp_compat(
          output_vectors_mode_,
          static_cast<int>(DW_VECTOR_FIELD),
          static_cast<int>(DW_VECTOR_ORBIT_CENTER));
      output_stmap_multiply_ = std::max(0.0, output_stmap_multiply_);
      output_stmap_mode_ = clamp_compat(
          output_stmap_mode_,
          static_cast<int>(DW_VECTOR_FIELD),
          static_cast<int>(DW_VECTOR_ORBIT_CENTER));
      output_normal_strength_ = std::max(0.0, output_normal_strength_);
      update_noise_type_ui();
      node_redraw();
      return 1;
    }

    if (knob_name == "reset_center") {
      reset_center_to_format();
      center_initialized_ = true;
      if (Knob* center_knob = knob("center")) {
        center_knob->set_value(center_[0], 0);
        center_knob->set_value(center_[1], 1);
      }
      relatch_main_pref_center_from_ui();
      node_redraw();
      return 1;
    }

    if (knob_name == "domainwarp_reset_center") {
      reset_domainwarp_center_to_format();
      domainwarp_center_initialized_ = true;
      if (Knob* center_knob = knob("domainwarp_center")) {
        center_knob->set_value(domainwarp_center_[0], 0);
        center_knob->set_value(domainwarp_center_[1], 1);
      }
      relatch_domainwarp_pref_center_from_ui();
      node_redraw();
      return 1;
    }

    return Iop::knob_changed(k);
  }

  bool updateUI(const OutputContext&) override {
    update_noise_type_ui();
    return true;
  }

  bool has_pref_input() const {
    Op* pref_input = input(0);
    if (!pref_input) {
      return false;
    }
    Iop* pref_iop = pref_input->iop();
    if (!pref_iop) {
      return false;
    }
#if defined(kDDImageVersionMajorNum) && (kDDImageVersionMajorNum >= 14)
    return !pref_iop->isBlackIop();
#else
    Iop* default_iop = Iop::default_input(outputContext());
    return default_iop ? (pref_iop != default_iop) : true;
#endif
  }

  bool has_pref_input_runtime() const {
    return has_pref_input();
  }

  bool source_has_channel(const ChannelSet& source_channels, Channel c) const {
    return c && source_channels.contains(c);
  }

  bool resolve_pref_channels_from_source_channels(const ChannelSet& source_channels,
                                                  Channel& out_r,
                                                  Channel& out_g,
                                                  Channel& out_b) const {
    out_r = pref_channels_[0] ? pref_channels_[0] : Chan_Red;
    out_g = pref_channels_[1] ? pref_channels_[1] : Chan_Green;
    out_b = pref_channels_[2] ? pref_channels_[2] : Chan_Blue;

    auto has_triplet = [&](Channel a, Channel b, Channel c) {
      return source_has_channel(source_channels, a) &&
             source_has_channel(source_channels, b) &&
             source_has_channel(source_channels, c);
    };

    if (!has_triplet(out_r, out_g, out_b)) {
      static const char* kPreferredLayers[] = {
          "Pref", "PRef", "P", "position", "Position",
      };
      Channel auto_r = out_r;
      Channel auto_g = out_g;
      Channel auto_b = out_b;
      bool resolved = false;
      for (const char* layer : kPreferredLayers) {
        if (resolve_triplet_for_layer(layer, source_channels, auto_r, auto_g, auto_b)) {
          resolved = true;
          break;
        }
      }
      if (resolved) {
        out_r = auto_r;
        out_g = auto_g;
        out_b = auto_b;
      }
    }

    return has_triplet(out_r, out_g, out_b);
  }

  bool resolve_triplet_for_layer(const char* layer_name,
                                 const ChannelSet& source_channels,
                                 Channel& out_x,
                                 Channel& out_y,
                                 Channel& out_z) const {
    if (!layer_name || !*layer_name) {
      return false;
    }

    struct TrioNames {
      const char* a;
      const char* b;
      const char* c;
    };
    static const TrioNames kTriplets[] = {
        {"x", "y", "z"},
        {"X", "Y", "Z"},
        {"red", "green", "blue"},
        {"r", "g", "b"},
    };

    for (const TrioNames& t : kTriplets) {
      const std::string ca = std::string(layer_name) + "." + t.a;
      const std::string cb = std::string(layer_name) + "." + t.b;
      const std::string cc = std::string(layer_name) + "." + t.c;
      const Channel ch_a = findChannel(ca.c_str());
      const Channel ch_b = findChannel(cb.c_str());
      const Channel ch_c = findChannel(cc.c_str());
      if (source_has_channel(source_channels, ch_a) &&
          source_has_channel(source_channels, ch_b) &&
          source_has_channel(source_channels, ch_c)) {
        out_x = ch_a;
        out_y = ch_b;
        out_z = ch_c;
        return true;
      }
    }
    return false;
  }

  bool find_registered_triplet_for_layer(const char* layer_name,
                                         Channel& out_x,
                                         Channel& out_y,
                                         Channel& out_z) const {
    if (!layer_name || !*layer_name) {
      return false;
    }

    struct TrioNames {
      const char* a;
      const char* b;
      const char* c;
    };
    static const TrioNames kTriplets[] = {
        {"x", "y", "z"},
        {"X", "Y", "Z"},
        {"red", "green", "blue"},
        {"r", "g", "b"},
    };

    for (const TrioNames& t : kTriplets) {
      const std::string ca = std::string(layer_name) + "." + t.a;
      const std::string cb = std::string(layer_name) + "." + t.b;
      const std::string cc = std::string(layer_name) + "." + t.c;
      const Channel ch_a = findChannel(ca.c_str());
      const Channel ch_b = findChannel(cb.c_str());
      const Channel ch_c = findChannel(cc.c_str());
      if (ch_a && ch_b && ch_c) {
        out_x = ch_a;
        out_y = ch_b;
        out_z = ch_c;
        return true;
      }
    }
    return false;
  }

  bool resolve_pref_channels_runtime(Channel& out_r,
                                     Channel& out_g,
                                     Channel& out_b) const {
    out_r = pref_channels_[0] ? pref_channels_[0] : Chan_Red;
    out_g = pref_channels_[1] ? pref_channels_[1] : Chan_Green;
    out_b = pref_channels_[2] ? pref_channels_[2] : Chan_Blue;

    if (!has_pref_input()) {
      return false;
    }

    const ChannelSet source_channels = input0().channels();
    if (resolve_pref_channels_from_source_channels(
            source_channels, out_r, out_g, out_b)) {
      return true;
    }

    static const char* kPreferredLayers[] = {
        "Pref", "PRef", "P", "position", "Position",
    };
    for (const char* layer : kPreferredLayers) {
      if (find_registered_triplet_for_layer(layer, out_r, out_g, out_b)) {
        return true;
      }
    }

    // During interactive viewer rebuilds Nuke can ask for hashes/requests while
    // the upstream channel set is still settling. Keep PRef mode stable by
    // falling back to the user-selected triplet instead of flipping to 2D.
    out_r = pref_channels_[0] ? pref_channels_[0] : Chan_Red;
    out_g = pref_channels_[1] ? pref_channels_[1] : Chan_Green;
    out_b = pref_channels_[2] ? pref_channels_[2] : Chan_Blue;
    return out_r && out_g && out_b;
  }

  bool sample_pref_triplet_from_iop(Iop* pref_iop,
                                    double center_x,
                                    double center_y,
                                    float out_center[3]) const {
    if (!pref_iop || !out_center) {
      return false;
    }

    Channel pref_chan_r = Chan_Black;
    Channel pref_chan_g = Chan_Black;
    Channel pref_chan_b = Chan_Black;
    if (!resolve_pref_channels_from_source_channels(
            pref_iop->channels(), pref_chan_r, pref_chan_g, pref_chan_b)) {
      return false;
    }

    const Format& pf = pref_iop->format();
    if (pf.r() <= pf.x() || pf.t() <= pf.y()) {
      return false;
    }

    int cx = static_cast<int>(std::floor(center_x));
    int cy = static_cast<int>(std::floor(center_y));
    cx = std::max(pf.x(), std::min(cx, pf.r() - 1));
    cy = std::max(pf.y(), std::min(cy, pf.t() - 1));

    Row pref_center_row(cx, cx + 1);
    ChannelSet pref_center_mask;
    pref_center_mask += pref_chan_r;
    pref_center_mask += pref_chan_g;
    pref_center_mask += pref_chan_b;
    pref_center_row.get(*pref_iop, cy, cx, cx + 1, pref_center_mask);
    const float* cr = pref_center_row[pref_chan_r];
    const float* cg = pref_center_row[pref_chan_g];
    const float* cb = pref_center_row[pref_chan_b];
    if (!cr || !cg || !cb) {
      return false;
    }

    out_center[0] = cr[cx];
    out_center[1] = cg[cx];
    out_center[2] = cb[cx];
    return std::isfinite(out_center[0]) &&
           std::isfinite(out_center[1]) &&
           std::isfinite(out_center[2]);
  }

  bool sample_pref_triplet_at_context(const OutputContext& sample_context,
                                      double center_x,
                                      double center_y,
                                      float out_center[3]) const {
    Op* pref_op = node_input(0, EXECUTABLE_INPUT, &sample_context);
    if (!pref_op) {
      return false;
    }
    Iop* pref_iop = pref_op->iop();
    if (!pref_iop) {
      return false;
    }
#if defined(kDDImageVersionMajorNum) && (kDDImageVersionMajorNum >= 14)
    if (pref_iop->isBlackIop()) {
      return false;
    }
#endif
    return sample_pref_triplet_from_iop(pref_iop, center_x, center_y, out_center);
  }

  void relatch_pref_center_from_context(double center_x,
                                        double center_y,
                                        float stored[3],
                                        bool& stored_valid,
                                        const char* value_knob,
                                        const char* valid_knob,
                                        const OutputContext& sample_context) {
    float sampled[3] = {0.0f, 0.0f, 0.0f};
    const bool sampled_valid =
        sample_pref_triplet_at_context(sample_context, center_x, center_y, sampled);
    store_latched_pref_center(
        value_knob, valid_knob, stored, stored_valid, sampled, sampled_valid);
  }

  void relatch_main_pref_center_from_ui() {
    const OutputContext sample_context = uiContext();
    relatch_pref_center_from_context(
        center_[0], center_[1],
        pref_center_latched_,
        pref_center_latched_valid_,
        "pref_center_latched",
        "pref_center_latched_valid",
        sample_context);
  }

  void relatch_domainwarp_pref_center_from_ui() {
    const OutputContext sample_context = uiContext();
    relatch_pref_center_from_context(
        domainwarp_center_[0], domainwarp_center_[1],
        domainwarp_pref_center_latched_,
        domainwarp_pref_center_latched_valid_,
        "domainwarp_pref_center_latched",
        "domainwarp_pref_center_latched_valid",
        sample_context);
  }

  bool has_mask_input() const {
    Op* mask_input = input(2);
    if (!mask_input) {
      return false;
    }
    Iop* mask_iop = mask_input->iop();
    if (!mask_iop) {
      return false;
    }
#if defined(kDDImageVersionMajorNum) && (kDDImageVersionMajorNum >= 14)
    return !mask_iop->isBlackIop();
#else
    Iop* default_iop = Iop::default_input(outputContext());
    return default_iop ? (mask_iop != default_iop) : true;
#endif
  }

  bool has_mask_input_runtime() const {
    return has_mask_input();
  }

  bool has_warp_input() const {
    Op* warp_input = input(1);
    if (!warp_input) {
      return false;
    }
    Iop* warp_iop = warp_input->iop();
    if (!warp_iop) {
      return false;
    }
#if defined(kDDImageVersionMajorNum) && (kDDImageVersionMajorNum >= 14)
    return !warp_iop->isBlackIop();
#else
    Iop* default_iop = Iop::default_input(outputContext());
    return default_iop ? (warp_iop != default_iop) : true;
#endif
  }

  bool has_warp_input_runtime() const {
    return has_warp_input();
  }

  static bool nearly_zero_double(double v, double eps = 1e-6) {
    return std::fabs(v) <= eps;
  }

  static bool nearly_one_double(double v, double eps = 1e-6) {
    return std::fabs(v - 1.0) <= eps;
  }





  ChannelSet output_channel_mask() const {
    ChannelSet mask = Mask_None;
    for (int i = 0; i < 4; ++i) {
      if (output_channels_[i]) {
        mask += output_channels_[i];
      }
    }

    if (mask.empty()) {
      mask += Chan_Red;
      mask += Chan_Green;
      mask += Chan_Blue;
      mask += Chan_Alpha;
    }
    return mask;
  }

  void _validate(bool) override {
    if (has_pref_input_runtime()) {
      info_.full_size_format(input0().full_size_format());
      info_.format(input0().format());
    } else if (formats_.format()) {
      info_.full_size_format(*formats_.fullSizeFormat());
      info_.format(*formats_.format());
    } else {
      Iop* fallback = Iop::default_input(outputContext());
      if (!fallback) {
        fallback = input(0);
      }
      info_.full_size_format(fallback->full_size_format());
      info_.format(fallback->format());
    }

    const ChannelSet out_mask = output_channel_mask();
    info_.channels(out_mask);
    set_out_channels(out_mask);
    info_.black_outside(false);
    info_.set(format());
    if (!center_initialized_) {
      reset_center_to_format();
      center_initialized_ = true;
    }
    if (!domainwarp_center_initialized_) {
      reset_domainwarp_center_to_format();
      domainwarp_center_initialized_ = true;
    }
    type_ = sanitize_noise_type(type_);
    type_menu_ = noise_type_to_menu_index(type_);
    if (output_mode_ < OUTPUT_NOISE || output_mode_ > OUTPUT_NORMAL) {
      output_mode_ = OUTPUT_NOISE;
    }
    if (projection_2d_ < PROJECTION_PLANAR || projection_2d_ > PROJECTION_CYLINDRICAL) {
      projection_2d_ = PROJECTION_PLANAR;
    }
    output_vectors_multiply_ = std::max(0.0, output_vectors_multiply_);
    output_vectors_mode_ = clamp_compat(
        output_vectors_mode_,
        static_cast<int>(DW_VECTOR_FIELD),
        static_cast<int>(DW_VECTOR_ORBIT_CENTER));
    output_stmap_multiply_ = std::max(0.0, output_stmap_multiply_);
    output_stmap_mode_ = clamp_compat(
        output_stmap_mode_,
        static_cast<int>(DW_VECTOR_FIELD),
        static_cast<int>(DW_VECTOR_ORBIT_CENTER));
    output_normal_strength_ = std::max(0.0, output_normal_strength_);
    quantize_levels_ = std::max(0.0, quantize_levels_);
    pixel_size_ = std::max(0.0, pixel_size_);
    if (!pref_channels_[0]) {
      pref_channels_[0] = Chan_Red;
    }
    if (!pref_channels_[1]) {
      pref_channels_[1] = Chan_Green;
    }
    if (!pref_channels_[2]) {
      pref_channels_[2] = Chan_Blue;
    }
    if (halftone_mode_ < HALFTONE_DOTS || halftone_mode_ > HALFTONE_HATCHES) {
      halftone_mode_ = HALFTONE_DOTS;
    }
    halftone_hatch_count_ = clamp_compat(halftone_hatch_count_, 1, 8);
    halftone_smoothness_ = clamp_compat(halftone_smoothness_, 0.0, 1.0);
    halftone_cell_size_ = std::max(1.0, halftone_cell_size_);
    halftone_strength_ = std::max(0.0, halftone_strength_);
    zspeed_ = std::max(0.0, zspeed_);
    translate_speed_ = std::max(0.0, translate_speed_);
    if (rgb_mode_ < RGB_MODE_OFFSET || rgb_mode_ > RGB_MODE_BOTH) {
      rgb_mode_ = RGB_MODE_OFFSET;
    }
    if (chroma_mode_ < CHROMA_RED_BLUE || chroma_mode_ > CHROMA_BLUE_GREEN) {
      chroma_mode_ = CHROMA_RED_BLUE;
    }
    rgb_size_ = std::max(0.0, rgb_size_);
    rgb_time_offset_ = std::max(0.0, rgb_time_offset_);
    octaves_ = std::max(1, octaves_);
    voronoi_octaves_ = std::max(1, voronoi_octaves_);
    pattern_octaves_ = std::max(1, pattern_octaves_);
    domainwarp_octaves_ = std::max(1, domainwarp_octaves_);
    domainwarp_voronoi_octaves_ = std::max(1, domainwarp_voronoi_octaves_);
    domainwarp_pattern_octaves_ = std::max(1, domainwarp_pattern_octaves_);
    voronoi_metric_ = clamp_compat(voronoi_metric_,
                                 static_cast<int>(VORONOI_EUCLIDEAN),
                                 static_cast<int>(VORONOI_MINKOWSKI));
    voronoi_shape_mode_ = clamp_compat(voronoi_shape_mode_,
                                     static_cast<int>(VORONOI_SHAPE_VORONOI),
                                     static_cast<int>(VORONOI_SHAPE_BUBBLES));
    voronoi_minkowski_exp_ = std::max(0.1, voronoi_minkowski_exp_);
    voronoi_color_seed_ = 750;
    voronoi_color_offset_ = std::max(0.0, voronoi_color_offset_);
    voronoi_randomness_ = std::max(0.0, voronoi_randomness_);
    voronoi_saturation_ = std::max(0.0, voronoi_saturation_);
    pattern_type_mode_ = sanitize_pattern_type_mode(pattern_type_mode_);
    pattern_shape_mode_ = sanitize_pattern_shape_mode();
    pattern_segment_count_ = std::max(1, pattern_segment_count_);
    domainwarp_amount_ = std::max(0.0, domainwarp_amount_);
    if (domainwarp_vector_mode_ < DW_VECTOR_FIELD ||
        domainwarp_vector_mode_ > DW_VECTOR_ORBIT_CENTER) {
      domainwarp_vector_mode_ = DW_VECTOR_FIELD;
    }
    domainwarp_map_blur_ = std::max(0.0, domainwarp_map_blur_);
    domainwarp_zspeed_ = std::max(0.0, domainwarp_zspeed_);
    domainwarp_translate_speed_ = std::max(0.0, domainwarp_translate_speed_);
    domainwarp_divergence_mix_ = std::max(0.0, domainwarp_divergence_mix_);
    domainwarp_voronoi_metric_ = clamp_compat(domainwarp_voronoi_metric_,
                                            static_cast<int>(VORONOI_EUCLIDEAN),
                                            static_cast<int>(VORONOI_MINKOWSKI));
    domainwarp_voronoi_shape_mode_ = clamp_compat(domainwarp_voronoi_shape_mode_,
                                                static_cast<int>(VORONOI_SHAPE_VORONOI),
                                                static_cast<int>(VORONOI_SHAPE_BUBBLES));
    domainwarp_voronoi_minkowski_exp_ = std::max(0.1, domainwarp_voronoi_minkowski_exp_);
    domainwarp_voronoi_randomness_ = std::max(0.0, domainwarp_voronoi_randomness_);
    domainwarp_pattern_type_mode_ =
        sanitize_pattern_type_mode(domainwarp_pattern_type_mode_);
    domainwarp_pattern_shape_mode_ =
        sanitize_pattern_shape_mode();
    domainwarp_pattern_segment_count_ = std::max(1, domainwarp_pattern_segment_count_);
    domainwarp_type_ = sanitize_noise_type(domainwarp_type_);
    domainwarp_type_menu_ = noise_type_to_menu_index(domainwarp_type_);

    const ChannelOffsets main_offsets = build_rgb_offsets_for_params(
        rgb_mode_, chroma_mode_, rgb_invert_chroma_,
        static_cast<float>(rgb_size_),
        static_cast<float>(rgb_angle_),
        static_cast<float>(rgb_time_offset_));
    main_rgb_has_offsets_ = !channel_offsets_are_zero(main_offsets);
    for (int i = 0; i < 3; ++i) {
      main_rgb_dx_[i] = main_offsets.dx[i];
      main_rgb_dy_[i] = main_offsets.dy[i];
      main_rgb_dt_[i] = main_offsets.dt[i];
    }

    domainwarp_rgb_has_offsets_ = false;
    for (int i = 0; i < 3; ++i) {
      domainwarp_rgb_dx_[i] = 0.0f;
      domainwarp_rgb_dy_[i] = 0.0f;
      domainwarp_rgb_dt_[i] = 0.0f;
    }

    Matrix4 m;
    m.makeIdentity();
    Channel pref_chan_r = Chan_Black;
    Channel pref_chan_g = Chan_Black;
    Channel pref_chan_b = Chan_Black;
    const bool use_pref =
        resolve_pref_channels_runtime(pref_chan_r, pref_chan_g, pref_chan_b);

    constexpr double kPatternSizeScale = 40.0 / 350.0;
    // PRef coordinates are in object space while 2D uses pixel space.
    // This coefficient keeps apparent noise size roughly aligned between 2D and PRef.
    constexpr double kPrefSizeBoost = 1000.0 / 350.0;
    constexpr double kPrefTo2DSizeCoeff = kPrefSizeBoost / 50.0;
    auto apply_transform_stack = [](Matrix4& mat,
                                    bool use_pref_space,
                                    float rot_x, float rot_y, float rot_z,
                                    float skew_x, float skew_y, float skew_z,
                                    float scale_x, float scale_y, float scale_z) {
      mat.rotateZ(radians(rot_z));
      mat.rotateY(radians(rot_y));
      mat.rotateX(radians(rot_x));
      if (use_pref_space) {
        constexpr float kPrefSkewScale = 10.0f;
        mat.skewVec(Vector3(skew_x * kPrefSkewScale,
                            skew_y * kPrefSkewScale,
                            skew_z * kPrefSkewScale));
      } else {
        mat.skewXY(skew_x, skew_y);
      }
      mat.scale(scale_x, scale_y, scale_z);
    };
    const double main_size_scale = (type_ == NOISE_PATTERN) ? kPatternSizeScale : 1.0;
    const double safe_xsize =
        std::max(0.000001, static_cast<double>(xsize_) * main_size_scale);
    const double safe_ysize =
        std::max(0.000001, static_cast<double>(ysize_) * main_size_scale);
    double sx = safe_xsize * pref_scale_[0];
    double sy = safe_ysize * pref_scale_[1];
    double sz = 1.0;
    if (use_pref) {
      const double x_base = std::max(0.0, safe_xsize * kPrefTo2DSizeCoeff);
      const double y_base = std::max(0.0, safe_ysize * kPrefTo2DSizeCoeff);
      const double z_base = std::max(0.0, (safe_xsize + safe_ysize) * 0.5 * kPrefTo2DSizeCoeff);
      sx = x_base * pref_scale_[0];
      sy = y_base * pref_scale_[1];
      sz = z_base * pref_scale_[2];
    } else if (projection_2d_ != PROJECTION_PLANAR) {
      // Surface projections use normalized sphere/cylinder coordinates.
      // Re-map x/ysize around 350 so default behaves like planar defaults.
      const double projection_ref = 350.0;
      sx = (safe_xsize / projection_ref) * pref_scale_[0];
      sy = (safe_ysize / projection_ref) * pref_scale_[1];
      const double z_base =
          std::max(0.000001, ((safe_xsize + safe_ysize) * 0.5) / projection_ref);
      sz = z_base * pref_scale_[2];
    }

    apply_transform_stack(
        m,
        use_pref,
        static_cast<float>(use_pref ? pref_rotate_[0] : rotate2d_xy_[0]),
        static_cast<float>(use_pref ? pref_rotate_[1] : rotate2d_xy_[1]),
        static_cast<float>(pref_rotate_[2]),
        static_cast<float>(pref_skew_[0]),
        static_cast<float>(pref_skew_[1]),
        static_cast<float>(use_pref ? pref_skew_[2] : 0.0),
        static_cast<float>(sx),
        static_cast<float>(sy),
        static_cast<float>(sz));

    uniform_ = false;
    int selected_main_octaves = octaves_;
    if (type_ == NOISE_VORONOI) {
      selected_main_octaves = voronoi_octaves_;
    } else if (type_ == NOISE_PATTERN) {
      selected_main_octaves = pattern_octaves_;
    }
    real_octaves_ = std::max(1, selected_main_octaves);
    main_fractal_mode_eval_ = fractal_mode_;
    main_real_octaves_eval_ = real_octaves_;
    main_lacunarity_eval_ = std::max(1e-4f, static_cast<float>(lacunarity_));
    main_gain_eval_ = std::max(0.0f, static_cast<float>(gain_));
    build_fractal_octave_tables(main_fractal_mode_eval_,
                                main_real_octaves_eval_,
                                main_lacunarity_eval_,
                                main_gain_eval_,
                                main_octave_freqs_,
                                main_octave_amps_,
                                main_octave_amp_sum_);

    float det = m.determinant();
    if (!det || octaves_ < 0) {
      uniform_ = true;
      std::atomic_store_explicit(
          &main_rgb_noise_offsets_cache_,
          std::shared_ptr<const MainRgbNoiseOffsetsCache>{},
          std::memory_order_release);
    } else {
      invmatrix_ = m.inverse(det);
      auto rgb_offsets_cache = std::make_shared<MainRgbNoiseOffsetsCache>();
      // Cache RGB offset deltas in noise space for planar/PRef fast paths.
      // Time offsets stay on the seed axis (W) so Z delta is always 0 here.
      for (int c = 0; c < 3; ++c) {
        rgb_offsets_cache->delta[c] = invmatrix_.transform(
            Vector3(main_rgb_dx_[c], main_rgb_dy_[c], 0.0f));
        rgb_offsets_cache->time_offset[c] = main_rgb_dt_[c];
      }
      std::atomic_store_explicit(
          &main_rgb_noise_offsets_cache_,
          std::shared_ptr<const MainRgbNoiseOffsetsCache>(rgb_offsets_cache),
          std::memory_order_release);
    }

    Matrix4 dw;
    dw.makeIdentity();
    const double domain_size_scale =
        (domainwarp_type_ == NOISE_PATTERN) ? kPatternSizeScale : 1.0;
    const double safe_dwx =
        std::max(0.000001, static_cast<double>(domainwarp_xsize_) * domain_size_scale);
    const double safe_dwy =
        std::max(0.000001, static_cast<double>(domainwarp_ysize_) * domain_size_scale);
    double dw_sx = safe_dwx * domainwarp_scale_[0];
    double dw_sy = safe_dwy * domainwarp_scale_[1];
    double dw_sz = 1.0;
    if (use_pref) {
      const double x_base = std::max(0.0, safe_dwx * kPrefTo2DSizeCoeff);
      const double y_base = std::max(0.0, safe_dwy * kPrefTo2DSizeCoeff);
      const double z_base = std::max(0.0, (safe_dwx + safe_dwy) * 0.5 * kPrefTo2DSizeCoeff);
      dw_sx = x_base * domainwarp_scale_[0];
      dw_sy = y_base * domainwarp_scale_[1];
      dw_sz = z_base * domainwarp_scale_[2];
    } else if (projection_2d_ != PROJECTION_PLANAR) {
      const double projection_ref = 350.0;
      dw_sx = (safe_dwx / projection_ref) * domainwarp_scale_[0];
      dw_sy = (safe_dwy / projection_ref) * domainwarp_scale_[1];
      const double z_base =
          std::max(0.000001, ((safe_dwx + safe_dwy) * 0.5) / projection_ref);
      dw_sz = z_base * domainwarp_scale_[2];
    }

    apply_transform_stack(
        dw,
        use_pref,
        static_cast<float>(use_pref ? domainwarp_rotate_[0] : domainwarp_rotate2d_xy_[0]),
        static_cast<float>(use_pref ? domainwarp_rotate_[1] : domainwarp_rotate2d_xy_[1]),
        static_cast<float>(domainwarp_rotate_[2]),
        static_cast<float>(domainwarp_skew_[0]),
        static_cast<float>(domainwarp_skew_[1]),
        static_cast<float>(use_pref ? domainwarp_skew_[2] : 0.0),
        static_cast<float>(dw_sx),
        static_cast<float>(dw_sy),
        static_cast<float>(dw_sz));

    domainwarp_uniform_ = false;
    int selected_domainwarp_octaves = domainwarp_octaves_;
    if (domainwarp_type_ == NOISE_VORONOI) {
      selected_domainwarp_octaves = domainwarp_voronoi_octaves_;
    } else if (domainwarp_type_ == NOISE_PATTERN) {
      selected_domainwarp_octaves = domainwarp_pattern_octaves_;
    }
    domainwarp_real_octaves_ = std::max(1, selected_domainwarp_octaves);
    domainwarp_fractal_mode_eval_ = domainwarp_fractal_mode_;
    domainwarp_real_octaves_eval_ = domainwarp_real_octaves_;
    domainwarp_lacunarity_eval_ = std::max(1e-4f, static_cast<float>(domainwarp_lacunarity_));
    domainwarp_gain_eval_ = std::max(0.0f, static_cast<float>(domainwarp_gain_));
    build_fractal_octave_tables(domainwarp_fractal_mode_eval_,
                                domainwarp_real_octaves_eval_,
                                domainwarp_lacunarity_eval_,
                                domainwarp_gain_eval_,
                                domainwarp_octave_freqs_,
                                domainwarp_octave_amps_,
                                domainwarp_octave_amp_sum_);
    octave_tables_version_.fetch_add(1, std::memory_order_release);
    const float dw_det = dw.determinant();
    if (!dw_det || selected_domainwarp_octaves < 0) {
      domainwarp_uniform_ = true;
    } else {
      domainwarp_invmatrix_ = dw.inverse(dw_det);
    }

    // Nyquist clipping intentionally disabled: use full user-defined octaves.
  }

  void _request(int x, int y, int r, int t, ChannelMask, int count) override {
    Channel pref_chan_r = Chan_Black;
    Channel pref_chan_g = Chan_Black;
    Channel pref_chan_b = Chan_Black;
    if (has_pref_input_runtime()) {
      resolve_pref_channels_runtime(pref_chan_r, pref_chan_g, pref_chan_b);
      const Channel keep_alpha_chan = output_channels_[3] ? output_channels_[3] : Chan_Alpha;
      ChannelSet pref_request_mask;
      pref_request_mask += pref_chan_r ? pref_chan_r : (pref_channels_[0] ? pref_channels_[0] : Chan_Red);
      pref_request_mask += pref_chan_g ? pref_chan_g : (pref_channels_[1] ? pref_channels_[1] : Chan_Green);
      pref_request_mask += pref_chan_b ? pref_chan_b : (pref_channels_[2] ? pref_channels_[2] : Chan_Blue);
      if (pref_keep_alpha_ &&
          keep_alpha_chan &&
          input0().channels().contains(keep_alpha_chan)) {
        pref_request_mask += keep_alpha_chan;
      }
      input(0)->request(x, y, r, t, pref_request_mask, count);
    }
    if (has_warp_input_runtime()) {
      input(1)->request(x, y, r, t, Mask_RGBA, count);
    }
    if (has_mask_input_runtime()) {
      input(2)->request(x, y, r, t, Mask_RGBA, count);
    }
  }

