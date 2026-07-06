#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "Usage: $0 <NUKE_ROOT> [build_type] [cuda_architectures]" >&2
  exit 1
fi

NUKE_ROOT="$1"
BUILD_TYPE="${2:-Release}"
CUDA_ARCHITECTURES="${3:-native}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

if [[ "${NUKE_ROOT}" =~ ([0-9]+\.[0-9]+) ]]; then
  NUKE_VERSION="${BASH_REMATCH[1]}"
else
  echo "Unable to detect Nuke version from path '${NUKE_ROOT}'." >&2
  exit 1
fi

BUILD_DIR="${PROJECT_ROOT}/build/linux-nuke-${NUKE_VERSION}"

if [[ -f "${BUILD_DIR}/CMakeCache.txt" ]] && ! grep -q "${PROJECT_ROOT}" "${BUILD_DIR}/CMakeCache.txt"; then
  rm -rf "${BUILD_DIR}"
fi

cmake \
  -S "${PROJECT_ROOT}" \
  -B "${BUILD_DIR}" \
  -G Ninja \
  -DTVECTORBLUR_NUKE_ROOT="${NUKE_ROOT}" \
  -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
  -DCMAKE_CUDA_ARCHITECTURES="${CUDA_ARCHITECTURES}"

cmake --build "${BUILD_DIR}" --config "${BUILD_TYPE}" --target TVectorBlurCUDA

python3 "${SCRIPT_DIR}/sync_publish_bins.py"
