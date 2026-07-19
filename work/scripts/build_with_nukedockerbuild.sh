#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: build_with_nukedockerbuild.sh [options]

Options:
  --repo-root <path>                Absolute repo root for TVectorBlur
  --nukedockerbuild-root <path>     Absolute repo root for NukeDockerBuild
  --versions <csv>                  Comma-separated Nuke versions (default: 17.0)
  --platforms <csv>                 Comma-separated platforms: linux
  --cuda-image <ref>                CUDA devel image for Linux CUDA builds
  --cuda-architectures <list>       CMake CUDA architectures (default: 75;86;89;90)
  --skip-base-image-build           Reuse existing nukedockerbuild:* images only
  --rebuild-base-image              Force rebuild of nukedockerbuild:* images
  --rebuild-builder-image           Force rebuild of tvectorblur-builder:* images
  --help                            Show this help
EOF
}

prepare_docker_command() {
    if command -v docker >/dev/null 2>&1; then
        return 0
    fi

    local docker_exe="/mnt/c/Program Files/Docker/Docker/resources/bin/docker.exe"
    if [[ ! -x "${docker_exe}" ]]; then
        echo "Docker CLI was not found in WSL, and Docker Desktop's docker.exe is unavailable." >&2
        return 1
    fi

    local wrapper_dir
    wrapper_dir="$(mktemp -d)"
    cat >"${wrapper_dir}/docker" <<EOF
#!/usr/bin/env bash
exec "${docker_exe}" "\$@"
EOF
    chmod +x "${wrapper_dir}/docker"
    export PATH="${wrapper_dir}:${PATH}"
}

normalize_shell_scripts() {
    local root="$1"
    shopt -s nullglob
    local script
    for script in "${root}"/*.sh "${root}"/scripts/*.sh; do
        perl -0pi -e 's/\r\n/\n/g' "${script}"
    done
    local dockerfile
    for dockerfile in "${root}"/dockerfiles/*/*/Dockerfile; do
        perl -0pi -e 's/\r\n/\n/g' "${dockerfile}"
    done
    shopt -u nullglob
}

docker_image_exists() {
    local image_name="$1"
    docker image inspect "${image_name}" >/dev/null 2>&1
}

builder_image_key() {
    local context_dir="$1"
    local base_image="$2"
    local dockerfile_path="$3"
    local cuda_image="$4"
    local base_image_id
    base_image_id="$(docker image inspect --format '{{.Id}}' "${base_image}")"

    {
        printf '%s\n' "${base_image_id}"
        printf '%s\n' "${dockerfile_path}"
        printf '%s\n' "${cuda_image}"
        (
            cd "${context_dir}"
            find . -type f -print0 | sort -z | xargs -0 sha256sum
        )
    } | sha256sum | cut -c1-12
}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEFAULT_REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
DEFAULT_NDB_ROOT="$(cd "${DEFAULT_REPO_ROOT}/../NukeDockerBuild" 2>/dev/null && pwd || true)"

REPO_ROOT="${DEFAULT_REPO_ROOT}"
NUKEDOCKERBUILD_ROOT="${DEFAULT_NDB_ROOT}"
VERSIONS_CSV="17.0"
PLATFORMS_CSV="linux"
CUDA_IMAGE="nvidia/cuda:12.6.3-devel-ubi8"
CUDA_ARCHITECTURES="75;86;89;90"
SKIP_BASE_IMAGE_BUILD=false
REBUILD_BASE_IMAGE=false
REBUILD_BUILDER_IMAGE=false

while [[ $# -gt 0 ]]; do
    case "$1" in
        --repo-root)
            REPO_ROOT="$2"
            shift 2
            ;;
        --nukedockerbuild-root)
            NUKEDOCKERBUILD_ROOT="$2"
            shift 2
            ;;
        --versions)
            VERSIONS_CSV="$2"
            shift 2
            ;;
        --platforms)
            PLATFORMS_CSV="$2"
            shift 2
            ;;
        --cuda-image)
            CUDA_IMAGE="$2"
            shift 2
            ;;
        --cuda-architectures)
            CUDA_ARCHITECTURES="$2"
            shift 2
            ;;
        --skip-base-image-build)
            SKIP_BASE_IMAGE_BUILD=true
            shift
            ;;
        --rebuild-base-image)
            REBUILD_BASE_IMAGE=true
            shift
            ;;
        --rebuild-builder-image)
            REBUILD_BUILDER_IMAGE=true
            shift
            ;;
        --help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            usage
            exit 1
            ;;
    esac
done

if [[ ! -d "${REPO_ROOT}" ]]; then
    echo "Missing repo root: ${REPO_ROOT}" >&2
    exit 1
