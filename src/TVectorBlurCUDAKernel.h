#pragma once

#include <string>

#include "TVectorBlurShared.h"

namespace tvb
{
bool is_cuda_available(std::string& gpu_name, std::string& error_message);
struct DeviceBuffers;

DeviceBuffers* create_device_buffers();
void destroy_device_buffers(DeviceBuffers* buffers);

bool run_cuda(
    DeviceBuffers* buffers,
    const float* src,
    const float* vec,
    float* dst,
    const Params& params,
    std::string& error_message);
} // namespace tvb
