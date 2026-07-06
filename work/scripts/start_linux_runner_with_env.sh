#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "Usage: $0 <runner_dir> <env_file>" >&2
  exit 1
fi

RUNNER_DIR="$1"
ENV_FILE="$2"

if [[ ! -d "${RUNNER_DIR}" ]]; then
  echo "Runner directory not found: ${RUNNER_DIR}" >&2
  exit 1
fi

if [[ ! -f "${ENV_FILE}" ]]; then
  echo "Runner env file not found: ${ENV_FILE}" >&2
  exit 1
fi

set -a
# shellcheck disable=SC1090
source "${ENV_FILE}"
set +a

cd "${RUNNER_DIR}"
exec ./run.sh
