#pragma once

#include <cmath>

#ifdef __CUDACC__
#define TVB_HD __host__ __device__
#else
#define TVB_HD
#endif

namespace tvb
{
struct Float4
{
  float x;
  float y;
  float z;
  float w;
};

struct Params
{
  int width;
  int height;

  float distance;
  int samples;
  int min_samples;
  int adaptive;
  int direction;
  int trace_mode;
  float offset;

  float vec_multiply;
  float cos_rot;
  float sin_rot;
  int vec_normalize;
  int vector_blur_radius;

  float box_blur;
  int box_samples;

  int falloff_type;
  float falloff_strength;

  int chroma_mode;
  int chroma_invert;

  int blur_mode;
  float mix;

  int show_vectors;

  float fwd_r;
  float fwd_g;
  float fwd_b;

  float mask_mix;
  int invert_mask;
};

struct Images
{
  const float* src;
  const float* vec;
  const float* mask;
  float* dst;
};

TVB_HD inline int clampi(const int value, const int low, const int high)
{
  return value < low ? low : (value > high ? high : value);
}

TVB_HD inline float clampf(const float value, const float low, const float high)
{
  return value < low ? low : (value > high ? high : value);
}

TVB_HD inline float minf(const float a, const float b)
{
  return a < b ? a : b;
}

TVB_HD inline float maxf(const float a, const float b)
{
  return a > b ? a : b;
}

TVB_HD inline int src_index(const Params& p, const int x, const int y, const int ch)
{
  return ((y * p.width) + x) * 4 + ch;
}

TVB_HD inline int vec_index(const Params& p, const int x, const int y, const int ch)
{
  return ((y * p.width) + x) * 2 + ch;
}

TVB_HD inline int mask_index(const Params& p, const int x, const int y)
{
  return (y * p.width) + x;
}

TVB_HD inline float read_src_pixel(const Images& images, const Params& p, const int x, const int y, const int ch)
{
  const int cx = clampi(x, 0, p.width - 1);
  const int cy = clampi(y, 0, p.height - 1);
  return images.src[src_index(p, cx, cy, ch)];
}

TVB_HD inline void bilinear_weights(
    const float x,
    const float y,
    int& x0,
    int& y0,
    int& x1,
    int& y1,
    float& w00,
    float& w10,
    float& w01,
    float& w11)
{
  const float ix = floorf(x);
  const float iy = floorf(y);
  const float fx = x - ix;
  const float fy = y - iy;

  x0 = static_cast<int>(ix);
  y0 = static_cast<int>(iy);
  x1 = x0 + 1;
  y1 = y0 + 1;

  w00 = (1.0f - fx) * (1.0f - fy);
  w10 = fx * (1.0f - fy);
  w01 = (1.0f - fx) * fy;
  w11 = fx * fy;
}

TVB_HD inline float sample_src_channel(
    const Images& images,
    const Params& p,
    const int ch,
    const float x,
    const float y)
{
  int x0, y0, x1, y1;
  float w00, w10, w01, w11;
  bilinear_weights(x, y, x0, y0, x1, y1, w00, w10, w01, w11);

  return read_src_pixel(images, p, x0, y0, ch) * w00
    + read_src_pixel(images, p, x1, y0, ch) * w10
    + read_src_pixel(images, p, x0, y1, ch) * w01
    + read_src_pixel(images, p, x1, y1, ch) * w11;
}

TVB_HD inline void sample_vector_raw(
    const Images& images,
    const Params& p,
    const float x,
    const float y,
    float& out_u,
    float& out_v)
{
  int x0, y0, x1, y1;
  float w00, w10, w01, w11;
  bilinear_weights(x, y, x0, y0, x1, y1, w00, w10, w01, w11);

  const int cx0 = clampi(x0, 0, p.width - 1);
  const int cy0 = clampi(y0, 0, p.height - 1);
  const int cx1 = clampi(x1, 0, p.width - 1);
  const int cy1 = clampi(y1, 0, p.height - 1);

  out_u =
      images.vec[vec_index(p, cx0, cy0, 0)] * w00
    + images.vec[vec_index(p, cx1, cy0, 0)] * w10
    + images.vec[vec_index(p, cx0, cy1, 0)] * w01
    + images.vec[vec_index(p, cx1, cy1, 0)] * w11;

  out_v =
      images.vec[vec_index(p, cx0, cy0, 1)] * w00
    + images.vec[vec_index(p, cx1, cy0, 1)] * w10
    + images.vec[vec_index(p, cx0, cy1, 1)] * w01
    + images.vec[vec_index(p, cx1, cy1, 1)] * w11;
}

TVB_HD inline float sample_mask(const Images& images, const Params& p, const float x, const float y)
{
  if (!images.mask || p.mask_mix <= 0.0f) {
    return 0.0f;
  }

  int x0, y0, x1, y1;
  float w00, w10, w01, w11;
  bilinear_weights(x, y, x0, y0, x1, y1, w00, w10, w01, w11);

  const int cx0 = clampi(x0, 0, p.width - 1);
  const int cy0 = clampi(y0, 0, p.height - 1);
  const int cx1 = clampi(x1, 0, p.width - 1);
  const int cy1 = clampi(y1, 0, p.height - 1);

  float value =
      images.mask[mask_index(p, cx0, cy0)] * w00
    + images.mask[mask_index(p, cx1, cy0)] * w10
    + images.mask[mask_index(p, cx0, cy1)] * w01
    + images.mask[mask_index(p, cx1, cy1)] * w11;

  value = clampf(value, 0.0f, 1.0f);
  if (p.invert_mask) {
    value = 1.0f - value;
  }
  return value;
}

TVB_HD inline void read_vec(
    const Images& images,
    const Params& p,
    const float x,
    const float y,
    float& out_u,
    float& out_v)
{
  float raw_u;
  float raw_v;
  sample_vector_raw(images, p, x, y, raw_u, raw_v);

  const float mask_value = sample_mask(images, p, x, y);
  const float attenuation = 1.0f - (mask_value * clampf(p.mask_mix, 0.0f, 1.0f));
  raw_u *= attenuation;
  raw_v *= attenuation;

  if (p.vec_normalize) {
    const float len = sqrtf(raw_u * raw_u + raw_v * raw_v);
    if (len > 0.0001f) {
      raw_u /= len;
      raw_v /= len;
    }
  }

  const float rotated_u = raw_u * p.cos_rot - raw_v * p.sin_rot;
  const float rotated_v = raw_u * p.sin_rot + raw_v * p.cos_rot;

  out_u = rotated_u * p.vec_multiply;
  out_v = rotated_v * p.vec_multiply;
}

TVB_HD inline float get_falloff(const Params& p, const float t)
{
  if (p.falloff_type == 0 || p.falloff_strength <= 0.0f) {
    return 1.0f;
  }

  if (p.falloff_type == 1) {
    return expf(-t * t * p.falloff_strength * 4.0f);
  }

  if (p.falloff_type == 2) {
    const float value = 1.0f - t * p.falloff_strength;
    return value < 0.0f ? 0.0f : value;
  }

  return 1.0f;
}

TVB_HD inline float sample_src_box(
    const Images& images,
    const Params& p,
    const int ch,
    const float x,
    const float y,
    const float perp_x,
    const float perp_y)
{
  if (p.box_blur <= 0.001f || p.box_samples <= 0) {
    return sample_src_channel(images, p, ch, x, y);
  }

  float acc = 0.0f;
  const int total = p.box_samples * 2 + 1;

  for (int b = -p.box_samples; b <= p.box_samples; ++b) {
    const float t = static_cast<float>(b) / static_cast<float>(p.box_samples);
    acc += sample_src_channel(
        images,
        p,
        ch,
        x + perp_x * t * p.box_blur,
        y + perp_y * t * p.box_blur);
  }

  return acc / static_cast<float>(total);
}

TVB_HD inline void trace_channel(
    const Images& images,
    const Params& p,
    const int ch,
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
  read_vec(images, p, static_cast<float>(px), static_cast<float>(py), orig_u, orig_v);

  const float vec_len = sqrtf(orig_u * orig_u + orig_v * orig_v);
  float perp_x = 0.0f;
  float perp_y = 0.0f;

  if (vec_len > 0.0001f) {
    perp_x = -orig_v / vec_len;
    perp_y = orig_u / vec_len;
  }

  const float start_x = static_cast<float>(px) + orig_u * p.offset * dir_sign;
  const float start_y = static_cast<float>(py) + orig_v * p.offset * dir_sign;

  float cur_x = start_x;
  float cur_y = start_y;

  for (int i = 1; i <= steps; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(steps);

    if (p.trace_mode == 0) {
      cur_x = start_x + orig_u * t * dist * dir_sign;
      cur_y = start_y + orig_v * t * dist * dir_sign;
    } else {
      float vec_u;
      float vec_v;
      read_vec(images, p, cur_x, cur_y, vec_u, vec_v);

      const float step_dist = (dist / static_cast<float>(steps)) * dir_sign;
      cur_x += vec_u * step_dist;
      cur_y += vec_v * step_dist;

      const float local_len = sqrtf(vec_u * vec_u + vec_v * vec_v);
      if (local_len > 0.0001f) {
        perp_x = -vec_v / local_len;
        perp_y = vec_u / local_len;
      }
    }

    float sample = sample_src_box(images, p, ch, cur_x, cur_y, perp_x, perp_y);
    const float weight = get_falloff(p, t);
    sample *= weight;

    if (p.blur_mode == 0) {
      acc += sample;
      cnt += weight;
    } else if (p.blur_mode == 1) {
      acc = minf(acc, sample);
    } else if (p.blur_mode == 2) {
      acc = maxf(acc, sample);
    }
  }
}

TVB_HD inline int get_adaptive_steps(const Images& images, const Params& p, const int px, const int py)
{
  float vec_u;
  float vec_v;
  read_vec(images, p, static_cast<float>(px), static_cast<float>(py), vec_u, vec_v);
  const float vec_len = sqrtf(vec_u * vec_u + vec_v * vec_v);

  int needed = static_cast<int>(vec_len * p.distance + 0.5f);
  if (needed < p.min_samples) {
    return p.min_samples;
  }
  if (needed > p.samples) {
    return p.samples;
  }
  return needed;
}

TVB_HD inline Float4 process_pixel(const Images& images, const Params& p, const int px, const int py)
{
  Float4 center;
  center.x = read_src_pixel(images, p, px, py, 0);
  center.y = read_src_pixel(images, p, px, py, 1);
  center.z = read_src_pixel(images, p, px, py, 2);
  center.w = read_src_pixel(images, p, px, py, 3);

  if (p.show_vectors) {
    float u;
    float v;
    read_vec(images, p, static_cast<float>(px), static_cast<float>(py), u, v);
    return {u, v, 0.0f, 1.0f};
  }

  if (p.mix <= 0.0f || p.distance <= 0.001f || p.samples <= 1) {
    return center;
  }

  const int actual_steps = p.adaptive ? get_adaptive_steps(images, p, px, py) : p.samples;
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

  if (p.direction == 0) {
    dist_fwd_r = p.distance * (p.fwd_r * 2.0f);
    dist_bwd_r = 0.0f;
    dist_fwd_g = p.distance * (p.fwd_g * 2.0f);
    dist_bwd_g = 0.0f;
    dist_fwd_b = p.distance * (p.fwd_b * 2.0f);
    dist_bwd_b = 0.0f;
    dist_fwd_a = p.distance;
    dist_bwd_a = 0.0f;

    steps_fwd_r = steps_fwd_g = steps_fwd_b = steps_fwd_a = total_steps;
    steps_bwd_r = steps_bwd_g = steps_bwd_b = steps_bwd_a = 0;
  } else if (p.direction == 1) {
    dist_fwd_r = 0.0f;
    dist_bwd_r = p.distance * ((1.0f - p.fwd_r) * 2.0f);
    dist_fwd_g = 0.0f;
    dist_bwd_g = p.distance * ((1.0f - p.fwd_g) * 2.0f);
    dist_fwd_b = 0.0f;
    dist_bwd_b = p.distance * ((1.0f - p.fwd_b) * 2.0f);
    dist_fwd_a = 0.0f;
    dist_bwd_a = p.distance;

    steps_fwd_r = steps_fwd_g = steps_fwd_b = steps_fwd_a = 0;
    steps_bwd_r = steps_bwd_g = steps_bwd_b = steps_bwd_a = total_steps;
  } else {
    dist_fwd_r = p.distance * p.fwd_r;
    dist_bwd_r = p.distance * (1.0f - p.fwd_r);
    dist_fwd_g = p.distance * p.fwd_g;
    dist_bwd_g = p.distance * (1.0f - p.fwd_g);
    dist_fwd_b = p.distance * p.fwd_b;
    dist_bwd_b = p.distance * (1.0f - p.fwd_b);
    dist_fwd_a = p.distance * 0.5f;
    dist_bwd_a = p.distance * 0.5f;

    steps_fwd_r = static_cast<int>(static_cast<float>(total_steps) * p.fwd_r + 0.5f);
    steps_bwd_r = total_steps - steps_fwd_r;
    steps_fwd_g = static_cast<int>(static_cast<float>(total_steps) * p.fwd_g + 0.5f);
    steps_bwd_g = total_steps - steps_fwd_g;
    steps_fwd_b = static_cast<int>(static_cast<float>(total_steps) * p.fwd_b + 0.5f);
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

  const float center_w = get_falloff(p, 0.0f);

  float acc_r = center.x * center_w;
  float cnt_r = center_w;
  trace_channel(images, p, 0, px, py, dist_fwd_r, 1.0f, steps_fwd_r, acc_r, cnt_r);
  trace_channel(images, p, 0, px, py, dist_bwd_r, -1.0f, steps_bwd_r, acc_r, cnt_r);

  float acc_g = center.y * center_w;
  float cnt_g = center_w;
  trace_channel(images, p, 1, px, py, dist_fwd_g, 1.0f, steps_fwd_g, acc_g, cnt_g);
  trace_channel(images, p, 1, px, py, dist_bwd_g, -1.0f, steps_bwd_g, acc_g, cnt_g);

  float acc_b = center.z * center_w;
  float cnt_b = center_w;
  trace_channel(images, p, 2, px, py, dist_fwd_b, 1.0f, steps_fwd_b, acc_b, cnt_b);
  trace_channel(images, p, 2, px, py, dist_bwd_b, -1.0f, steps_bwd_b, acc_b, cnt_b);

  float acc_a = center.w * center_w;
  float cnt_a = center_w;
  trace_channel(images, p, 3, px, py, dist_fwd_a, 1.0f, steps_fwd_a, acc_a, cnt_a);
  trace_channel(images, p, 3, px, py, dist_bwd_a, -1.0f, steps_bwd_a, acc_a, cnt_a);

  Float4 result;
  if (p.blur_mode == 0) {
    result = {
      cnt_r > 0.0f ? acc_r / cnt_r : center.x,
      cnt_g > 0.0f ? acc_g / cnt_g : center.y,
      cnt_b > 0.0f ? acc_b / cnt_b : center.z,
      cnt_a > 0.0f ? acc_a / cnt_a : center.w
    };
  } else {
    result = {acc_r, acc_g, acc_b, acc_a};
  }

  if (p.mix >= 1.0f) {
    return result;
  }

  return {
    center.x + (result.x - center.x) * p.mix,
    center.y + (result.y - center.y) * p.mix,
    center.z + (result.z - center.z) * p.mix,
    center.w + (result.w - center.w) * p.mix
  };
}
} // namespace tvb

#undef TVB_HD
