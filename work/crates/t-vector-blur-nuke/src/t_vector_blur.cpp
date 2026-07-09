static const char* const CLASS = "TVectorBlur";
static const char* const HELP =
    "Procedural noise source with fBm and turbulence modes.";

#include <cmath>
#include <cstddef>
#include <string>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>
#if defined(__SSE__)
#include <xmmintrin.h>
#endif


#include "DDImage/Channel.h"
#include "DDImage/DDMath.h"
#include "DDImage/Format.h"
#include "DDImage/Iop.h"
#include "DDImage/Knob.h"
#include "DDImage/Knobs.h"
#include "DDImage/Matrix4.h"
#include "DDImage/Row.h"
#include "DDImage/Thread.h"
#include "DDImage/Vector3.h"
#include "DDImage/noise.h"

using namespace DD::Image;


enum NoiseType {
  NOISE_PERLIN = 0,
  NOISE_SIMPLEX = 1,
  NOISE_VORONOI = 3,
  NOISE_PATTERN = 4
};
enum FractalMode { FRACTAL_NONE, FRACTAL_FBM, FRACTAL_BILLOW, FRACTAL_RIDGED };
enum Projection2DMode { PROJECTION_PLANAR, PROJECTION_SPHERICAL, PROJECTION_CYLINDRICAL };
enum OutputMode { OUTPUT_NOISE, OUTPUT_VECTORS, OUTPUT_STMAP, OUTPUT_NORMAL };
enum RgbTypeMode { RGB_MODE_OFFSET, RGB_MODE_TIME, RGB_MODE_BOTH };
enum ChromaMode { CHROMA_RED_BLUE, CHROMA_RED_GREEN, CHROMA_BLUE_GREEN };
enum DomainWarpVectorMode {
  DW_VECTOR_FIELD,
  DW_VECTOR_DERIVATIVE,
  DW_VECTOR_RADIAL_CENTER,
  DW_VECTOR_ORBIT_CENTER
};
enum VoronoiMetric {
  VORONOI_EUCLIDEAN,
  VORONOI_MANHATTAN,
  VORONOI_CHEBYSHEV,
  VORONOI_MINKOWSKI
};
enum VoronoiShapeMode {
  VORONOI_SHAPE_VORONOI,
  VORONOI_SHAPE_CELLS,
  VORONOI_SHAPE_CRYSTALS,
  VORONOI_SHAPE_CRACKS,
  VORONOI_SHAPE_WEB,
  VORONOI_SHAPE_BUBBLES
};
enum PatternTypeMode {
  PATTERN_CONCENTRIC = 0,
  PATTERN_LINEAR = 1,
  PATTERN_RADIAL = 2,
  PATTERN_SPIRAL = 3  // Legacy value kept for backward compatibility.
};
enum PatternShapeMode {
  PATTERN_SHAPE_ROUND = 0,
  PATTERN_SHAPE_SQUARE = 1,   // Legacy value kept for backward compatibility.
  PATTERN_SHAPE_DIAMOND = 2   // Legacy value kept for backward compatibility.
};
enum HalftoneMode {
  HALFTONE_DOTS = 0,
  HALFTONE_HATCHES = 1
};
const char* const kNoiseTypes[] = {
    "Perlin",
    "Simplex",
    "Voronoi",
    "Pattern",
    nullptr,
};
const char* const kFractalModes[] = {
    "None",
    "fBm",
    "Billow",
    "Ridged",
    nullptr,
};
const char* const kProjectionModes[] = {
    "Planar",
    "Spherical",
    "Cylindrical",
    nullptr,
};
const char* const kOutputModes[] = {
    "Noise",
    "Vectors",
    "STMap",
    "Normal Map",
    nullptr,
};
const char* const kRgbTypeModes[] = {
    "Offset",
    "Time",
    "Both",
    nullptr,
};
const char* const kChromaModes[] = {
    "Red Blue",
    "Red Green",
    "Blue Green",
    nullptr,
};
const char* const kDomainWarpVectorModes[] = {
    "Field",
    "Spiral Noise",
    "Radial Center",
    "Orbit Center",
    nullptr,
};
const char* const kVoronoiMetrics[] = {
    "Euclidean",
    "Manhattan",
    "Chebyshev",
    "Minkowski",
    nullptr,
};
const char* const kVoronoiShapeModes[] = {
    "Voronoi",
    "Cells",
    "Crystals",
    "Cracks",
    "Web",
    "Bubbles",
    nullptr,
};
const char* const kPatternTypeModes[] = {
    "Concentric",
    "Linear",
    "Radial",
    nullptr,
};
const char* const kPatternShapeModes[] = {
    "Round",
    nullptr,
};
const char* const kHalftoneModes[] = {
    "Dots",
    "Hatches",
    nullptr,
};

