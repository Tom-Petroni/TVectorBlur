#include "DDImage/Hash.h"
#include "DDImage/ImagePlane.h"
#include "DDImage/Iop.h"
#include "DDImage/Knobs.h"
#include "DDImage/NukeWrapper.h"
#include "DDImage/Row.h"
#include "DDImage/Thread.h"
#include "DDImage/ddImageVersion.h"

#include "TVectorBlurCUDAKernel.h"
#include "TVectorBlurShared.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>

using namespace DD::Image;

namespace
{
static const char* const kClass = "TVectorBlur";

static const char* const kHelp =
    "<b>TVectorBlur</b><br/>"
    "Directional vector blur with CUDA acceleration.<br/>"
    "Input 0: source image, Input 1: vectors in user-selectable channels.";

static const char* const kDirectionNames[] = {
    "Forward",
    "Backward",
    "Bi-directional",
    nullptr
};

static const char* const kTraceModeNames[] = {
    "Linear",
    "Advected",
    nullptr
};

static const char* const kChromaModeNames[] = {
    "Red Blue",
    "Red Green",
    "Green Blue",
    nullptr
};

static const char* const kFalloffNames[] = {
    "None",
    "Gaussian",
    "Linear",
    nullptr
};

static const char* const kBlurModeNames[] = {
    "Average",
    "Min",
    "Max",
    nullptr
};

int channel_to_index(Channel channel)
{
  switch (channel) {
    case Chan_Red:
      return 0;
    case Chan_Green:
      return 1;
    case Chan_Blue:
      return 2;
    case Chan_Alpha:
      return 3;
    default:
      return -1;
  }
}

float clampf(const float value, const float low, const float high)
{
  return std::max(low, std::min(value, high));
}

#if DD_IMAGE_VERSION_MAJOR >= 16
inline void tvb_begin_open_group(Knob_Callback f, const char* name, const char* label)
{
  BeginOpenGroup(f, name, label);
}
#else
inline void tvb_begin_open_group(Knob_Callback f, const char* name, const char* label)
{
  BeginGroup(f, name, label);
}
#endif

} // namespace

class TVectorBlurIop : public Iop
{
public:
  explicit TVectorBlurIop(Node* node)
      : Iop(node)
      , direction_(2)
      , trace_mode_(1)
      , blur_distance_(100.0f)
      , blur_samples_(100)
      , chroma_mode_(0)
      , chroma_invert_(false)
      , chroma_(1.0f)
      , vec_multiply_(1.0f)
      , vec_rotation_(0.0f)
      , blur_vectors_(0.0f)
      , vec_normalize_(false)
      , show_vectors_(false)
      , falloff_type_(0)
      , falloff_strength_(1.0f)
      , box_blur_(1.0f)
      , box_samples_(3)
      , blur_mode_(0)
      , mix_(1.0f)
      , offset_(0.0f)
      , cuda_initialized_(false)
      , cuda_gpu_name_("Unknown")
      , cache_valid_(false)
      , cache_x_(0)
      , cache_y_(0)
      , cache_w_(0)
      , cache_h_(0)
      , cuda_buffers_(tvb::create_device_buffers())
  {
    vector_channel_set_ += Chan_Red;
    vector_channel_set_ += Chan_Green;
  }

  ~TVectorBlurIop() override
  {
    tvb::destroy_device_buffers(cuda_buffers_);
    cuda_buffers_ = nullptr;
  }

  int maximum_inputs() const override { return 2; }
  int minimum_inputs() const override { return 2; }

  const char* input_label(int input, char*) const override
  {
    switch (input) {
      case 0:
        return "Img";
      case 1:
        return "Vectors";
      default:
        return nullptr;
    }
  }

