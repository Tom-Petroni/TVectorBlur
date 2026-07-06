#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  check_linux_runner.sh --env-file /path/to/tvectorblur-runner.env

Validates the Linux self-hosted runner prerequisites for TVectorBlur:
  - NVIDIA driver visibility
  - CUDA toolkit / nvcc
  - cmake, ninja, git, python3
  - declared Nuke roots and executables
EOF
}

ENV_FILE=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --env-file)
      ENV_FILE="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

if [[ -z "${ENV_FILE}" || ! -f "${ENV_FILE}" ]]; then
  echo "Missing --env-file or file not found." >&2
  usage >&2
  exit 1
fi

set -a
# shellcheck disable=SC1090
source "${ENV_FILE}"
set +a

require_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "Missing command: $1" >&2
    exit 1
  fi
}

for cmd in git python3 cmake ninja nvidia-smi nvcc; do
  require_cmd "$cmd"
done

echo "== Toolchain =="
python3 --version
cmake --version | head -n 1
ninja --version
nvcc --version | tail -n 1
nvidia-smi --query-gpu=name,driver_version --format=csv,noheader

echo
echo "== Nuke installs =="
found_nuke=0
while IFS='=' read -r key value; do
  case "${key}" in
    NUKE_*_ROOT)
      found_nuke=1
      if [[ ! -d "${value}" ]]; then
        echo "Missing Nuke root: ${key}=${value}" >&2
        exit 1
      fi
      echo "${key}=${value}"
      ;;
    NUKE_*_EXECUTABLE)
      if [[ ! -x "${value}" ]]; then
        echo "Missing Nuke executable: ${key}=${value}" >&2
        exit 1
      fi
      echo "${key}=${value}"
      ;;
  esac
done < "${ENV_FILE}"

if [[ "${found_nuke}" -ne 1 ]]; then
  echo "No NUKE_*_ROOT entries found in ${ENV_FILE}" >&2
  exit 1
fi

echo
echo "Linux runner check OK"