class TVectorBlurBase : public Iop {
  int type_ = NOISE_PERLIN;
  int type_menu_ = 0;
  int fractal_mode_ = FRACTAL_FBM;
  double xsize_ = 350.0;
  double ysize_ = 350.0;
  double zsize_ = 0.0;
  double zspeed_ = 0.0;
  int octaves_ = 10;
  int voronoi_octaves_ = 1;
  int pattern_octaves_ = 1;
  int real_octaves_ = 10;
  int voronoi_metric_ = VORONOI_EUCLIDEAN;
  int voronoi_shape_mode_ = VORONOI_SHAPE_VORONOI;
  int voronoi_color_seed_ = 750;
  double voronoi_color_offset_ = 0.0;
  double voronoi_randomness_ = 1.0;
  double voronoi_saturation_ = 1.0;
  double voronoi_minkowski_exp_ = 1.0;
  int pattern_type_mode_ = PATTERN_CONCENTRIC;
  int pattern_shape_mode_ = PATTERN_SHAPE_ROUND;
  int pattern_segment_count_ = 10;
  int pattern_twist_ = 0;
  double lacunarity_ = 2.0;
  double gain_ = 0.5;
  float gamma_ = 0.5f;
  double quantize_levels_ = 0.0;
  double pixel_size_ = 0.0;
  bool halftone_enable_ = false;
  double halftone_cell_size_ = 8.0;
  double halftone_strength_ = 1.0;
  int halftone_mode_ = HALFTONE_DOTS;
  double halftone_smoothness_ = 0.0;
  bool halftone_invert_ = false;
  double halftone_hatch_angle_ = 45.0;
  int halftone_hatch_count_ = 2;
  int projection_2d_ = PROJECTION_PLANAR;
  int output_mode_ = OUTPUT_NOISE;
  bool output_invert_ = false;
  int output_vectors_mode_ = DW_VECTOR_FIELD;
  double output_vectors_multiply_ = 1.0;
  int output_stmap_mode_ = DW_VECTOR_FIELD;
  double output_stmap_multiply_ = 1.0;
  double output_normal_strength_ = 1.0;
  int rgb_mode_ = RGB_MODE_OFFSET;
  int chroma_mode_ = CHROMA_RED_BLUE;
  bool rgb_invert_chroma_ = false;
  double rgb_size_ = 0.0;
  double rgb_angle_ = 0.0;
  double rgb_time_offset_ = 0.0;

  // Domain warping settings: independent noise + transform stack.
  double domainwarp_amount_ = 0.0;
  double domainwarp_zspeed_ = 0.0;
  int domainwarp_vector_mode_ = DW_VECTOR_FIELD;
  double domainwarp_map_blur_ = 0.0;
  double domainwarp_divergence_mix_ = 0.0;
  bool domainwarp_show_map_ = false;
  int domainwarp_type_ = NOISE_PERLIN;
  int domainwarp_type_menu_ = 0;
  int domainwarp_fractal_mode_ = FRACTAL_FBM;
  double domainwarp_xsize_ = 350.0;
  double domainwarp_ysize_ = 350.0;
  double domainwarp_zsize_ = 0.0;
  int domainwarp_octaves_ = 10;
  int domainwarp_voronoi_octaves_ = 1;
  int domainwarp_pattern_octaves_ = 1;
  int domainwarp_real_octaves_ = 10;
  double domainwarp_lacunarity_ = 2.0;
  double domainwarp_gain_ = 0.5;
  int domainwarp_voronoi_metric_ = VORONOI_EUCLIDEAN;
  int domainwarp_voronoi_shape_mode_ = VORONOI_SHAPE_VORONOI;
  double domainwarp_voronoi_minkowski_exp_ = 1.0;
  double domainwarp_voronoi_randomness_ = 1.0;
  int domainwarp_pattern_type_mode_ = PATTERN_CONCENTRIC;
  int domainwarp_pattern_shape_mode_ = PATTERN_SHAPE_ROUND;
  int domainwarp_pattern_segment_count_ = 10;
  int domainwarp_pattern_twist_ = 0;