  void knobs(Knob_Callback f) override
  {
    tvb_begin_open_group(f, "blur_group", "Blur");
    Enumeration_knob(f, &direction_, kDirectionNames, "direction", "Direction");
    Tooltip(f, "Forward: follows vector direction. Backward: opposite direction. Bi-directional: both sides.");
    Enumeration_knob(f, &trace_mode_, kTraceModeNames, "trace_mode", "Trace Mode");
    ClearFlags(f, Knob::STARTLINE);
    Tooltip(f, "Linear reads the vector once. Advected re-reads vectors at each step for curved motion.");
    Divider(f);
    Float_knob(f, &blur_distance_, IRange(0, 1000), "blur_distance", "Blur Distance");
    Divider(f);
    Int_knob(f, &blur_samples_, IRange(2, 512), "blur_samples", "Samples");
    EndGroup(f);

    Divider(f);

    tvb_begin_open_group(f, "chroma_group", "Chromatic Aberration");
    Enumeration_knob(f, &chroma_mode_, kChromaModeNames, "chroma_mode", "Chroma Mode");
    Bool_knob(f, &chroma_invert_, "chroma_invert", "Invert");
    SetFlags(f, Knob::STARTLINE);
    Divider(f);
    Float_knob(f, &chroma_, IRange(0, 2), "chroma", "Aberration");
    EndGroup(f);

    Divider(f);

    tvb_begin_open_group(f, "vectors_group", "Vectors");
    Input_ChannelMask_knob(f, &vector_channel_set_, 1, "vector_channels", "Vector Channels");
    Tooltip(f, "Select the channels from the Vectors input. The first two selected channels are used as U and V.");
    Divider(f);
    Float_knob(f, &vec_multiply_, IRange(-10, 10), "vec_multiply", "Multiply");
    Float_knob(f, &vec_rotation_, IRange(-180, 180), "vec_rotation", "Rotation");
    Float_knob(f, &offset_, IRange(-2, 2), "offset", "Offset");
    Divider(f);
    Float_knob(f, &blur_vectors_, IRange(0, 100), "blur_vectors", "Blur Vectors");
    Bool_knob(f, &vec_normalize_, "vec_normalize", "Normalize");
    SetFlags(f, Knob::STARTLINE);
    Bool_knob(f, &show_vectors_, "show_vectors", "Show Vectors");
    ClearFlags(f, Knob::STARTLINE);
    EndGroup(f);

    Divider(f);

    tvb_begin_open_group(f, "falloff_group", "Falloff");
    Enumeration_knob(f, &falloff_type_, kFalloffNames, "falloff_type", "Type");
    Divider(f);
    Float_knob(f, &falloff_strength_, IRange(0, 4), "falloff_strength", "Strength");
    EndGroup(f);

    Divider(f);

    tvb_begin_open_group(f, "box_group", "Box Blur");
    Float_knob(f, &box_blur_, IRange(0, 50), "box_blur", "Size");
    Divider(f);
    Int_knob(f, &box_samples_, IRange(0, 32), "box_samples", "Samples");
    EndGroup(f);

    Divider(f);

    BeginClosedGroup(f, "advanced_group", "Advanced");
    Enumeration_knob(f, &blur_mode_, kBlurModeNames, "blur_mode", "Combine Mode");
    EndGroup(f);
    Divider(f);
  }

  void _validate(bool) override
  {
    copy_info(0);
  }

  void _request(int, int, int, int, ChannelMask channels, int count) override
  {
    if (input(0)) {
      ChannelSet source_channels = channels;
      source_channels += Mask_RGBA;
      input(0)->request(info_.x(), info_.y(), info_.r(), info_.t(), source_channels, count);
    }

    if (input(1)) {
      input(1)->request(info_.x(), info_.y(), info_.r(), info_.t(), selected_vector_channelset(), count);
    }
  }