fi

if [[ ! -d "${NUKEDOCKERBUILD_ROOT}" ]]; then
    echo "Missing NukeDockerBuild root: ${NUKEDOCKERBUILD_ROOT}" >&2
    exit 1
fi

normalize_shell_scripts "${NUKEDOCKERBUILD_ROOT}"
normalize_shell_scripts "${REPO_ROOT}/work"
prepare_docker_command

if ! docker info >/dev/null 2>&1; then
    echo "Docker daemon is not reachable. Start Docker Desktop first." >&2
    exit 1
fi

IFS=',' read -r -a RAW_VERSIONS <<< "${VERSIONS_CSV// /}"
IFS=',' read -r -a RAW_PLATFORMS <<< "${PLATFORMS_CSV// /}"

VERSIONS=()
for version in "${RAW_VERSIONS[@]}"; do
    [[ -n "${version}" ]] && VERSIONS+=("${version}")
done

PLATFORMS=()
for platform in "${RAW_PLATFORMS[@]}"; do
    [[ -n "${platform}" ]] && PLATFORMS+=("${platform}")
done

if [[ ${#VERSIONS[@]} -eq 0 ]]; then
    echo "No Nuke versions requested." >&2
    exit 1
fi

if [[ ${#PLATFORMS[@]} -eq 0 ]]; then
    echo "No target platforms requested." >&2
    exit 1
fi

CUDA_ARCHITECTURES="${CUDA_ARCHITECTURES//,/;}"

BUILDER_CONTEXT="${REPO_ROOT}/work/docker/nukedockerbuild-builder"
BUILDER_DOCKERFILE="${BUILDER_CONTEXT}/Dockerfile.cuda"
CONTAINER_SCRIPT="/workspace/work/docker/nukedockerbuild-builder/build-node.sh"

for version in "${VERSIONS[@]}"; do
    for platform in "${PLATFORMS[@]}"; do
        if [[ "${platform}" != "linux" ]]; then
            echo "TVectorBlur Docker builds support linux only; use the PowerShell wrapper for windows." >&2
            exit 1
        fi

        BASE_IMAGE="nukedockerbuild:${version}-${platform}"

        if [[ "${REBUILD_BASE_IMAGE}" == "true" ]] || ! docker_image_exists "${BASE_IMAGE}"; then
            if [[ "${SKIP_BASE_IMAGE_BUILD}" == "true" ]] && [[ "${REBUILD_BASE_IMAGE}" != "true" ]]; then
                echo "Base image ${BASE_IMAGE} is missing and --skip-base-image-build was requested." >&2
                exit 1
            fi

            echo "Building base image ${BASE_IMAGE}"
            (
                cd "${NUKEDOCKERBUILD_ROOT}"
                bash ./build.sh "${version}" "${platform}" --skip-load
            )
        else
            echo "Reusing base image ${BASE_IMAGE}"
        fi

        BUILDER_KEY="$(builder_image_key "${BUILDER_CONTEXT}" "${BASE_IMAGE}" "${BUILDER_DOCKERFILE}" "${CUDA_IMAGE}")"
        BUILDER_IMAGE="tvectorblur-builder:${version}-${platform}-cuda-${BUILDER_KEY}"
        BUILDER_IMAGE_ALIAS="tvectorblur-builder:${version}-${platform}-cuda"

        if [[ "${REBUILD_BUILDER_IMAGE}" == "true" ]] || ! docker_image_exists "${BUILDER_IMAGE}"; then
            echo "Building builder image ${BUILDER_IMAGE}"
            docker build \
                --build-arg "BASE_IMAGE=${BASE_IMAGE}" \
                --build-arg "CUDA_IMAGE=${CUDA_IMAGE}" \
                -t "${BUILDER_IMAGE}" \
                -t "${BUILDER_IMAGE_ALIAS}" \
                -f "${BUILDER_DOCKERFILE}" \
                "${BUILDER_CONTEXT}"
        else
            echo "Reusing builder image ${BUILDER_IMAGE}"
        fi

        echo "Compiling TVectorBlur CUDA build for Nuke ${version} on ${platform}"
        docker run --rm \
            -v "${REPO_ROOT}:/workspace" \
            -v "${NUKEDOCKERBUILD_ROOT}:/nukedockerbuild-src:ro" \
            "${BUILDER_IMAGE}" \
            bash "${CONTAINER_SCRIPT}" "/workspace/work" "${version}" "${platform}" "${CUDA_ARCHITECTURES}"
    done
done

echo "NukeDockerBuild TVectorBlur CUDA compilation completed."