  Channel output_channels_[4];
  Channel pref_channels_[4] = {Chan_Red, Chan_Green, Chan_Blue, Chan_Alpha};
  bool pref_keep_alpha_ = true;
  double center_[2] = {0.0, 0.0};
  double translate_speed_ = 0.0;
  double translate_angle_ = 0.0;
  double domainwarp_translate_speed_ = 0.0;
  double domainwarp_translate_angle_ = 0.0;
  double rotate2d_xy_[2] = {0.0, 0.0};
  double pref_translate_[3] = {0.0, 0.0, 0.0};
  double pref_scale_[3] = {1.0, 1.0, 1.0};
  double pref_skew_[3] = {0.0, 0.0, 0.0};
  double pref_rotate_[3] = {0.0, 0.0, 0.0};

  double domainwarp_center_[2] = {0.0, 0.0};
  double domainwarp_rotate2d_xy_[2] = {0.0, 0.0};
  double domainwarp_translate_[3] = {0.0, 0.0, 0.0};
  double domainwarp_scale_[3] = {1.0, 1.0, 1.0};
  double domainwarp_skew_[3] = {0.0, 0.0, 0.0};
  double domainwarp_rotate_[3] = {0.0, 0.0, 0.0};

  bool center_initialized_ = true;
  bool domainwarp_center_initialized_ = false;
  FormatPair formats_;
  Matrix4 invmatrix_;
  Matrix4 domainwarp_invmatrix_;
  bool uniform_ = false;
  bool domainwarp_uniform_ = false;
  float pref_center_latched_[3] = {0.0f, 0.0f, 0.0f};
  bool pref_center_latched_valid_ = false;
  float domainwarp_pref_center_latched_[3] = {0.0f, 0.0f, 0.0f};
  bool domainwarp_pref_center_latched_valid_ = false;
  float main_rgb_dx_[3] = {0.0f, 0.0f, 0.0f};
  float main_rgb_dy_[3] = {0.0f, 0.0f, 0.0f};
  float main_rgb_dt_[3] = {0.0f, 0.0f, 0.0f};
  bool main_rgb_has_offsets_ = false;
  float domainwarp_rgb_dx_[3] = {0.0f, 0.0f, 0.0f};
  float domainwarp_rgb_dy_[3] = {0.0f, 0.0f, 0.0f};
  float domainwarp_rgb_dt_[3] = {0.0f, 0.0f, 0.0f};
  bool domainwarp_rgb_has_offsets_ = false;
  struct MainRgbNoiseOffsetsCache {
    Vector3 delta[3] = {Vector3(), Vector3(), Vector3()};
    float time_offset[3] = {0.0f, 0.0f, 0.0f};
  };
  std::shared_ptr<const MainRgbNoiseOffsetsCache> main_rgb_noise_offsets_cache_;
  int main_fractal_mode_eval_ = FRACTAL_FBM;
  int main_real_octaves_eval_ = 10;
  float main_lacunarity_eval_ = 2.0f;
  float main_gain_eval_ = 0.5f;
  std::vector<float> main_octave_freqs_;
  std::vector<float> main_octave_amps_;
  float main_octave_amp_sum_ = 1.0f;
  int domainwarp_fractal_mode_eval_ = FRACTAL_FBM;
  int domainwarp_real_octaves_eval_ = 10;
  float domainwarp_lacunarity_eval_ = 2.0f;
  float domainwarp_gain_eval_ = 0.5f;
  std::vector<float> domainwarp_octave_freqs_;
  std::vector<float> domainwarp_octave_amps_;
  float domainwarp_octave_amp_sum_ = 1.0f;
  std::atomic<int> octave_tables_version_{0};