  void append(Hash& hash) override
  {
    if (input(0)) {
      hash = input0().hash();
    }
    if (input(1)) {
      hash.append(input1().hash());
    }

    hash.append(direction_);
    hash.append(trace_mode_);
    hash.append(blur_distance_);
    hash.append(blur_samples_);
    hash.append(static_cast<int>(selected_vector_channel(0)));
    hash.append(static_cast<int>(selected_vector_channel(1)));
    hash.append(chroma_mode_);
    hash.append(static_cast<int>(chroma_invert_));
    hash.append(chroma_);
    hash.append(vec_multiply_);
    hash.append(vec_rotation_);
    hash.append(blur_vectors_);
    hash.append(static_cast<int>(vec_normalize_));
    hash.append(static_cast<int>(show_vectors_));
    hash.append(falloff_type_);
    hash.append(falloff_strength_);
    hash.append(box_blur_);
    hash.append(box_samples_);
    hash.append(blur_mode_);
    hash.append(mix_);
    hash.append(offset_);
  }

  void engine(int y, int x, int r, ChannelMask channels, Row& row) override
  {
    if (!ensure_cache()) {
      row.erase(channels);
      return;
    }

    const ChannelSet rgba_channels = output_rgba_channels();
    ChannelSet pass_through = channels;
    pass_through -= rgba_channels;
    if (pass_through) {
      input0().get(y, x, r, pass_through, row);
    }

    ChannelSet effect_channels = channels;
    effect_channels &= rgba_channels;

    foreach (channel, effect_channels) {
      const int component = channel_to_index(channel);
      if (component < 0) {
        continue;
      }

      float* out_ptr = row.writable(channel) + x;
      const float* src_ptr = cache_planar_[component].data()
          + static_cast<size_t>(y - cache_y_) * static_cast<size_t>(cache_w_)
          + static_cast<size_t>(x - cache_x_);
      std::memcpy(out_ptr, src_ptr, static_cast<size_t>(r - x) * sizeof(float));
    }
  }

  const char* Class() const override { return kClass; }
  const char* node_help() const override { return kHelp; }
  float* mix_knob_storage() { return &mix_; }

  static const Iop::Description description;

private:
  Channel selected_vector_channel(const int index) const
  {
    Channel selected = Chan_Black;
    int found = 0;
    for (Channel chan = vector_channel_set_.first(); chan != Chan_Black; chan = vector_channel_set_.next(chan)) {
      if (found == index) {
        selected = chan;
        break;
      }
      ++found;
    }

    if (selected != Chan_Black) {
      return selected;
    }

    return index == 0 ? Chan_Red : Chan_Green;
  }

  ChannelSet selected_vector_channelset() const
  {
    ChannelSet channels;
    channels += selected_vector_channel(0);
    channels += selected_vector_channel(1);
    return channels;
  }

  void ensure_host_buffers()
  {
    const size_t rgba_size = static_cast<size_t>(cache_w_) * static_cast<size_t>(cache_h_) * 4ULL;
    const size_t vec_size = static_cast<size_t>(cache_w_) * static_cast<size_t>(cache_h_) * 2ULL;
    const size_t plane_size = static_cast<size_t>(cache_w_) * static_cast<size_t>(cache_h_);
    if (source_pixels_.size() != rgba_size) {
      source_pixels_.resize(rgba_size);
    }
    if (cache_result_.size() != rgba_size) {
      cache_result_.resize(rgba_size);
    }
    if (vector_pixels_.size() != vec_size) {
      vector_pixels_.resize(vec_size);
    }
    for (std::vector<float>& plane : cache_planar_) {
      if (plane.size() != plane_size) {
        plane.resize(plane_size);
      }
    }
  }

  void update_planar_cache()
  {
    const size_t pixel_count = static_cast<size_t>(cache_w_) * static_cast<size_t>(cache_h_);
    const float* src = cache_result_.data();
    float* plane_r = cache_planar_[0].data();
    float* plane_g = cache_planar_[1].data();
    float* plane_b = cache_planar_[2].data();
    float* plane_a = cache_planar_[3].data();

    for (size_t i = 0; i < pixel_count; ++i) {
      const size_t base = i * 4ULL;
      plane_r[i] = src[base + 0ULL];
      plane_g[i] = src[base + 1ULL];
      plane_b[i] = src[base + 2ULL];
      plane_a[i] = src[base + 3ULL];
    }
  }

