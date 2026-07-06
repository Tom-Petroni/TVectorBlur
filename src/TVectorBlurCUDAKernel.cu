#include "TVectorBlurCUDAKernel.h"

#include <cuda_runtime.h>

#include <cstring>
#include <sstream>

namespace tvb
{
struct DeviceBuffers
{
  float4* d_src = nullptr;
  float2* d_vec = nullptr;
  float2* d_vec_tmp = nullptr;
  float4* d_dst = nullptr;
  size_t src_pitch = 0;
  size_t vec_pitch = 0;
  size_t vec_tmp_pitch = 0;
  size_t dst_pitch = 0;
  cudaTextureObject_t src_tex = 0;
  cudaTextureObject_t vec_tex = 0;
  cudaStream_t stream = nullptr;
  int width = 0;
  int height = 0;
};

namespace
{
#define TVB_CUDA_INLINE __device__ __forceinline__

inline bool check_cuda(cudaError_t status, std::string& error_message, const char* context)
{
  if (status == cudaSuccess) {
    return true;
  }

  std::ostringstream stream;
  stream << context << ": " << cudaGetErrorString(status);
  error_message = stream.str();
  return false;
}

inline void destroy_texture(cudaTextureObject_t& texture)
{
  if (texture) {
    cudaDestroyTextureObject(texture);
    texture = 0;
  }
}

inline void reset_buffers(DeviceBuffers* buffers)
{
  if (!buffers) {
    return;
  }

  destroy_texture(buffers->src_tex);
  destroy_texture(buffers->vec_tex);

  if (buffers->d_dst) {
    cudaFree(buffers->d_dst);
    buffers->d_dst = nullptr;
  }
  if (buffers->d_vec) {
    cudaFree(buffers->d_vec);
    buffers->d_vec = nullptr;
  }
  if (buffers->d_vec_tmp) {
    cudaFree(buffers->d_vec_tmp);
    buffers->d_vec_tmp = nullptr;
  }
  if (buffers->d_src) {
    cudaFree(buffers->d_src);
    buffers->d_src = nullptr;
  }

  buffers->src_pitch = 0;
  buffers->vec_pitch = 0;
  buffers->vec_tmp_pitch = 0;
  buffers->dst_pitch = 0;
  buffers->width = 0;
  buffers->height = 0;
}

inline bool create_texture_object(
    cudaTextureObject_t& texture,
    const void* dev_ptr,
    const size_t pitch,
    const int width,
    const int height,
    const cudaChannelFormatDesc& channel_desc,
    std::string& error_message,
    const char* context)
{
  destroy_texture(texture);

  cudaResourceDesc resource_desc;
  std::memset(&resource_desc, 0, sizeof(resource_desc));
  resource_desc.resType = cudaResourceTypePitch2D;
  resource_desc.res.pitch2D.devPtr = const_cast<void*>(dev_ptr);
  resource_desc.res.pitch2D.desc = channel_desc;
  resource_desc.res.pitch2D.width = static_cast<size_t>(width);
  resource_desc.res.pitch2D.height = static_cast<size_t>(height);
  resource_desc.res.pitch2D.pitchInBytes = pitch;

  cudaTextureDesc texture_desc;
  std::memset(&texture_desc, 0, sizeof(texture_desc));
  texture_desc.addressMode[0] = cudaAddressModeClamp;
  texture_desc.addressMode[1] = cudaAddressModeClamp;
  texture_desc.filterMode = cudaFilterModeLinear;
  texture_desc.readMode = cudaReadModeElementType;
  texture_desc.normalizedCoords = 0;

  return check_cuda(cudaCreateTextureObject(&texture, &resource_desc, &texture_desc, nullptr), error_message, context);
}

inline bool ensure_buffers(DeviceBuffers* buffers, const Params& params, std::string& error_message)
{
  if (!buffers) {
    error_message = "CUDA buffers were not initialized.";
    return false;
  }

  if (buffers->width == params.width
      && buffers->height == params.height
      && buffers->d_src
      && buffers->d_vec
      && buffers->d_vec_tmp
      && buffers->d_dst
      && buffers->src_tex
      && buffers->vec_tex) {
    return true;
  }

  reset_buffers(buffers);

  if (!check_cuda(
          cudaMallocPitch(
              reinterpret_cast<void**>(&buffers->d_src),
              &buffers->src_pitch,
              static_cast<size_t>(params.width) * sizeof(float4),
              static_cast<size_t>(params.height)),
          error_message,
          "cudaMallocPitch(src)")) {
    reset_buffers(buffers);
    return false;
  }

  if (!check_cuda(
          cudaMallocPitch(
              reinterpret_cast<void**>(&buffers->d_vec),
              &buffers->vec_pitch,
              static_cast<size_t>(params.width) * sizeof(float2),
              static_cast<size_t>(params.height)),
          error_message,
          "cudaMallocPitch(vec)")) {
    reset_buffers(buffers);
    return false;
  }

  if (!check_cuda(
          cudaMallocPitch(
              reinterpret_cast<void**>(&buffers->d_vec_tmp),
              &buffers->vec_tmp_pitch,
              static_cast<size_t>(params.width) * sizeof(float2),
              static_cast<size_t>(params.height)),
          error_message,
          "cudaMallocPitch(vec_tmp)")) {
    reset_buffers(buffers);
    return false;
  }

  if (!check_cuda(
          cudaMallocPitch(
              reinterpret_cast<void**>(&buffers->d_dst),
              &buffers->dst_pitch,
              static_cast<size_t>(params.width) * sizeof(float4),
              static_cast<size_t>(params.height)),
          error_message,
          "cudaMallocPitch(dst)")) {
    reset_buffers(buffers);
    return false;
  }

  const cudaChannelFormatDesc src_desc = cudaCreateChannelDesc<float4>();
  if (!create_texture_object(
          buffers->src_tex,
          buffers->d_src,
          buffers->src_pitch,
          params.width,
          params.height,
          src_desc,
          error_message,
          "cudaCreateTextureObject(src)")) {
    reset_buffers(buffers);
    return false;
  }

  const cudaChannelFormatDesc vec_desc = cudaCreateChannelDesc<float2>();
  if (!create_texture_object(
          buffers->vec_tex,
          buffers->d_vec,
          buffers->vec_pitch,
          params.width,
          params.height,
          vec_desc,
          error_message,
          "cudaCreateTextureObject(vec)")) {
    reset_buffers(buffers);
    return false;
  }

  buffers->width = params.width;
  buffers->height = params.height;
  return true;
}

struct DeviceImages
{
  cudaTextureObject_t src_tex;
  cudaTextureObject_t vec_tex;
  float4* dst;
  size_t dst_pitch;
};

TVB_CUDA_INLINE float sample_component(const Float4& value, const int channel)
{
  if (channel == 0) {
    return value.x;
  }
  if (channel == 1) {
    return value.y;
  }
  if (channel == 2) {
    return value.z;
  }
  return value.w;
}

TVB_CUDA_INLINE Float4 sample_src_rgba(const DeviceImages& images, const float x, const float y)
{
  const float4 value = tex2D<float4>(images.src_tex, x + 0.5f, y + 0.5f);
  return {value.x, value.y, value.z, value.w};
}

TVB_CUDA_INLINE void sample_vector_raw_cuda(
    const DeviceImages& images,
    const float x,
    const float y,
    float& out_u,
    float& out_v)
{
  const float2 value = tex2D<float2>(images.vec_tex, x + 0.5f, y + 0.5f);
  out_u = value.x;
  out_v = value.y;
}

TVB_CUDA_INLINE void read_vec_cuda(
    const DeviceImages& images,
    const Params& params,
    const float x,
    const float y,
    float& out_u,
    float& out_v)
{
  float raw_u;
  float raw_v;
  sample_vector_raw_cuda(images, x, y, raw_u, raw_v);

  if (params.vec_normalize) {
    const float len = sqrtf(raw_u * raw_u + raw_v * raw_v);
    if (len > 0.0001f) {
      raw_u /= len;
      raw_v /= len;
    }
  }

  const float rotated_u = raw_u * params.cos_rot - raw_v * params.sin_rot;
  const float rotated_v = raw_u * params.sin_rot + raw_v * params.cos_rot;

  out_u = rotated_u * params.vec_multiply;
  out_v = rotated_v * params.vec_multiply;
}

TVB_CUDA_INLINE float sample_src_box_channel_cuda(
    const DeviceImages& images,
    const Params& params,
    const int channel,
    const float x,
    const float y,
    const float perp_x,
    const float perp_y)
{
  if (params.box_blur <= 0.001f || params.box_samples <= 0) {
    return sample_component(sample_src_rgba(images, x, y), channel);
  }

  float acc = 0.0f;
  const int total = params.box_samples * 2 + 1;

  for (int b = -params.box_samples; b <= params.box_samples; ++b) {
    const float t = static_cast<float>(b) / static_cast<float>(params.box_samples);
    acc += sample_component(
        sample_src_rgba(
            images,
            x + perp_x * t * params.box_blur,
            y + perp_y * t * params.box_blur),
        channel);
  }

  return acc / static_cast<float>(total);
}

TVB_CUDA_INLINE Float4 sample_src_box_rgba_cuda(
    const DeviceImages& images,
    const Params& params,
    const float x,
    const float y,
    const float perp_x,
    const float perp_y)
{
  if (params.box_blur <= 0.001f || params.box_samples <= 0) {
    return sample_src_rgba(images, x, y);
  }

  Float4 acc{0.0f, 0.0f, 0.0f, 0.0f};
  const int total = params.box_samples * 2 + 1;

  for (int b = -params.box_samples; b <= params.box_samples; ++b) {
    const float t = static_cast<float>(b) / static_cast<float>(params.box_samples);
    const Float4 sample = sample_src_rgba(
        images,
        x + perp_x * t * params.box_blur,
        y + perp_y * t * params.box_blur);
    acc.x += sample.x;
    acc.y += sample.y;
    acc.z += sample.z;
    acc.w += sample.w;
  }

  const float inv_total = 1.0f / static_cast<float>(total);
  acc.x *= inv_total;
  acc.y *= inv_total;
  acc.z *= inv_total;
  acc.w *= inv_total;
  return acc;
}

TVB_CUDA_INLINE void trace_channel_cuda(
    const DeviceImages& images,
    const Params& params,
    const int channel,
    const int px,
    const int py,
    const float dist,
    const float dir_sign,
    const int steps,
    float& acc,
    float& cnt)
{
  if (dist <= 0.001f || steps <= 0) {
    return;
  }

  float orig_u;
  float orig_v;
  read_vec_cuda(images, params, static_cast<float>(px), static_cast<float>(py), orig_u, orig_v);

  const float vec_len = sqrtf(orig_u * orig_u + orig_v * orig_v);
  float perp_x = 0.0f;
  float perp_y = 0.0f;

  if (vec_len > 0.0001f) {
    perp_x = -orig_v / vec_len;
    perp_y = orig_u / vec_len;
  }

  const float start_x = static_cast<float>(px) + orig_u * params.offset * dir_sign;
  const float start_y = static_cast<float>(py) + orig_v * params.offset * dir_sign;

  float cur_x = start_x;
  float cur_y = start_y;

  for (int i = 1; i <= steps; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(steps);

    if (params.trace_mode == 0) {
      cur_x = start_x + orig_u * t * dist * dir_sign;
      cur_y = start_y + orig_v * t * dist * dir_sign;
    } else {
      float vec_u;
      float vec_v;
      read_vec_cuda(images, params, cur_x, cur_y, vec_u, vec_v);

      const float step_dist = (dist / static_cast<float>(steps)) * dir_sign;
      cur_x += vec_u * step_dist;
      cur_y += vec_v * step_dist;

      const float local_len = sqrtf(vec_u * vec_u + vec_v * vec_v);
      if (local_len > 0.0001f) {
        perp_x = -vec_v / local_len;
        perp_y = vec_u / local_len;
      }
    }

    float sample = sample_src_box_channel_cuda(images, params, channel, cur_x, cur_y, perp_x, perp_y);
    const float weight = get_falloff(params, t);
    sample *= weight;

    if (params.blur_mode == 0) {
      acc += sample;
      cnt += weight;
    } else if (params.blur_mode == 1) {
      acc = minf(acc, sample);
    } else if (params.blur_mode == 2) {
      acc = maxf(acc, sample);
    }
  }
}

TVB_CUDA_INLINE void trace_rgba_cuda(
    const DeviceImages& images,
    const Params& params,
    const int px,
    const int py,
    const float dist,
    const float dir_sign,
    const int steps,
    Float4& acc,
    float& cnt)
{
  if (dist <= 0.001f || steps <= 0) {
    return;
  }

  float orig_u;
  float orig_v;
  read_vec_cuda(images, params, static_cast<float>(px), static_cast<float>(py), orig_u, orig_v);

  const float vec_len = sqrtf(orig_u * orig_u + orig_v * orig_v);
  float perp_x = 0.0f;
  float perp_y = 0.0f;

  if (vec_len > 0.0001f) {
    perp_x = -orig_v / vec_len;
    perp_y = orig_u / vec_len;
  }

  const float start_x = static_cast<float>(px) + orig_u * params.offset * dir_sign;
  const float start_y = static_cast<float>(py) + orig_v * params.offset * dir_sign;

  float cur_x = start_x;
  float cur_y = start_y;

  for (int i = 1; i <= steps; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(steps);

    if (params.trace_mode == 0) {
      cur_x = start_x + orig_u * t * dist * dir_sign;
      cur_y = start_y + orig_v * t * dist * dir_sign;
    } else {
      float vec_u;
      float vec_v;
      read_vec_cuda(images, params, cur_x, cur_y, vec_u, vec_v);

      const float step_dist = (dist / static_cast<float>(steps)) * dir_sign;
      cur_x += vec_u * step_dist;
      cur_y += vec_v * step_dist;

      const float local_len = sqrtf(vec_u * vec_u + vec_v * vec_v);
      if (local_len > 0.0001f) {
        perp_x = -vec_v / local_len;
        perp_y = vec_u / local_len;
      }
    }

    Float4 sample = sample_src_box_rgba_cuda(images, params, cur_x, cur_y, perp_x, perp_y);
    const float weight = get_falloff(params, t);
    sample.x *= weight;
    sample.y *= weight;
    sample.z *= weight;
    sample.w *= weight;

    acc.x += sample.x;
    acc.y += sample.y;
    acc.z += sample.z;
    acc.w += sample.w;
    cnt += weight;
  }
}

TVB_CUDA_INLINE int get_adaptive_steps_cuda(const DeviceImages& images, const Params& params, const int px, const int py)
{
  float vec_u;
  float vec_v;
  read_vec_cuda(images, params, static_cast<float>(px), static_cast<float>(py), vec_u, vec_v);
  const float vec_len = sqrtf(vec_u * vec_u + vec_v * vec_v);

  int needed = static_cast<int>(vec_len * params.distance + 0.5f);
  if (needed < params.min_samples) {
    return params.min_samples;
  }
  if (needed > params.samples) {
    return params.samples;
  }
  return needed;
}

TVB_CUDA_INLINE Float4 process_pixel_cuda(const DeviceImages& images, const Params& params, const int px, const int py)
{
  const Float4 center = sample_src_rgba(images, static_cast<float>(px), static_cast<float>(py));

  if (params.show_vectors) {
    float u;
    float v;
    read_vec_cuda(images, params, static_cast<float>(px), static_cast<float>(py), u, v);
    return {u, v, 0.0f, 1.0f};
  }

  if (params.mix <= 0.0f || params.distance <= 0.001f || params.samples <= 1) {
    return center;
  }

  const int actual_steps = params.adaptive ? get_adaptive_steps_cuda(images, params, px, py) : params.samples;
  int total_steps = actual_steps - 1;
  if (total_steps < 1) {
    total_steps = 1;
  }

  float dist_fwd_r;
  float dist_bwd_r;
  float dist_fwd_g;
  float dist_bwd_g;
  float dist_fwd_b;
  float dist_bwd_b;
  float dist_fwd_a;
  float dist_bwd_a;

  int steps_fwd_r;
  int steps_bwd_r;
  int steps_fwd_g;
  int steps_bwd_g;
  int steps_fwd_b;
  int steps_bwd_b;
  int steps_fwd_a;
  int steps_bwd_a;

  if (params.direction == 0) {
    dist_fwd_r = params.distance * (params.fwd_r * 2.0f);
    dist_bwd_r = 0.0f;
    dist_fwd_g = params.distance * (params.fwd_g * 2.0f);
    dist_bwd_g = 0.0f;
    dist_fwd_b = params.distance * (params.fwd_b * 2.0f);
    dist_bwd_b = 0.0f;
    dist_fwd_a = params.distance;
    dist_bwd_a = 0.0f;

    steps_fwd_r = steps_fwd_g = steps_fwd_b = steps_fwd_a = total_steps;
    steps_bwd_r = steps_bwd_g = steps_bwd_b = steps_bwd_a = 0;
  } else if (params.direction == 1) {
    dist_fwd_r = 0.0f;
    dist_bwd_r = params.distance * ((1.0f - params.fwd_r) * 2.0f);
    dist_fwd_g = 0.0f;
    dist_bwd_g = params.distance * ((1.0f - params.fwd_g) * 2.0f);
    dist_fwd_b = 0.0f;
    dist_bwd_b = params.distance * ((1.0f - params.fwd_b) * 2.0f);
    dist_fwd_a = 0.0f;
    dist_bwd_a = params.distance;

    steps_fwd_r = steps_fwd_g = steps_fwd_b = steps_fwd_a = 0;
    steps_bwd_r = steps_bwd_g = steps_bwd_b = steps_bwd_a = total_steps;
  } else {
    dist_fwd_r = params.distance * params.fwd_r;
    dist_bwd_r = params.distance * (1.0f - params.fwd_r);
    dist_fwd_g = params.distance * params.fwd_g;
    dist_bwd_g = params.distance * (1.0f - params.fwd_g);
    dist_fwd_b = params.distance * params.fwd_b;
    dist_bwd_b = params.distance * (1.0f - params.fwd_b);
    dist_fwd_a = params.distance * 0.5f;
    dist_bwd_a = params.distance * 0.5f;

    steps_fwd_r = static_cast<int>(static_cast<float>(total_steps) * params.fwd_r + 0.5f);
    steps_bwd_r = total_steps - steps_fwd_r;
    steps_fwd_g = static_cast<int>(static_cast<float>(total_steps) * params.fwd_g + 0.5f);
    steps_bwd_g = total_steps - steps_fwd_g;
    steps_fwd_b = static_cast<int>(static_cast<float>(total_steps) * params.fwd_b + 0.5f);
    steps_bwd_b = total_steps - steps_fwd_b;
    steps_fwd_a = static_cast<int>(static_cast<float>(total_steps) * 0.5f + 0.5f);
    steps_bwd_a = total_steps - steps_fwd_a;

    if (steps_fwd_r < 1 && dist_fwd_r > 0.001f) {
      steps_fwd_r = 1;
      steps_bwd_r = total_steps - 1;
    }
    if (steps_bwd_r < 1 && dist_bwd_r > 0.001f) {
      steps_bwd_r = 1;
      steps_fwd_r = total_steps - 1;
    }
    if (steps_fwd_g < 1 && dist_fwd_g > 0.001f) {
      steps_fwd_g = 1;
      steps_bwd_g = total_steps - 1;
    }
    if (steps_bwd_g < 1 && dist_bwd_g > 0.001f) {
      steps_bwd_g = 1;
      steps_fwd_g = total_steps - 1;
    }
    if (steps_fwd_b < 1 && dist_fwd_b > 0.001f) {
      steps_fwd_b = 1;
      steps_bwd_b = total_steps - 1;
    }
    if (steps_bwd_b < 1 && dist_bwd_b > 0.001f) {
      steps_bwd_b = 1;
      steps_fwd_b = total_steps - 1;
    }
  }

  const float center_w = get_falloff(params, 0.0f);
  const bool uniform_rgba_trace = params.blur_mode == 0
      && fabsf(dist_fwd_r - dist_fwd_g) < 1.0e-6f
      && fabsf(dist_fwd_r - dist_fwd_b) < 1.0e-6f
      && fabsf(dist_fwd_r - dist_fwd_a) < 1.0e-6f
      && fabsf(dist_bwd_r - dist_bwd_g) < 1.0e-6f
      && fabsf(dist_bwd_r - dist_bwd_b) < 1.0e-6f
      && fabsf(dist_bwd_r - dist_bwd_a) < 1.0e-6f
      && steps_fwd_r == steps_fwd_g
      && steps_fwd_r == steps_fwd_b
      && steps_fwd_r == steps_fwd_a
      && steps_bwd_r == steps_bwd_g
      && steps_bwd_r == steps_bwd_b
      && steps_bwd_r == steps_bwd_a;

  if (uniform_rgba_trace) {
    Float4 acc{
        center.x * center_w,
        center.y * center_w,
        center.z * center_w,
        center.w * center_w};
    float cnt = center_w;

    trace_rgba_cuda(images, params, px, py, dist_fwd_r, 1.0f, steps_fwd_r, acc, cnt);
    trace_rgba_cuda(images, params, px, py, dist_bwd_r, -1.0f, steps_bwd_r, acc, cnt);

    const Float4 result = cnt > 0.0f
        ? Float4{acc.x / cnt, acc.y / cnt, acc.z / cnt, acc.w / cnt}
        : center;

    if (params.mix >= 1.0f) {
      return result;
    }

    return {
        center.x + (result.x - center.x) * params.mix,
        center.y + (result.y - center.y) * params.mix,
        center.z + (result.z - center.z) * params.mix,
        center.w + (result.w - center.w) * params.mix};
  }

  float acc_r = center.x * center_w;
  float cnt_r = center_w;
  trace_channel_cuda(images, params, 0, px, py, dist_fwd_r, 1.0f, steps_fwd_r, acc_r, cnt_r);
  trace_channel_cuda(images, params, 0, px, py, dist_bwd_r, -1.0f, steps_bwd_r, acc_r, cnt_r);

  float acc_g = center.y * center_w;
  float cnt_g = center_w;
  trace_channel_cuda(images, params, 1, px, py, dist_fwd_g, 1.0f, steps_fwd_g, acc_g, cnt_g);
  trace_channel_cuda(images, params, 1, px, py, dist_bwd_g, -1.0f, steps_bwd_g, acc_g, cnt_g);

  float acc_b = center.z * center_w;
  float cnt_b = center_w;
  trace_channel_cuda(images, params, 2, px, py, dist_fwd_b, 1.0f, steps_fwd_b, acc_b, cnt_b);
  trace_channel_cuda(images, params, 2, px, py, dist_bwd_b, -1.0f, steps_bwd_b, acc_b, cnt_b);

  float acc_a = center.w * center_w;
  float cnt_a = center_w;
  trace_channel_cuda(images, params, 3, px, py, dist_fwd_a, 1.0f, steps_fwd_a, acc_a, cnt_a);
  trace_channel_cuda(images, params, 3, px, py, dist_bwd_a, -1.0f, steps_bwd_a, acc_a, cnt_a);

  Float4 result;
  if (params.blur_mode == 0) {
    result = {
        cnt_r > 0.0f ? acc_r / cnt_r : center.x,
        cnt_g > 0.0f ? acc_g / cnt_g : center.y,
        cnt_b > 0.0f ? acc_b / cnt_b : center.z,
        cnt_a > 0.0f ? acc_a / cnt_a : center.w};
  } else {
    result = {acc_r, acc_g, acc_b, acc_a};
  }

  if (params.mix >= 1.0f) {
    return result;
  }

  return {
      center.x + (result.x - center.x) * params.mix,
      center.y + (result.y - center.y) * params.mix,
      center.z + (result.z - center.z) * params.mix,
      center.w + (result.w - center.w) * params.mix};
}

__global__ void vector_blur_horizontal_kernel(
    const float2* src,
    const size_t src_pitch,
    float2* dst,
    const size_t dst_pitch,
    const int width,
    const int height,
    const int radius)
{
  const int x = blockIdx.x * blockDim.x + threadIdx.x;
  const int y = blockIdx.y * blockDim.y + threadIdx.y;

  if (x >= width || y >= height) {
    return;
  }

  const float2* src_row = reinterpret_cast<const float2*>(reinterpret_cast<const char*>(src) + static_cast<size_t>(y) * src_pitch);
  float2* dst_row = reinterpret_cast<float2*>(reinterpret_cast<char*>(dst) + static_cast<size_t>(y) * dst_pitch);

  float2 acc = make_float2(0.0f, 0.0f);
  int count = 0;
  for (int sx = max(0, x - radius); sx <= min(width - 1, x + radius); ++sx) {
    const float2 value = src_row[sx];
    acc.x += value.x;
    acc.y += value.y;
    ++count;
  }

  const float inv_count = count > 0 ? (1.0f / static_cast<float>(count)) : 0.0f;
  dst_row[x] = make_float2(acc.x * inv_count, acc.y * inv_count);
}

__global__ void vector_blur_vertical_kernel(
    const float2* src,
    const size_t src_pitch,
    float2* dst,
    const size_t dst_pitch,
    const int width,
    const int height,
    const int radius)
{
  const int x = blockIdx.x * blockDim.x + threadIdx.x;
  const int y = blockIdx.y * blockDim.y + threadIdx.y;

  if (x >= width || y >= height) {
    return;
  }

  float2 acc = make_float2(0.0f, 0.0f);
  int count = 0;
  for (int sy = max(0, y - radius); sy <= min(height - 1, y + radius); ++sy) {
    const float2* src_row = reinterpret_cast<const float2*>(reinterpret_cast<const char*>(src) + static_cast<size_t>(sy) * src_pitch);
    const float2 value = src_row[x];
    acc.x += value.x;
    acc.y += value.y;
    ++count;
  }

  float2* dst_row = reinterpret_cast<float2*>(reinterpret_cast<char*>(dst) + static_cast<size_t>(y) * dst_pitch);
  const float inv_count = count > 0 ? (1.0f / static_cast<float>(count)) : 0.0f;
  dst_row[x] = make_float2(acc.x * inv_count, acc.y * inv_count);
}

__global__ void vector_blur_kernel(const DeviceImages images, const Params params)
{
  const int x = blockIdx.x * blockDim.x + threadIdx.x;
  const int y = blockIdx.y * blockDim.y + threadIdx.y;

  if (x >= params.width || y >= params.height) {
    return;
  }

  const Float4 result = process_pixel_cuda(images, params, x, y);
  float4* row = reinterpret_cast<float4*>(reinterpret_cast<char*>(images.dst) + static_cast<size_t>(y) * images.dst_pitch);
  row[x] = make_float4(result.x, result.y, result.z, result.w);
}

#undef TVB_CUDA_INLINE
} // namespace

DeviceBuffers* create_device_buffers()
{
  return new DeviceBuffers();
}

void destroy_device_buffers(DeviceBuffers* buffers)
{
  if (!buffers) {
    return;
  }

  reset_buffers(buffers);
  if (buffers->stream) {
    cudaStreamDestroy(buffers->stream);
    buffers->stream = nullptr;
  }
  delete buffers;
}

bool is_cuda_available(std::string& gpu_name, std::string& error_message)
{
  int device_count = 0;
  cudaError_t status = cudaSetDevice(0);
  if (status != cudaSuccess) {
    error_message = cudaGetErrorString(status);
    return false;
  }

  status = cudaGetDeviceCount(&device_count);
  if (status != cudaSuccess) {
    error_message = cudaGetErrorString(status);
    return false;
  }

  if (device_count <= 0) {
    error_message = "No CUDA device detected.";
    return false;
  }

  cudaDeviceProp device_props;
  status = cudaGetDeviceProperties(&device_props, 0);
  if (status != cudaSuccess) {
    error_message = cudaGetErrorString(status);
    return false;
  }

  gpu_name = device_props.name;
  error_message.clear();
  return true;
}

bool run_cuda(
    DeviceBuffers* buffers,
    const float* src,
    const float* vec,
    float* dst,
    const Params& params,
    std::string& error_message)
{
  if (!buffers) {
    error_message = "CUDA buffers were not initialized.";
    return false;
  }

  error_message.clear();
  if (!check_cuda(cudaSetDevice(0), error_message, "cudaSetDevice")) {
    return false;
  }

  if (!buffers->stream) {
    if (!check_cuda(cudaStreamCreateWithFlags(&buffers->stream, cudaStreamNonBlocking), error_message, "cudaStreamCreateWithFlags")) {
      return false;
    }
  }

  if (!ensure_buffers(buffers, params, error_message)) {
    return false;
  }

  const size_t src_row_bytes = static_cast<size_t>(params.width) * sizeof(float4);
  const size_t vec_row_bytes = static_cast<size_t>(params.width) * sizeof(float2);
  const size_t dst_row_bytes = static_cast<size_t>(params.width) * sizeof(float4);

  if (!check_cuda(
          cudaMemcpy2DAsync(
              buffers->d_src,
              buffers->src_pitch,
              src,
              src_row_bytes,
              src_row_bytes,
              static_cast<size_t>(params.height),
              cudaMemcpyHostToDevice,
              buffers->stream),
          error_message,
          "cudaMemcpy2DAsync(src)")) {
    return false;
  }

  if (!check_cuda(
          cudaMemcpy2DAsync(
              buffers->d_vec,
              buffers->vec_pitch,
              vec,
              vec_row_bytes,
              vec_row_bytes,
              static_cast<size_t>(params.height),
              cudaMemcpyHostToDevice,
              buffers->stream),
          error_message,
          "cudaMemcpy2DAsync(vec)")) {
    return false;
  }

  if (params.vector_blur_radius > 0) {
    const dim3 blur_block(32, 8);
    const dim3 blur_grid(
        static_cast<unsigned int>((params.width + blur_block.x - 1) / blur_block.x),
        static_cast<unsigned int>((params.height + blur_block.y - 1) / blur_block.y));

    vector_blur_horizontal_kernel<<<blur_grid, blur_block, 0, buffers->stream>>>(
        buffers->d_vec,
        buffers->vec_pitch,
        buffers->d_vec_tmp,
        buffers->vec_tmp_pitch,
        params.width,
        params.height,
        params.vector_blur_radius);

    if (!check_cuda(cudaGetLastError(), error_message, "vector blur horizontal launch")) {
      return false;
    }

    vector_blur_vertical_kernel<<<blur_grid, blur_block, 0, buffers->stream>>>(
        buffers->d_vec_tmp,
        buffers->vec_tmp_pitch,
        buffers->d_vec,
        buffers->vec_pitch,
        params.width,
        params.height,
        params.vector_blur_radius);

    if (!check_cuda(cudaGetLastError(), error_message, "vector blur vertical launch")) {
      return false;
    }
  }

  {
    DeviceImages images{buffers->src_tex, buffers->vec_tex, buffers->d_dst, buffers->dst_pitch};
    const dim3 block(32, 8);
    const dim3 grid(
        static_cast<unsigned int>((params.width + block.x - 1) / block.x),
        static_cast<unsigned int>((params.height + block.y - 1) / block.y));
    cudaFuncSetCacheConfig(vector_blur_kernel, cudaFuncCachePreferL1);
    vector_blur_kernel<<<grid, block, 0, buffers->stream>>>(images, params);
  }

  if (!check_cuda(cudaGetLastError(), error_message, "kernel launch")) {
    return false;
  }

  if (!check_cuda(
          cudaMemcpy2DAsync(
              dst,
              dst_row_bytes,
              buffers->d_dst,
              buffers->dst_pitch,
              dst_row_bytes,
              static_cast<size_t>(params.height),
              cudaMemcpyDeviceToHost,
              buffers->stream),
          error_message,
          "cudaMemcpy2DAsync(dst)")) {
    return false;
  }

  if (!check_cuda(cudaStreamSynchronize(buffers->stream), error_message, "cudaStreamSynchronize")) {
    return false;
  }

  return error_message.empty();
}
} // namespace tvb
