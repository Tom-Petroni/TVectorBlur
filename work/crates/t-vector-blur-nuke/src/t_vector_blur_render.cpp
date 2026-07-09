  float evaluate_noise_pref_time(float sx, float sy, float sz, float time_w) const {
    return evaluate_noise_sample(sx, sy, sz, true, time_w, true).r;
  }

  float evaluate_noise(int x, int y) const {
    const float frame = static_cast<float>(outputContext().frame());
    const bool use_time_2d = true;
    const float seed = animated_seed_from_z_params(zsize_, zspeed_, frame, type_, true);
    const float base_z = 0.0f;
    float animated_tx = 0.0f;
    float animated_ty = 0.0f;
    animated_translate_from_frame(
        static_cast<float>(translate_speed_),
        static_cast<float>(translate_angle_),
        frame, animated_tx, animated_ty, 0.5f);
    const bool apply_animated_translate = (projection_2d_ == PROJECTION_PLANAR);
    const float tx = apply_animated_translate ? animated_tx : 0.0f;
    const float ty = apply_animated_translate ? animated_ty : 0.0f;

    // 2D path: keep translate-like behavior as default.
    return evaluate_noise_sample(static_cast<float>(x) + tx,
                                 static_cast<float>(y) + ty, base_z,
                                 false, seed, use_time_2d)
        .r;
  }

  void engine_cpu(int y, int x, int r, ChannelMask channels, Row& row) {
    float* out_channels[4] = {nullptr, nullptr, nullptr, nullptr};
    for (int i = 0; i < 4; ++i) {
      Channel out = output_channels_[i];
      if (out && channels.contains(out)) {
        out_channels[i] = row.writable(out);
      }
    }

    if (!out_channels[0] && !out_channels[1] && !out_channels[2] && !out_channels[3]) {
      return;
    }
    const bool alpha_requested = (out_channels[3] != nullptr);

    Channel pref_chan_r = Chan_Black;
    Channel pref_chan_g = Chan_Black;
    Channel pref_chan_b = Chan_Black;
    const bool use_pref =
        resolve_pref_channels_runtime(pref_chan_r, pref_chan_g, pref_chan_b);
    const bool use_mask = has_mask_input_runtime();
    const bool use_warp = has_warp_input_runtime();
    Iop* mask_iop = nullptr;
    if (use_mask) {
      Op* mask_op = input(2);
      if (mask_op) {
        mask_iop = mask_op->iop();
      }
    }
    Iop* warp_iop = nullptr;
    if (use_warp) {
      Op* warp_op = input(1);
      if (warp_op) {
        warp_iop = warp_op->iop();
      }
    }
    const Channel keep_alpha_chan = output_channels_[3] ? output_channels_[3] : Chan_Alpha;
    const bool keep_alpha_enabled = use_pref && pref_keep_alpha_;
    const bool keep_alpha_available =
        keep_alpha_enabled &&
        keep_alpha_chan &&
        input0().channels().contains(keep_alpha_chan);
    const bool mask_has_alpha = (mask_iop != nullptr) && mask_iop->channels().contains(Chan_Alpha);
    float pref_center[3] = {0.0f, 0.0f, 0.0f};
    bool pref_center_valid = false;
    float domainwarp_pref_center[3] = {0.0f, 0.0f, 0.0f};
    bool domainwarp_pref_center_valid = false;
    const float frame = static_cast<float>(outputContext().frame());
    const bool use_time_2d = true;
    // Keep the same parameter coefficients in 2D and 3D modes.
    const bool apply_2d_tuning = true;
    const float seed = animated_seed_from_z_params(
        zsize_, zspeed_, frame, type_, apply_2d_tuning);
    const float domainwarp_seed = animated_seed_from_z_params(
        domainwarp_zsize_, domainwarp_zspeed_, frame, domainwarp_type_, apply_2d_tuning);
    const float translate_speed_scale = use_pref ? 0.02f : 1.0f;
    const float translate_effect_scale = 0.5f;
    float animated_tx = 0.0f;
    float animated_ty = 0.0f;
    animated_translate_from_frame(
        static_cast<float>(translate_speed_) * translate_speed_scale,
        static_cast<float>(translate_angle_),
        frame, animated_tx, animated_ty, translate_effect_scale);
    float domainwarp_animated_tx = 0.0f;
    float domainwarp_animated_ty = 0.0f;
    animated_translate_from_frame(
        static_cast<float>(domainwarp_translate_speed_) * translate_speed_scale,
        static_cast<float>(domainwarp_translate_angle_),
        frame, domainwarp_animated_tx, domainwarp_animated_ty, translate_effect_scale);
    const bool apply_animated_translate = use_pref || (projection_2d_ == PROJECTION_PLANAR);
    const float main_applied_tx = apply_animated_translate ? animated_tx : 0.0f;
    const float main_applied_ty = apply_animated_translate ? animated_ty : 0.0f;
    const float domain_applied_tx = apply_animated_translate ? domainwarp_animated_tx : 0.0f;
    const float domain_applied_ty = apply_animated_translate ? domainwarp_animated_ty : 0.0f;
    const bool show_domainwarp_map = domainwarp_show_map_ && !use_warp;
    const float domainwarp_amount = domainwarp_amount_scale();
    const float domainwarp_map_blur =
        use_warp ? 0.0f : domainwarp_effective_map_blur_from_knob(domainwarp_map_blur_);
    const float domainwarp_pref_map_blur_radius =
        use_pref ? domainwarp_pref_map_blur_radius_from_params(
                       domainwarp_map_blur_, domainwarp_xsize_, domainwarp_ysize_)
                 : 0.0f;
    const bool needs_warp_vector = !show_domainwarp_map && (domainwarp_amount > 0.0f);
    const bool needs_map_blur = (domainwarp_map_blur > 1e-6f);
    const bool domainwarp_active =
        show_domainwarp_map || needs_warp_vector;
    // Use domain-warp animation offset in absolute terms so blur/cache sampling
    // follows the same motion direction as the animated base coordinates.
    const float domainwarp_relative_tx = domain_applied_tx;
    const float domainwarp_relative_ty = domain_applied_ty;
    const float quantize_steps = quantize_steps_from_strength();
    const bool apply_quantize_post =
        !show_domainwarp_map && (quantize_steps > 1.0f);
    const bool apply_halftone_post =
        !show_domainwarp_map &&
        halftone_enable_ &&
        (clamp_compat(static_cast<float>(halftone_strength_), 0.0f, 1.0f) > 1e-6f);
    const bool rgb_offset_over_post =
        (output_mode_ == OUTPUT_NOISE && main_rgb_has_offsets_ && !show_domainwarp_map);
    const Format& out_format = format();
    const int out_y_min = out_format.y();
    const int out_y_max = std::max(out_y_min, out_format.t() - 1);

    DomainwarpFlowCacheView domainwarp_flow_cache_view;
    const DomainwarpFlowCacheView* domainwarp_flow_cache_view_ptr = nullptr;
    const bool needs_flow_cache =
        !use_warp && !use_pref && (needs_map_blur && domainwarp_active);
    if (needs_flow_cache) {
      const DomainwarpFlowCacheKey flow_cache_key = build_domainwarp_flow_cache_key(
          domainwarp_seed,
          use_time_2d,
          domainwarp_relative_tx,
          domainwarp_relative_ty);
      if (build_domainwarp_flow_cache_view(flow_cache_key, domainwarp_flow_cache_view)) {
        domainwarp_flow_cache_view_ptr = &domainwarp_flow_cache_view;
      }
    }

    if (use_pref) {
      // Read only the serialized center pick state. The latched PRef sample is
      // part of the node state now, not a per-render mutable cache.
      pref_center[0] = pref_center_latched_[0];
      pref_center[1] = pref_center_latched_[1];
      pref_center[2] = pref_center_latched_[2];
      pref_center_valid = pref_center_latched_valid_;
      domainwarp_pref_center[0] = domainwarp_pref_center_latched_[0];
      domainwarp_pref_center[1] = domainwarp_pref_center_latched_[1];
      domainwarp_pref_center[2] = domainwarp_pref_center_latched_[2];
      domainwarp_pref_center_valid = domainwarp_pref_center_latched_valid_;
    }

    struct HalftoneCellSampleCache {
      bool valid = false;
      bool pref_space = false;
      bool use_time = false;
      bool allow_flow_cache = false;
      bool apply_source_rgb_offsets = false;
      bool use_custom_warp_unit = false;
      float main_base_sx = 0.0f;
      float main_base_sy = 0.0f;
      float main_base_sz = 0.0f;
      float domainwarp_base_sx = 0.0f;
      float domainwarp_base_sy = 0.0f;
      float domainwarp_base_sz = 0.0f;
      float main_seed_value = 0.0f;
      float domainwarp_seed_value = 0.0f;
      float main_translate_tx = 0.0f;
      float main_translate_ty = 0.0f;
      float domainwarp_translate_tx = 0.0f;
      float domainwarp_translate_ty = 0.0f;
      float domainwarp_amount_value = 0.0f;
      NoiseRGB custom_warp_unit = {0.0f, 0.0f, 0.0f};
      NoiseRGB sample = {0.0f, 0.0f, 0.0f};

      bool matches(float in_main_sx, float in_main_sy, float in_main_sz,
                   float in_dw_sx, float in_dw_sy, float in_dw_sz,
                   float in_main_seed, float in_dw_seed,
                   float in_main_tx, float in_main_ty,
                   float in_dw_tx, float in_dw_ty,
                   float in_dw_amount,
                   bool in_pref_space, bool in_use_time,
                   bool in_allow_flow, bool in_apply_rgb,
                   bool in_use_custom_warp, const NoiseRGB& in_warp_unit) const {
        return valid &&
               pref_space == in_pref_space &&
               use_time == in_use_time &&
               allow_flow_cache == in_allow_flow &&
               apply_source_rgb_offsets == in_apply_rgb &&
               use_custom_warp_unit == in_use_custom_warp &&
               main_base_sx == in_main_sx && main_base_sy == in_main_sy && main_base_sz == in_main_sz &&
               domainwarp_base_sx == in_dw_sx && domainwarp_base_sy == in_dw_sy && domainwarp_base_sz == in_dw_sz &&
               main_seed_value == in_main_seed && domainwarp_seed_value == in_dw_seed &&
               main_translate_tx == in_main_tx && main_translate_ty == in_main_ty &&
               domainwarp_translate_tx == in_dw_tx && domainwarp_translate_ty == in_dw_ty &&
               domainwarp_amount_value == in_dw_amount &&
               custom_warp_unit.r == in_warp_unit.r &&
               custom_warp_unit.g == in_warp_unit.g &&
               custom_warp_unit.b == in_warp_unit.b;
      }

      void store(float in_main_sx, float in_main_sy, float in_main_sz,
                 float in_dw_sx, float in_dw_sy, float in_dw_sz,
                 float in_main_seed, float in_dw_seed,
                 float in_main_tx, float in_main_ty,
                 float in_dw_tx, float in_dw_ty,
                 float in_dw_amount,
                 bool in_pref_space, bool in_use_time,
                 bool in_allow_flow, bool in_apply_rgb,
                 bool in_use_custom_warp, const NoiseRGB& in_warp_unit,
                 const NoiseRGB& in_sample) {
        valid = true;
        pref_space = in_pref_space; use_time = in_use_time;
        allow_flow_cache = in_allow_flow; apply_source_rgb_offsets = in_apply_rgb;
        use_custom_warp_unit = in_use_custom_warp;
        main_base_sx = in_main_sx; main_base_sy = in_main_sy; main_base_sz = in_main_sz;
        domainwarp_base_sx = in_dw_sx; domainwarp_base_sy = in_dw_sy; domainwarp_base_sz = in_dw_sz;
        main_seed_value = in_main_seed; domainwarp_seed_value = in_dw_seed;
        main_translate_tx = in_main_tx; main_translate_ty = in_main_ty;
        domainwarp_translate_tx = in_dw_tx; domainwarp_translate_ty = in_dw_ty;
        domainwarp_amount_value = in_dw_amount;
        custom_warp_unit = in_warp_unit;
        sample = in_sample;
      }
    };
    HalftoneCellSampleCache halftone_cell_sample_cache;

    auto sample_pipeline = [&](auto&& self,
                               float main_base_sx, float main_base_sy, float main_base_sz,
                               float domainwarp_base_sx, float domainwarp_base_sy, float domainwarp_base_sz,
                               int screen_x, int screen_y,
                               bool pref_space, bool use_time,
                               float main_seed_value,
                               float domainwarp_seed_value,
                               float main_translate_tx,
                               float main_translate_ty,
                               float domainwarp_translate_tx,
                               float domainwarp_translate_ty,
                               float domainwarp_amount_value,
                               bool allow_flow_cache,
                               bool apply_source_rgb_offsets,
                               bool use_custom_warp_unit,
                               const NoiseRGB& custom_warp_unit,
                               bool skip_halftone) -> NoiseRGB {
      NoiseRGB n = {0.0f, 0.0f, 0.0f};
      float post_sx = main_base_sx;
      float post_sy = main_base_sy;
      float post_sz = main_base_sz;
      const bool local_needs_warp_vector =
          !show_domainwarp_map && (domainwarp_amount_value > 0.0f);
      const bool local_domainwarp_active =
          show_domainwarp_map || local_needs_warp_vector;

      float main_sx = main_base_sx + main_translate_tx;
      float main_sy = main_base_sy + main_translate_ty;
      float main_sz = main_base_sz;
      // Keep a halftone anchor in pre-domainwarp noise space:
      // follows PRef/model, includes 3D pixelized coords, and avoids warp deformation.
      const float halftone_sx = main_sx;
      const float halftone_sy = main_sy;
      const float halftone_sz = main_sz;

      if (!local_domainwarp_active) {
        n = evaluate_output_no_warp(
            main_sx, main_sy, main_sz, pref_space, main_seed_value, use_time,
            apply_source_rgb_offsets);
        post_sx = main_sx;
        post_sy = main_sy;
        post_sz = main_sz;
      } else {
        const float domainwarp_sx = domainwarp_base_sx + domainwarp_translate_tx;
        const float domainwarp_sy = domainwarp_base_sy + domainwarp_translate_ty;
        const float domainwarp_sz = domainwarp_base_sz;
        const float local_domainwarp_relative_tx = domainwarp_translate_tx;
        const float local_domainwarp_relative_ty = domainwarp_translate_ty;
        auto sample_domainwarp_unit = [&](float qx, float qy, float qz) -> NoiseRGB {
          if (pref_space && domainwarp_pref_map_blur_radius > 1e-6f) {
            return evaluate_domainwarp_map_pref_blurred(
                qx, qy, qz, domainwarp_seed_value, use_time, domainwarp_pref_map_blur_radius);
          }
          return evaluate_domainwarp_map(
              qx, qy, qz, pref_space, domainwarp_seed_value, use_time);
        };
        if (show_domainwarp_map) {
          bool sampled_cache = false;
          if (!pref_space && domainwarp_flow_cache_view_ptr) {
            sampled_cache = sample_domainwarp_flow_cache(
                domainwarp_flow_cache_view_ptr,
                domainwarp_base_sx,
                domainwarp_base_sy,
                n);
          }
          if (!sampled_cache) {
            n = sample_domainwarp_unit(domainwarp_sx, domainwarp_sy, domainwarp_sz);
          }
          post_sx = domainwarp_sx;
          post_sy = domainwarp_sy;
          post_sz = domainwarp_sz;
        } else {
          NoiseRGB warp = {0.0f, 0.0f, 0.0f};
          const float effective_domainwarp_amount =
              pref_space ? (domainwarp_amount_value * 0.02f) : domainwarp_amount_value;
          if (local_needs_warp_vector) {
            if (use_custom_warp_unit) {
              warp = {
                  custom_warp_unit.r * effective_domainwarp_amount,
                  custom_warp_unit.g * effective_domainwarp_amount,
                  custom_warp_unit.b * effective_domainwarp_amount,
              };
            } else {
              bool sampled_cache = false;
              if (!pref_space && domainwarp_flow_cache_view_ptr) {
                NoiseRGB unit = {0.0f, 0.0f, 0.0f};
                sampled_cache = sample_domainwarp_flow_cache(
                    domainwarp_flow_cache_view_ptr,
                    domainwarp_base_sx,
                    domainwarp_base_sy,
                    unit);
                if (sampled_cache) {
                  warp = {
                      unit.r * effective_domainwarp_amount,
                      unit.g * effective_domainwarp_amount,
                      unit.b * effective_domainwarp_amount,
                  };
                }
              }
              if (!sampled_cache) {
                const NoiseRGB unit = sample_domainwarp_unit(
                    domainwarp_sx, domainwarp_sy, domainwarp_sz);
                warp = {
                    unit.r * effective_domainwarp_amount,
                    unit.g * effective_domainwarp_amount,
                    unit.b * effective_domainwarp_amount,
                };
              }
            }
            main_sx += warp.r;
            main_sy += warp.g;
            main_sz += warp.b;
          }

          n = evaluate_output_no_warp(
              main_sx, main_sy, main_sz, pref_space, main_seed_value, use_time,
              apply_source_rgb_offsets);
          post_sx = main_sx;
          post_sy = main_sy;
          post_sz = main_sz;
        }
      }

      if (apply_quantize_post) {
        n = apply_quantize_output_rgb(n, quantize_steps, pref_space);
      }
      if (apply_halftone_post && !skip_halftone) {
        NoiseRGB halftone_sample = n;
        const float halftone_mix =
            clamp_compat(static_cast<float>(halftone_strength_), 0.0f, 1.0f);
        const int halftone_mode = clamp_compat(
            halftone_mode_,
            static_cast<int>(HALFTONE_DOTS),
            static_cast<int>(HALFTONE_HATCHES));
        const bool sample_cell_center =
            (halftone_mode == HALFTONE_DOTS) &&
            (halftone_mix >= 0.999f) && !rgb_offset_over_post;
        if (sample_cell_center) {
          float sample_main_base_sx = main_base_sx;
          float sample_main_base_sy = main_base_sy;
          float sample_main_base_sz = main_base_sz;
          float sample_domainwarp_base_sx = domainwarp_base_sx;
          float sample_domainwarp_base_sy = domainwarp_base_sy;
          float sample_domainwarp_base_sz = domainwarp_base_sz;

          if (pref_space) {
            const float cell = std::max(1.0f, static_cast<float>(halftone_cell_size_));
            const float base_scale = std::max(1.0f, static_cast<float>(xsize_));
            const float step = std::max(1e-4f, (cell * 10.0f) / base_scale);
            auto snap_pref = [step](float v) -> float {
              return std::floor(v / step) * step + 0.5f * step;
            };
            sample_main_base_sx = snap_pref(sample_main_base_sx);
            sample_main_base_sy = snap_pref(sample_main_base_sy);
            sample_main_base_sz = snap_pref(sample_main_base_sz);
            sample_domainwarp_base_sx = snap_pref(sample_domainwarp_base_sx);
            sample_domainwarp_base_sy = snap_pref(sample_domainwarp_base_sy);
            sample_domainwarp_base_sz = snap_pref(sample_domainwarp_base_sz);
          } else {
            const float step = std::max(1.0f, static_cast<float>(halftone_cell_size_));
            auto snap_raster = [step](float v) -> float {
              return std::floor(v / step) * step + 0.5f * step;
            };
            sample_main_base_sx = snap_raster(sample_main_base_sx);
            sample_main_base_sy = snap_raster(sample_main_base_sy);
            sample_domainwarp_base_sx = snap_raster(sample_domainwarp_base_sx);
            sample_domainwarp_base_sy = snap_raster(sample_domainwarp_base_sy);
          }

          const bool cache_hit = halftone_cell_sample_cache.matches(
              sample_main_base_sx, sample_main_base_sy, sample_main_base_sz,
              sample_domainwarp_base_sx, sample_domainwarp_base_sy, sample_domainwarp_base_sz,
              main_seed_value, domainwarp_seed_value,
              main_translate_tx, main_translate_ty,
              domainwarp_translate_tx, domainwarp_translate_ty,
              domainwarp_amount_value,
              pref_space, use_time,
              allow_flow_cache, apply_source_rgb_offsets,
              use_custom_warp_unit, custom_warp_unit);

          if (cache_hit) {
            halftone_sample = halftone_cell_sample_cache.sample;
          } else {
            halftone_sample = self(
                self,
                sample_main_base_sx, sample_main_base_sy, sample_main_base_sz,
                sample_domainwarp_base_sx, sample_domainwarp_base_sy, sample_domainwarp_base_sz,
                screen_x, screen_y,
                pref_space, use_time,
                main_seed_value, domainwarp_seed_value,
                main_translate_tx, main_translate_ty,
                domainwarp_translate_tx, domainwarp_translate_ty,
                domainwarp_amount_value,
                allow_flow_cache,
                apply_source_rgb_offsets,
                use_custom_warp_unit,
                custom_warp_unit,
                true);

            halftone_cell_sample_cache.store(
                sample_main_base_sx, sample_main_base_sy, sample_main_base_sz,
                sample_domainwarp_base_sx, sample_domainwarp_base_sy, sample_domainwarp_base_sz,
                main_seed_value, domainwarp_seed_value,
                main_translate_tx, main_translate_ty,
                domainwarp_translate_tx, domainwarp_translate_ty,
                domainwarp_amount_value,
                pref_space, use_time,
                allow_flow_cache, apply_source_rgb_offsets,
                use_custom_warp_unit, custom_warp_unit,
                halftone_sample);
          }
        }

        if (pref_space) {
          n = apply_halftone_output_rgb(
              n, halftone_sample, screen_x, screen_y, halftone_sx, halftone_sy, halftone_sz,
              true);
        } else {
          n = apply_halftone_output_rgb(
              n, halftone_sample, screen_x, screen_y, post_sx, post_sy, post_sz, false);
        }
      }
      if (!output_uses_blue_channel(pref_space)) {
        n.b = 0.0f;
      }
      return n;
    };

    auto evaluate_pixel = [&](int source_x, int source_y, int render_x, int render_y,
                              const float* sample_pref_r,
                              const float* sample_pref_g,
                              const float* sample_pref_b,
                              const float* sample_pref_a,
                              const float* sample_mask_r,
                              const float* sample_mask_a,
                              const float* sample_warp_r,
                              const float* sample_warp_g,
                              const float* sample_warp_b) -> NoiseRGBA {
      NoiseRGB n = {0.0f, 0.0f, 0.0f};
      bool sample_valid = false;
      bool pref_space = false;
      bool use_time = false;
      bool use_custom_warp_unit = false;
      NoiseRGB custom_warp_unit = {0.0f, 0.0f, 0.0f};
      float main_base_sx = 0.0f;
      float main_base_sy = 0.0f;
      float main_base_sz = 0.0f;
      float domainwarp_base_sx = 0.0f;
      float domainwarp_base_sy = 0.0f;
      float domainwarp_base_sz = 0.0f;

      float pref_alpha_center = 1.0f;
      if (use_pref) {
        const int pref_idx_center = source_x;
        if (keep_alpha_available && sample_pref_a) {
          pref_alpha_center = clamp01(sample_pref_a[pref_idx_center]);
        }

        const float raw_sx = sample_pref_r ? sample_pref_r[pref_idx_center] : 0.0f;
        const float raw_sy = sample_pref_g ? sample_pref_g[pref_idx_center] : 0.0f;
        const float raw_sz = sample_pref_b ? sample_pref_b[pref_idx_center] : 0.0f;
        {
          main_base_sx = raw_sx;
          main_base_sy = raw_sy;
          main_base_sz = raw_sz;
          domainwarp_base_sx = raw_sx;
          domainwarp_base_sy = raw_sy;
          domainwarp_base_sz = raw_sz;
          if (pref_center_valid) {
            main_base_sx -= pref_center[0];
            main_base_sy -= pref_center[1];
            main_base_sz -= pref_center[2];
          }
          if (domainwarp_pref_center_valid) {
            domainwarp_base_sx -= domainwarp_pref_center[0];
            domainwarp_base_sy -= domainwarp_pref_center[1];
            domainwarp_base_sz -= domainwarp_pref_center[2];
          }
          if (pixel_size_ > 0.0 && !show_domainwarp_map) {
            apply_pref_pixelize_coords(main_base_sx, main_base_sy, main_base_sz);
            apply_pref_pixelize_coords(
                domainwarp_base_sx, domainwarp_base_sy, domainwarp_base_sz);
          }
          sample_valid = true;
          pref_space = true;
          use_time = true;
        }
      } else {
        main_base_sx = static_cast<float>(source_x);
        main_base_sy = static_cast<float>(source_y);
        main_base_sz = 0.0f;
        domainwarp_base_sx = main_base_sx;
        domainwarp_base_sy = main_base_sy;
        domainwarp_base_sz = main_base_sz;
        sample_valid = true;
        use_time = use_time_2d;
      }

      if (sample_valid && use_warp) {
        const float wr = sample_warp_r ? sample_warp_r[source_x] : 0.0f;
        const float wg = sample_warp_g ? sample_warp_g[source_x] : 0.0f;
        const float wb = sample_warp_b ? sample_warp_b[source_x] : 0.0f;
        custom_warp_unit = {
            clamp_compat(wr, -1.0f, 1.0f),
            clamp_compat(wg, -1.0f, 1.0f),
            pref_space ? clamp_compat(wb, -1.0f, 1.0f) : 0.0f,
        };
        use_custom_warp_unit = true;
      }

      float mask_value = 1.0f;
      if (use_mask) {
        if (mask_has_alpha && sample_mask_a) {
          mask_value = sample_mask_a[render_x];
        } else if (sample_mask_r) {
          mask_value = sample_mask_r[render_x];
        } else {
          mask_value = 0.0f;
        }
        mask_value = clamp01(mask_value);
      }

      float alpha_out = 0.0f;
      if (sample_valid) {
        if (!rgb_offset_over_post) {
          n = sample_pipeline(
              sample_pipeline,
              main_base_sx, main_base_sy, main_base_sz,
              domainwarp_base_sx, domainwarp_base_sy, domainwarp_base_sz,
              render_x, render_y,
              pref_space, use_time,
              seed, domainwarp_seed,
              main_applied_tx, main_applied_ty,
              domain_applied_tx, domain_applied_ty,
              domainwarp_amount,
              true,
              true,
              use_custom_warp_unit,
              custom_warp_unit,
              false);
        } else {
          auto evaluate_shifted_channel = [&](int channel_index) -> NoiseRGB {
            const int idx = clamp_compat(channel_index, 0, 2);
            const float channel_dt = main_rgb_dt_[idx];
            // RGB time is applied after domain warp deformation:
            // keep warp-map sampling shared, shift only the final noise sampling coords.
            float shifted_main_sx = main_base_sx + main_rgb_dx_[idx];
            float shifted_main_sy = main_base_sy + main_rgb_dy_[idx];
            float shifted_main_sz = main_base_sz;
            float shifted_domainwarp_sx = domainwarp_base_sx;
            float shifted_domainwarp_sy = domainwarp_base_sy;
            float shifted_domainwarp_sz = domainwarp_base_sz;
            const float shifted_main_seed = seed + channel_dt;
            const float shifted_domainwarp_seed = domainwarp_seed + channel_dt;
            const int shifted_screen_x = static_cast<int>(std::lround(
                static_cast<double>(render_x) + static_cast<double>(main_rgb_dx_[idx])));
            const int shifted_screen_y = static_cast<int>(std::lround(
                static_cast<double>(render_y) + static_cast<double>(main_rgb_dy_[idx])));
            return sample_pipeline(
                sample_pipeline,
                shifted_main_sx, shifted_main_sy, shifted_main_sz,
                shifted_domainwarp_sx, shifted_domainwarp_sy, shifted_domainwarp_sz,
                shifted_screen_x, shifted_screen_y,
                pref_space, use_time,
                shifted_main_seed, shifted_domainwarp_seed,
                main_applied_tx, main_applied_ty,
                domain_applied_tx, domain_applied_ty,
                domainwarp_amount,
                false,
                false,
                use_custom_warp_unit,
                custom_warp_unit,
                false);
          };

          const NoiseRGB nr = evaluate_shifted_channel(0);
          const NoiseRGB ng = evaluate_shifted_channel(1);
          const NoiseRGB nb = evaluate_shifted_channel(2);
          n = {nr.r, ng.g, nb.b};
        }

        n.r *= mask_value;
        n.g *= mask_value;
        n.b *= mask_value;
        const NoiseRGB alpha_reference = n;
        if (keep_alpha_enabled && keep_alpha_available) {
          // Keep Alpha only applies as a final RGB multiply; it must not affect
          // the generated noise alpha or the sampling coordinates.
          n.r *= pref_alpha_center;
          n.g *= pref_alpha_center;
          n.b *= pref_alpha_center;
        }
        if (alpha_requested) {
          alpha_out = output_alpha_from_sample(alpha_reference);
        }
      }

      return {n.r, n.g, n.b, alpha_out};
    };

    const int clamped_y = clamp_compat(y, out_y_min, out_y_max);

    const bool post_pixel_enabled = (pixel_size_ > 0.0) && !show_domainwarp_map && !use_pref;

    // Hot path in production: no PRef, no mask, no domain-warp, no post effects.
    // Avoid temporary buffers and per-pixel branch fanout for this case.
    if (!use_pref && !use_mask && !domainwarp_active &&
        quantize_levels_ <= 0.0 && !halftone_enable_ &&
        !post_pixel_enabled) {
      const float fast_sy = static_cast<float>(clamped_y) + main_applied_ty;
      if (uniform_) {
        const NoiseRGB uniform_sample = evaluate_output_no_warp(
            static_cast<float>(x) + main_applied_tx, fast_sy, 0.0f, false, seed, use_time_2d);
        for (int px = x; px < r; ++px) {
          if (out_channels[0]) {
            out_channels[0][px] = uniform_sample.r;
          }
          if (out_channels[1]) {
            out_channels[1][px] = uniform_sample.g;
          }
          if (out_channels[2]) {
            out_channels[2][px] = uniform_sample.b;
          }
          if (alpha_requested) {
            out_channels[3][px] = output_alpha_from_sample(uniform_sample);
          }
        }
        return;
      }
      if (projection_2d_ == PROJECTION_PLANAR &&
          (output_mode_ == OUTPUT_VECTORS || output_mode_ == OUTPUT_STMAP)) {
        const float center_x = static_cast<float>(center_[0]);
        const float center_y = static_cast<float>(center_[1]);
        const float translate_x = static_cast<float>(pref_translate_[0]);
        const float translate_y = static_cast<float>(pref_translate_[1]);
        const float local_x0 = static_cast<float>(x) + main_applied_tx - center_x - translate_x;
        const float local_y = fast_sy - center_y - translate_y;
        const Vector3 p0 = invmatrix_.transform(Vector3(local_x0, local_y, 0.0f));
        const Vector3 p1 = invmatrix_.transform(Vector3(local_x0 + 1.0f, local_y, 0.0f));
        const float step_x = p1.x - p0.x;
        const float step_y = p1.y - p0.y;
        const float step_z = p1.z - p0.z;
        const float vector_mul = std::max(0.0f, static_cast<float>(output_vectors_multiply_));
        const float st_mul = std::max(0.0f, static_cast<float>(output_stmap_multiply_));

        for (int px = x; px < r; ++px) {
          const float ofs = static_cast<float>(px - x);
          const float vx = p0.x + step_x * ofs;
          const float vy = p0.y + step_y * ofs;
          const float vz = p0.z + step_z * ofs;
          NoiseRGB n = {0.0f, 0.0f, 0.0f};
          if (output_mode_ == OUTPUT_VECTORS) {
            const NoiseRGB unit = evaluate_noise_vector_map_with_offset_from_point(
                vx, vy, vz, seed, false, use_time_2d, kFixedUvOffset, output_vectors_mode_);
            n.r = unit.r * vector_mul;
            n.g = unit.g * vector_mul;
            n.b = unit.b * vector_mul;
            if (output_invert_) {
              n.r = -n.r;
              n.g = -n.g;
              n.b = -n.b;
            }
          } else {
            const NoiseRGB unit = evaluate_noise_vector_map_with_offset_from_point(
                vx, vy, vz, seed, false, use_time_2d, kFixedUvOffset, output_stmap_mode_);
            n.r = (0.5f + 0.5f * unit.r) * st_mul;
            n.g = (0.5f + 0.5f * unit.g) * st_mul;
            n.b = 0.0f;
            if (output_invert_) {
              n.r = 1.0f - n.r;
              n.g = 1.0f - n.g;
              n.b = 1.0f - n.b;
            }
          }
          if (out_channels[0]) {
            out_channels[0][px] = n.r;
          }
          if (out_channels[1]) {
            out_channels[1][px] = n.g;
          }
          if (out_channels[2]) {
            out_channels[2][px] = n.b;
          }
          if (alpha_requested) {
            out_channels[3][px] = output_alpha_from_sample(n);
          }
        }
        return;
      }
      if (projection_2d_ == PROJECTION_PLANAR &&
          output_mode_ == OUTPUT_NOISE &&
          type_ != NOISE_VORONOI) {
        const float center_x = static_cast<float>(center_[0]);
        const float center_y = static_cast<float>(center_[1]);
        const float translate_x = static_cast<float>(pref_translate_[0]);
        const float translate_y = static_cast<float>(pref_translate_[1]);
        const float local_x0 = static_cast<float>(x) + main_applied_tx - center_x - translate_x;
        const float local_y = fast_sy - center_y - translate_y;
        const Vector3 p0 = invmatrix_.transform(Vector3(local_x0, local_y, 0.0f));
        const Vector3 p1 = invmatrix_.transform(Vector3(local_x0 + 1.0f, local_y, 0.0f));
        const float step_x = p1.x - p0.x;
        const float step_y = p1.y - p0.y;
        const float step_z = p1.z - p0.z;
        const bool rgb_offsets_enabled = main_rgb_has_offsets_;
        const bool rgb_time_offset_on_seed = true;
        const auto rgb_offsets_cache =
            std::atomic_load_explicit(&main_rgb_noise_offsets_cache_, std::memory_order_acquire);
        Vector3 rgb_delta[3];
        float rgb_time[3] = {seed, seed, seed};
        if (rgb_offsets_enabled) {
          for (int c = 0; c < 3; ++c) {
            rgb_delta[c] = rgb_offsets_cache
                               ? rgb_offsets_cache->delta[c]
                               : invmatrix_.transform(
                                     Vector3(main_rgb_dx_[c], main_rgb_dy_[c], 0.0f));
            rgb_time[c] = seed + (rgb_time_offset_on_seed
                                      ? (rgb_offsets_cache
                                             ? rgb_offsets_cache->time_offset[c]
                                             : main_rgb_dt_[c])
                                      : 0.0f);
          }
        }

        const float mid = finalize_noise_value(0.5f);
        for (int px = x; px < r; ++px) {
          const float ofs = static_cast<float>(px - x);
          const float vx = p0.x + step_x * ofs;
          const float vy = p0.y + step_y * ofs;
          const float vz = p0.z + step_z * ofs;

          NoiseRGB n = {mid, mid, mid};
          if (!uniform_) {
            if (!rgb_offsets_enabled) {
              const float signed_v = evaluate_noise_scalar_from_point(
                  vx, vy, vz, seed, use_time_2d, 0.0f);
              const float base01 = clamp01((signed_v + 1.0f) * 0.5f);
              const float value = finalize_noise_value(base01);
              n = {value, value, value};
            } else {
              auto eval_channel = [&](int c) -> float {
                const float signed_v = evaluate_noise_scalar_from_point(
                    vx + rgb_delta[c].x,
                    vy + rgb_delta[c].y,
                    vz + rgb_delta[c].z,
                    rgb_time[c], use_time_2d, 0.0f);
                const float base01 = clamp01((signed_v + 1.0f) * 0.5f);
                return finalize_noise_value(base01);
              };
              n = {eval_channel(0), eval_channel(1), eval_channel(2)};
            }
          }

          if (output_invert_) {
            n.r = clamp01(1.0f - n.r);
            n.g = clamp01(1.0f - n.g);
            n.b = clamp01(1.0f - n.b);
          }

          if (out_channels[0]) {
            out_channels[0][px] = n.r;
          }
          if (out_channels[1]) {
            out_channels[1][px] = n.g;
          }
          if (out_channels[2]) {
            out_channels[2][px] = n.b;
          }
          if (alpha_requested) {
            out_channels[3][px] = output_alpha_from_sample(n);
          }
        }
        return;
      }
      if (projection_2d_ != PROJECTION_PLANAR &&
          output_mode_ == OUTPUT_NOISE &&
          type_ != NOISE_VORONOI &&
          !main_rgb_has_offsets_) {
        static const float kPi = 3.14159265358979323846f;
        static const float kTwoPi = 6.28318530717958647692f;
        const bool is_pattern = (sanitize_noise_type(type_) == NOISE_PATTERN);
        const auto lon_cache = ensure_projection_lon_cache_for_format();
        const Format& f = format();
        const int min_x_i = f.x();
        const int max_x_i = f.r();
        const int width_i = std::max(1, max_x_i - min_x_i);
        const float min_x = static_cast<float>(min_x_i);
        const float max_x = static_cast<float>(max_x_i);
        const float min_y = static_cast<float>(f.y());
        const float width = static_cast<float>(width_i);
        const float height = std::max(1.0f, static_cast<float>(f.t() - f.y()));
        const float tx = static_cast<float>(pref_translate_[0]);
        const float ty = static_cast<float>(pref_translate_[1]);
        const float tz = static_cast<float>(pref_translate_[2]);
        const float lon_shift = (main_applied_tx / width) * kTwoPi;
        const float cos_shift = std::cos(lon_shift);
        const float sin_shift = std::sin(lon_shift);
        const float* lon_cos = lon_cache.cos_data;
        const float* lon_sin = lon_cache.sin_data;

        const float v01 = clamp_compat((fast_sy - min_y) / height, 0.0f, 1.0f);
        float proj_row_y = 0.0f;
        float row_radius = 1.0f;
        if (projection_2d_ == PROJECTION_SPHERICAL) {
          const float lat = (0.5f - v01) * kPi;
          row_radius = std::cos(lat);
          proj_row_y = std::sin(lat);
        } else {
          row_radius = 1.0f;
          proj_row_y = (0.5f - v01) * 2.0f;
        }
        const float row_y_translated = proj_row_y - ty;
        const bool no_x_shift = std::fabs(main_applied_tx) <= 1e-6f;

        auto write_non_planar_noise = [&](int px, float cx, float sz_lon) {
          const Vector3 p = invmatrix_.transform(
              Vector3(row_radius * cx - tx,
                      row_y_translated,
                      row_radius * sz_lon - tz));

          float pz = is_pattern ? 0.0f : p.z;
          const float signed_v = evaluate_noise_scalar_from_point(
              p.x, p.y, pz, seed, use_time_2d, 0.0f);
          const float base01 = clamp01((signed_v + 1.0f) * 0.5f);
          float value = finalize_noise_value(base01);
          if (output_invert_) {
            value = clamp01(1.0f - value);
          }
          if (out_channels[0]) {
            out_channels[0][px] = value;
          }
          if (out_channels[1]) {
            out_channels[1][px] = value;
          }
          if (out_channels[2]) {
            out_channels[2][px] = value;
          }
          if (alpha_requested) {
            out_channels[3][px] = value;
          }
        };

        if (no_x_shift && lon_cos && lon_sin) {
          for (int px = x; px < r; ++px) {
            const int base_idx = px - min_x_i;
            float cx = -1.0f;
            float sz_lon = 0.0f;
            if (base_idx > 0 && base_idx < width_i) {
              cx = lon_cos[base_idx];
              sz_lon = lon_sin[base_idx];
            }
            write_non_planar_noise(px, cx, sz_lon);
          }
        } else {
          for (int px = x; px < r; ++px) {
            const float sx = static_cast<float>(px) + main_applied_tx;
            float cx = -1.0f;
            float sz_lon = 0.0f;
            if (sx >= max_x) {
              cx = -1.0f;
              sz_lon = 0.0f;
            } else if (sx <= min_x) {
              cx = -1.0f;
              sz_lon = 0.0f;
            } else {
              const int base_idx = px - min_x_i;
              if (lon_cos && lon_sin && base_idx >= 0 && base_idx <= width_i) {
                const float bx = lon_cos[base_idx];
                const float by = lon_sin[base_idx];
                cx = bx * cos_shift - by * sin_shift;
                sz_lon = by * cos_shift + bx * sin_shift;
              } else {
                const float u = clamp_compat((sx - min_x) / width, 0.0f, 1.0f);
                const float lon = (u - 0.5f) * kTwoPi;
                cx = std::cos(lon);
                sz_lon = std::sin(lon);
              }
            }
            write_non_planar_noise(px, cx, sz_lon);
          }
        }
        return;
      }
      for (int px = x; px < r; ++px) {
        const float fast_sx = static_cast<float>(px) + main_applied_tx;
        const NoiseRGB n = evaluate_output_no_warp(
            fast_sx, fast_sy, 0.0f, false, seed, use_time_2d);
        if (out_channels[0]) {
          out_channels[0][px] = n.r;
        }
        if (out_channels[1]) {
          out_channels[1][px] = n.g;
        }
        if (out_channels[2]) {
          out_channels[2][px] = n.b;
        }
        if (alpha_requested) {
          out_channels[3][px] = output_alpha_from_sample(n);
        }
      }
      return;
    }

    const int eval_y = post_pixel_enabled
                           ? clamp_compat(
                                 snap_raster_coord_for_post_pixel(
                                     clamped_y, static_cast<float>(pixel_size_)),
                                 out_y_min, out_y_max)
                           : clamped_y;

    Row pref_row_local(x, r);
    Row mask_row_local(x, r);
    Row warp_row_local(x, r);
    const float* sample_pref_r = nullptr;
    const float* sample_pref_g = nullptr;
    const float* sample_pref_b = nullptr;
    const float* sample_pref_a = nullptr;
    const float* sample_mask_r = nullptr;
    const float* sample_mask_a = nullptr;
    const float* sample_warp_r = nullptr;
    const float* sample_warp_g = nullptr;
    const float* sample_warp_b = nullptr;
    ChannelSet pref_sample_mask;

    if (use_pref) {
      pref_sample_mask += pref_chan_r;
      pref_sample_mask += pref_chan_g;
      pref_sample_mask += pref_chan_b;
      if (keep_alpha_available) {
        pref_sample_mask += keep_alpha_chan;
      }
      const Format& pf = input0().format();
      const int pref_eval_y = clamp_compat(eval_y, pf.y(), pf.t() - 1);
      pref_row_local.get(input0(), pref_eval_y, x, r, pref_sample_mask);
      sample_pref_r = pref_row_local[pref_chan_r];
      sample_pref_g = pref_row_local[pref_chan_g];
      sample_pref_b = pref_row_local[pref_chan_b];
      sample_pref_a = keep_alpha_available ? pref_row_local[keep_alpha_chan] : nullptr;
    }
    if (mask_iop) {
      mask_row_local.get(*mask_iop, eval_y, x, r, Mask_RGBA);
      sample_mask_r = mask_row_local[Chan_Red];
      sample_mask_a = mask_row_local[Chan_Alpha];
    }
    if (warp_iop) {
      warp_row_local.get(*warp_iop, eval_y, x, r, Mask_RGBA);
      sample_warp_r = warp_row_local[Chan_Red];
      sample_warp_g = warp_row_local[Chan_Green];
      sample_warp_b = warp_row_local[Chan_Blue];
    }

    auto write_sample_to_outputs = [&](int px, const NoiseRGBA& s) {
      if (out_channels[0]) {
        out_channels[0][px] = s.r;
      }
      if (out_channels[1]) {
        out_channels[1][px] = s.g;
      }
      if (out_channels[2]) {
        out_channels[2][px] = s.b;
      }
      if (out_channels[3]) {
        out_channels[3][px] = s.a;
      }
    };

    if (post_pixel_enabled) {
      const float step = std::max(1.0f, static_cast<float>(pixel_size_));
      int last_src_x = 0;
      NoiseRGBA last_sample = {};
      bool has_last_sample = false;

      for (int px = x; px < r; ++px) {
        const int src_x = clamp_compat(
            snap_raster_coord_for_post_pixel(px, step), x, r - 1);
        if (apply_halftone_post) {
          const NoiseRGBA s = evaluate_pixel(
              src_x, eval_y, px, clamped_y,
              sample_pref_r, sample_pref_g, sample_pref_b, sample_pref_a,
              sample_mask_r, sample_mask_a,
              sample_warp_r, sample_warp_g, sample_warp_b);
          write_sample_to_outputs(px, s);
          continue;
        } else if (!has_last_sample || src_x != last_src_x) {
          last_sample = evaluate_pixel(
              src_x, eval_y, src_x, eval_y,
              sample_pref_r, sample_pref_g, sample_pref_b, sample_pref_a,
              sample_mask_r, sample_mask_a,
              sample_warp_r, sample_warp_g, sample_warp_b);
          last_src_x = src_x;
          has_last_sample = true;
        }

        const NoiseRGBA& s = last_sample;
        write_sample_to_outputs(px, s);
      }
    } else {
      for (int px = x; px < r; ++px) {
        const NoiseRGBA s = evaluate_pixel(
            px, eval_y, px, eval_y,
            sample_pref_r, sample_pref_g, sample_pref_b, sample_pref_a,
            sample_mask_r, sample_mask_a,
            sample_warp_r, sample_warp_g, sample_warp_b);
        write_sample_to_outputs(px, s);
      }
    }
  }

  void engine(int y, int x, int r, ChannelMask channels, Row& row) override {
    engine_cpu(y, x, r, channels, row);
  }

  void append(Hash& hash) override {
    Iop::append(hash);
    auto append_bool = [&](bool value) {
      hash.append(value ? 1 : 0);
    };
    auto append_float = [&](double value) {
      hash.append(static_cast<float>(value));
    };
    auto append_float_array = [&](const float* values, int count) {
      for (int i = 0; i < count; ++i) {
        hash.append(values[i]);
      }
    };
    auto append_double_array = [&](const double* values, int count) {
      for (int i = 0; i < count; ++i) {
        hash.append(static_cast<float>(values[i]));
      }
    };
    auto append_channel_array = [&](const Channel* values, int count) {
      for (int i = 0; i < count; ++i) {
        hash.append(static_cast<unsigned int>(values[i]));
      }
    };

    hash.append(0x544E4F49534532ull); // Stable template hash salt.
    hash.append(static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(this)));
    hash.append(node_name());
    hash.append(outputContext().view());

    const Format fmt = format();
    hash.append(fmt.x());
    hash.append(fmt.y());
    hash.append(fmt.r());
    hash.append(fmt.t());

    hash.append(type_);
    hash.append(type_menu_);
    hash.append(fractal_mode_);
    append_float(xsize_);
    append_float(ysize_);
    append_float(zsize_);
    append_float(zspeed_);
    hash.append(octaves_);
    hash.append(voronoi_octaves_);
    hash.append(pattern_octaves_);
    hash.append(real_octaves_);
    hash.append(voronoi_metric_);
    hash.append(voronoi_shape_mode_);
    hash.append(voronoi_color_seed_);
    append_float(voronoi_color_offset_);
    append_float(voronoi_randomness_);
    append_float(voronoi_saturation_);
    append_float(voronoi_minkowski_exp_);
    hash.append(pattern_type_mode_);
    hash.append(pattern_shape_mode_);
    hash.append(pattern_segment_count_);
    hash.append(pattern_twist_);
    append_float(lacunarity_);
    append_float(gain_);
    hash.append(gamma_);
    append_float(quantize_levels_);
    append_float(pixel_size_);
    append_bool(halftone_enable_);
    append_float(halftone_cell_size_);
    append_float(halftone_strength_);
    hash.append(halftone_mode_);
    append_float(halftone_smoothness_);
    append_bool(halftone_invert_);
    append_float(halftone_hatch_angle_);
    hash.append(halftone_hatch_count_);
    hash.append(projection_2d_);
    hash.append(output_mode_);
    append_bool(output_invert_);
    hash.append(output_vectors_mode_);
    append_float(output_vectors_multiply_);
    hash.append(output_stmap_mode_);
    append_float(output_stmap_multiply_);
    append_float(output_normal_strength_);
    hash.append(rgb_mode_);
    hash.append(chroma_mode_);
    append_bool(rgb_invert_chroma_);
    append_float(rgb_size_);
    append_float(rgb_angle_);
    append_float(rgb_time_offset_);

    append_float(domainwarp_amount_);
    append_float(domainwarp_zspeed_);
    hash.append(domainwarp_vector_mode_);
    append_float(domainwarp_map_blur_);
    append_float(domainwarp_divergence_mix_);
    append_bool(domainwarp_show_map_);
    hash.append(domainwarp_type_);
    hash.append(domainwarp_type_menu_);
    hash.append(domainwarp_fractal_mode_);
    append_float(domainwarp_xsize_);
    append_float(domainwarp_ysize_);
    append_float(domainwarp_zsize_);
    hash.append(domainwarp_octaves_);
    hash.append(domainwarp_voronoi_octaves_);
    hash.append(domainwarp_pattern_octaves_);
    hash.append(domainwarp_real_octaves_);
    append_float(domainwarp_lacunarity_);
    append_float(domainwarp_gain_);
    hash.append(domainwarp_voronoi_metric_);
    hash.append(domainwarp_voronoi_shape_mode_);
    append_float(domainwarp_voronoi_minkowski_exp_);
    append_float(domainwarp_voronoi_randomness_);
    hash.append(domainwarp_pattern_type_mode_);
    hash.append(domainwarp_pattern_shape_mode_);
    hash.append(domainwarp_pattern_segment_count_);
    hash.append(domainwarp_pattern_twist_);

    append_channel_array(output_channels_, 4);
    append_channel_array(pref_channels_, 4);
    append_bool(pref_keep_alpha_);
    append_double_array(center_, 2);
    append_float(translate_speed_);
    append_float(translate_angle_);
    append_float(domainwarp_translate_speed_);
    append_float(domainwarp_translate_angle_);
    append_double_array(rotate2d_xy_, 2);
    append_double_array(pref_translate_, 3);
    append_double_array(pref_scale_, 3);
    append_double_array(pref_skew_, 3);
    append_double_array(pref_rotate_, 3);
    append_double_array(domainwarp_center_, 2);
    append_double_array(domainwarp_rotate2d_xy_, 2);
    append_double_array(domainwarp_translate_, 3);
    append_double_array(domainwarp_scale_, 3);
    append_double_array(domainwarp_skew_, 3);
    append_double_array(domainwarp_rotate_, 3);
    append_float_array(pref_center_latched_, 3);
    append_bool(pref_center_latched_valid_);
    append_float_array(domainwarp_pref_center_latched_, 3);
    append_bool(domainwarp_pref_center_latched_valid_);

    append_bool(input(0) != nullptr);
    append_bool(input(1) != nullptr);
    append_bool(input(2) != nullptr);

    // Be conservative for interactive viewer playback. PRef/mask/warp inputs
    // can be frame-varying even when TVectorBlur's own animation knobs are static.
    hash.append(outputContext().frame());
#if defined(kDDImageVersionMajorNum) && (kDDImageVersionMajorNum >= 14)
    enableVaryingOutputHash();
#endif
  }

  const char* Class() const override { return CLASS; }
  const char* node_help() const override { return HELP; }
  int optional_input() const override { return 2; }
  int minimum_inputs() const override { return 3; }
  int maximum_inputs() const override { return 3; }
  const char* input_label(int input, char*) const override {
    if (input == 0) {
      return "PRef";
    }
    if (input == 1) {
      return "Warp";
    }
    if (input == 2) {
      return "mask";
    }
    return nullptr;
  }