  static Box cache_box(const int x, const int y, const int w, const int h)
  {
    return Box(x, y, x + w, y + h);
  }

  ChannelSet output_rgba_channels() const
  {
    ChannelSet rgba = Mask_RGBA;
    rgba &= info_.channels();
    return rgba;
  }

  bool fetch_source_rgba()
  {
    source_plane_ = ImagePlane(cache_box(cache_x_, cache_y_, cache_w_, cache_h_), true, Mask_RGBA);
    input0().fetchPlane(source_plane_);

    if (aborted() || !source_plane_.readable()) {
      return false;
    }

    const bool direct_rgba = source_plane_.packed()
        && source_plane_.channels() == Mask_RGBA
        && source_plane_.rowStride() == cache_w_ * 4
        && source_plane_.colStride() == 4;

    if (direct_rgba) {
      source_data_ = source_plane_.readable();
      return true;
    }

    float* dst = source_pixels_.data();
    for (int py = cache_y_; py < cache_y_ + cache_h_; ++py) {
      for (int px = cache_x_; px < cache_x_ + cache_w_; ++px) {
        *dst++ = source_plane_.at(px, py, Chan_Red);
        *dst++ = source_plane_.at(px, py, Chan_Green);
        *dst++ = source_plane_.at(px, py, Chan_Blue);
        *dst++ = source_plane_.at(px, py, Chan_Alpha);
      }
    }
    source_data_ = source_pixels_.data();
    return true;
  }

  bool fetch_vectors_rg()
  {
    const Channel vector_u_channel = selected_vector_channel(0);
    const Channel vector_v_channel = selected_vector_channel(1);
    const ChannelSet vector_channels = selected_vector_channelset();

    vector_plane_ = ImagePlane(cache_box(cache_x_, cache_y_, cache_w_, cache_h_), true, vector_channels);
    input1().fetchPlane(vector_plane_);

    if (aborted() || !vector_plane_.readable()) {
      return false;
    }

    const bool direct_rg = vector_plane_.packed()
        && vector_plane_.channels() == vector_channels
        && vector_plane_.rowStride() == cache_w_ * 2
        && vector_plane_.colStride() == 2;

    const int u_index = vector_plane_.chanNo(vector_u_channel);
    const int v_index = vector_plane_.chanNo(vector_v_channel);

    if (direct_rg
        && u_index == 0
        && v_index == 1
        && blur_vectors_ <= 0.001f) {
      vector_data_ = vector_plane_.readable();
      return true;
    }

    float* dst = vector_pixels_.data();
    const float* src = vector_plane_.readable();
    if (vector_plane_.packed() && src) {
      if (u_index == 0 && v_index == 1 && vector_plane_.rowStride() == cache_w_ * 2 && vector_plane_.colStride() == 2) {
        std::memcpy(dst, src, vector_pixels_.size() * sizeof(float));
        vector_data_ = vector_pixels_.data();
        return true;
      }

      const int row_stride = vector_plane_.rowStride();
      const int col_stride = vector_plane_.colStride();
      for (int py = 0; py < cache_h_; ++py) {
        const float* src_row = src + static_cast<size_t>(py) * static_cast<size_t>(row_stride);
        for (int px = 0; px < cache_w_; ++px) {
          const float* src_pixel = src_row + static_cast<size_t>(px) * static_cast<size_t>(col_stride);
          *dst++ = u_index >= 0 ? src_pixel[u_index] : 0.0f;
          *dst++ = v_index >= 0 ? src_pixel[v_index] : 0.0f;
        }
      }
    } else {
      const int64_t chan_stride = vector_plane_.chanStride();
      const int row_stride = vector_plane_.rowStride();
      const float* u_plane = (src && u_index >= 0) ? (src + static_cast<int64_t>(u_index) * chan_stride) : nullptr;
      const float* v_plane = (src && v_index >= 0) ? (src + static_cast<int64_t>(v_index) * chan_stride) : nullptr;
      for (int py = 0; py < cache_h_; ++py) {
        const float* u_row = u_plane ? (u_plane + static_cast<size_t>(py) * static_cast<size_t>(row_stride)) : nullptr;
        const float* v_row = v_plane ? (v_plane + static_cast<size_t>(py) * static_cast<size_t>(row_stride)) : nullptr;
        for (int px = 0; px < cache_w_; ++px) {
          *dst++ = u_row ? u_row[px] : 0.0f;
          *dst++ = v_row ? v_row[px] : 0.0f;
        }
      }
    }

    vector_data_ = vector_pixels_.data();
    return true;
  }

