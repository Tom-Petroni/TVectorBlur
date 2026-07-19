#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 3 || $# -gt 4 ]]; then
    echo "Usage: $0 <workspace-root> <nuke-version> <linux> [cuda-architectures]" >&2
    exit 1
fi

WORKSPACE_ROOT="$1"
NUKE_VERSION="$2"
TARGET_PLATFORM="$3"
CUDA_ARCHITECTURES="${4:-75;86;89;90}"

NODE_KEY="TVectorBlur"
PACKAGE_OS="linux"
OUTPUT_NAME="lib${NODE_KEY}.so"

if [[ "${TARGET_PLATFORM}" != "linux" ]]; then
    echo "TVectorBlur CUDA Docker builds currently support linux only." >&2
    echo "Run work/scripts/build_with_nukedockerbuild.ps1 from Windows for the hybrid windows+linux flow." >&2
    exit 1
fi

export CUDA_PATH="${CUDA_PATH:-/usr/local/cuda}"
if [[ ! -x "${CUDA_PATH}/bin/nvcc" ]]; then
    echo "CUDA toolkit not found in container: expected ${CUDA_PATH}/bin/nvcc" >&2
    exit 1
fi

if ! command -v cmake >/dev/null 2>&1; then
    echo "cmake was not found in the builder image." >&2
    exit 1
fi

if ! command -v ninja >/dev/null 2>&1; then
    echo "ninja was not found in the builder image." >&2
    exit 1
fi

export PATH="${CUDA_PATH}/bin:${PATH}"
export LD_LIBRARY_PATH="${CUDA_PATH}/lib64:${LD_LIBRARY_PATH:-}"
export TVECTORBLUR_CUDA_ARCHITECTURES="${CUDA_ARCHITECTURES}"

NUKE_INSTALL_ROOT="/usr/local/nuke_install"
NUKE_ROOT="/tmp/Nuke${NUKE_VERSION}"
ln -sfn "${NUKE_INSTALL_ROOT}" "${NUKE_ROOT}"

if [[ ! -d "${NUKE_ROOT}/include" || ! -f "${NUKE_ROOT}/libDDImage.so" ]]; then
    echo "Nuke Linux SDK files are missing from ${NUKE_ROOT}" >&2
    exit 1
fi

cd "${WORKSPACE_ROOT}"

bash scripts/build_linux.sh "${NUKE_ROOT}" Release "${CUDA_ARCHITECTURES}"

SOURCE_BINARY="${WORKSPACE_ROOT}/${NODE_KEY}/bin/${NUKE_VERSION}/${PACKAGE_OS}/x86_64/${OUTPUT_NAME}"
if [[ ! -f "${SOURCE_BINARY}" ]]; then
    echo "Expected binary was not produced: ${SOURCE_BINARY}" >&2
    exit 1
fi

echo "Published CUDA binary: ${SOURCE_BINARY}"