  struct DomainwarpFlowCacheKey {
    int format_x = 0;
    int format_y = 0;
    int format_r = 0;
    int format_t = 0;
    int projection_mode = PROJECTION_PLANAR;
    int domain_type = NOISE_PERLIN;
    int fractal_mode = FRACTAL_FBM;
    int octaves = 1;
    int vector_mode = DW_VECTOR_FIELD;
    int vor_metric = VORONOI_EUCLIDEAN;
    int vor_shape = VORONOI_SHAPE_VORONOI;
    int pattern_type_mode = PATTERN_CONCENTRIC;
    int pattern_shape_mode = PATTERN_SHAPE_ROUND;
    int pattern_segment_count = 10;
    int pattern_twist = 1;
    int use_time = 0;
    int rgb_has_offsets = 0;
    float seed = 0.0f;
    float relative_tx = 0.0f;
    float relative_ty = 0.0f;
    float lacunarity = 2.0f;
    float gain = 0.5f;
    float vector_offset = 0.0f;
    float map_blur = 0.0f;
    float divergence_mix = 0.0f;
    float vor_jitter = 1.0f;
    float vor_minkowski_exp = 1.0f;
    float center_x = 0.0f;
    float center_y = 0.0f;
    float translate_x = 0.0f;
    float translate_y = 0.0f;
    float translate_z = 0.0f;
    float inv_matrix[16] = {};
    float rgb_dx[3] = {0.0f, 0.0f, 0.0f};
    float rgb_dy[3] = {0.0f, 0.0f, 0.0f};
    float rgb_dt[3] = {0.0f, 0.0f, 0.0f};
  };

  struct DomainwarpFlowCacheView {
    std::shared_ptr<const std::vector<float>> storage;
    const float* data = nullptr;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    float sample_scale_x = 1.0f;
    float sample_scale_y = 1.0f;
  };

  bool domainwarp_flow_cache_valid_ = false;
  int domainwarp_flow_cache_width_ = 0;
  int domainwarp_flow_cache_height_ = 0;
  DomainwarpFlowCacheKey domainwarp_flow_cache_key_ = {};
  std::shared_ptr<const std::vector<float>> domainwarp_flow_cache_data_;
  bool domainwarp_flow_base_cache_valid_ = false;
  int domainwarp_flow_base_cache_width_ = 0;
  int domainwarp_flow_base_cache_height_ = 0;
  DomainwarpFlowCacheKey domainwarp_flow_base_cache_key_ = {};
  std::shared_ptr<const std::vector<float>> domainwarp_flow_base_cache_data_;
  Lock domainwarp_flow_cache_lock_;
  Lock domainwarp_flow_build_lock_;
  mutable bool projection_lon_cache_valid_ = false;
  mutable int projection_lon_cache_min_x_ = 0;
  mutable int projection_lon_cache_width_ = 0;
  mutable std::shared_ptr<const std::vector<float>> projection_lon_cos_cache_;
  mutable std::shared_ptr<const std::vector<float>> projection_lon_sin_cache_;
  mutable Lock projection_lon_cache_lock_;

 public:
  // Split into focused implementation chunks while keeping a single translation unit.
#include "t_vector_blur_ui.cpp"
#include "t_vector_blur_processing.cpp"
#include "t_vector_blur_noise.cpp"
#include "t_vector_blur_render.cpp"

  static const Iop::Description d;
};

static Iop* build(Node* node) { return new TVectorBlurBase(node); }
const Iop::Description TVectorBlurBase::d(CLASS, "Draw/TVectorBlur/TVectorBlur", build);

extern "C" void t_vector_blur_keepalive() {}
extern "C" void FnPlugin_GetAPI(int) {}