  tvb::Params build_params() const
  {
    tvb::Params params{};
    params.width = cache_w_;
    params.height = cache_h_;
    params.distance = std::max(0.0f, blur_distance_);
    params.samples = std::max(2, blur_samples_);
    params.min_samples = std::max(1, std::min(10, params.samples));
    params.adaptive = 1;
    params.direction = direction_;
    params.trace_mode = trace_mode_;
    params.offset = offset_;

    params.vec_multiply = vec_multiply_;
    const float radians = vec_rotation_ * 3.14159265358979323846f / 180.0f;
    params.cos_rot = std::cos(radians);
    params.sin_rot = std::sin(radians);
    params.vec_normalize = vec_normalize_ ? 1 : 0;
    params.vector_blur_radius = std::max(0, static_cast<int>(std::round(blur_vectors_)));

    params.box_blur = std::max(0.0f, box_blur_);
    params.box_samples = std::max(0, box_samples_);

    params.falloff_type = falloff_type_;
    params.falloff_strength = std::max(0.0f, falloff_strength_);

    params.chroma_mode = chroma_mode_;
    params.chroma_invert = chroma_invert_ ? 1 : 0;

    params.blur_mode = blur_mode_;
    params.mix = clampf(mix_, 0.0f, 1.0f);
    params.show_vectors = show_vectors_ ? 1 : 0;

    params.mask_mix = 0.0f;
    params.invert_mask = 0;

    float shift = clampf(chroma_, 0.0f, 2.0f) * 0.5f;
    if (chroma_invert_) {
      shift = -shift;
    }

    params.fwd_r = 0.5f;
    params.fwd_g = 0.5f;
    params.fwd_b = 0.5f;

    if (chroma_mode_ == 0) {
      params.fwd_r -= shift;
      params.fwd_b += shift;
    } else if (chroma_mode_ == 1) {
      params.fwd_r -= shift;
      params.fwd_g += shift;
    } else if (chroma_mode_ == 2) {
      params.fwd_g -= shift;
      params.fwd_b += shift;
    }

    params.fwd_r = clampf(params.fwd_r, 0.0f, 1.0f);
    params.fwd_g = clampf(params.fwd_g, 0.0f, 1.0f);
    params.fwd_b = clampf(params.fwd_b, 0.0f, 1.0f);

    return params;
  }

  bool ensure_cuda_initialized(std::string& error_message)
  {
    if (cuda_initialized_) {
      return true;
    }

    if (!tvb::is_cuda_available(cuda_gpu_name_, error_message)) {
      std::fprintf(
          stderr,
          "[TVectorBlur] CUDA initialization failed: %s\n",
          error_message.empty() ? "Unknown CUDA error." : error_message.c_str());
      std::fflush(stderr);
      return false;
    }

    cuda_initialized_ = true;
    return true;
  }

  bool ensure_cache()
  {
    Guard guard(cache_lock_);

    const int next_x = info_.x();
    const int next_y = info_.y();
    const int next_w = info_.r() - info_.x();
    const int next_h = info_.t() - info_.y();
    const Hash current_hash = hash();

    if (cache_valid_
        && current_hash == cache_hash_
        && next_x == cache_x_
        && next_y == cache_y_
        && next_w == cache_w_
        && next_h == cache_h_) {
      return true;
    }

    if (!input(0) || !input(1)) {
      error("TVectorBlur requires an image input and a vector input.");
      return false;
    }

    cache_x_ = next_x;
    cache_y_ = next_y;
    cache_w_ = next_w;
    cache_h_ = next_h;

    if (cache_w_ <= 0 || cache_h_ <= 0) {
      cache_result_.clear();
      source_pixels_.clear();
      vector_pixels_.clear();
      source_plane_.clear();
      vector_plane_.clear();
      source_data_ = nullptr;
      vector_data_ = nullptr;
      cache_hash_ = current_hash;
      cache_valid_ = true;
      return true;
    }

    ensure_host_buffers();

    if (!fetch_source_rgba()) {
      cache_valid_ = false;
      return false;
    }
    if (!fetch_vectors_rg()) {
      cache_valid_ = false;
      return false;
    }

    const tvb::Params params = build_params();

    std::string error_message;
    if (!ensure_cuda_initialized(error_message)) {
      error(error_message.empty() ? "CUDA unavailable." : error_message.c_str());
      cache_valid_ = false;
      return false;
    }

    if (!tvb::run_cuda(
            cuda_buffers_,
            source_data_,
            vector_data_,
            cache_result_.data(),
            params,
            error_message)) {
      std::fprintf(
          stderr,
          "[TVectorBlur] CUDA execution failed on %s: %s\n",
          cuda_gpu_name_.c_str(),
          error_message.empty() ? "Unknown CUDA error." : error_message.c_str());
      std::fflush(stderr);
      error(error_message.empty() ? "CUDA execution failed." : error_message.c_str());
      cache_valid_ = false;
      return false;
    }
    update_planar_cache();

    cache_hash_ = current_hash;
    cache_valid_ = true;
    return true;
  }

private:
  ChannelSet vector_channel_set_;
  int direction_;
  int trace_mode_;
  float blur_distance_;
  int blur_samples_;

  int chroma_mode_;
  bool chroma_invert_;
  float chroma_;

  float vec_multiply_;
  float vec_rotation_;
  float blur_vectors_;
  bool vec_normalize_;
  bool show_vectors_;

  int falloff_type_;
  float falloff_strength_;

  float box_blur_;
  int box_samples_;

  int blur_mode_;
  float mix_;

  float offset_;

  bool cuda_initialized_;
  std::string cuda_gpu_name_;

  bool cache_valid_;
  Hash cache_hash_;
  int cache_x_;
  int cache_y_;
  int cache_w_;
  int cache_h_;
  const float* source_data_ = nullptr;
  const float* vector_data_ = nullptr;
  ImagePlane source_plane_;
  ImagePlane vector_plane_;
  std::vector<float> source_pixels_;
  std::vector<float> vector_pixels_;
  std::vector<float> cache_result_;
  std::array<std::vector<float>, 4> cache_planar_;
  Lock cache_lock_;
  tvb::DeviceBuffers* cuda_buffers_;
};

class TVectorBlurWrapper : public NukeWrapper
{
public:
  explicit TVectorBlurWrapper(Iop* op)
      : NukeWrapper(op)
  {
  }

  void knobs(Knob_Callback f) override
  {
    NukeWrapper::knobs(f);
    if (TVectorBlurIop* blur = dynamic_cast<TVectorBlurIop*>(wrapped_iop())) {
      Float_knob(f, blur->mix_knob_storage(), IRange(0, 1), "mix", "mix");
    }
    Divider(f);
    Text_knob(
        f,
        "",
        "TVectorBlur 2.0.3 - 2026 - <a href=\"www.linkedin.com/in/thomas-petroni\"><font color=\"#55aaff\"><u>Thomas Petroni</u></font></a>");
  }
};

static Iop* build_tvector_blur(Node* node)
{
  return (new TVectorBlurWrapper(new TVectorBlurIop(node)))->channels(Mask_RGBA)->noMix()->noUnpremult();
}

const Iop::Description TVectorBlurIop::description(kClass, 0, build_tvector_blur);
