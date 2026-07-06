"""Plugin loader and binary resolver."""

from __future__ import annotations

import logging
import os
import platform

import nuke  # ty:ignore[unresolved-import]

try:
    from TVectorBlur._consts import (
        INSTALLATION_PATH,
        NODE_CLASS_NAME,
        PLUGIN_BIN_DIRECTORY,
        PLUGIN_BINARY_PATH_ENV_VAR,
        PLUGIN_LOADED_ENV_VAR,
        normalized_path,
    )
except Exception:
    from _consts import (
        INSTALLATION_PATH,
        NODE_CLASS_NAME,
        PLUGIN_BIN_DIRECTORY,
        PLUGIN_BINARY_PATH_ENV_VAR,
        PLUGIN_LOADED_ENV_VAR,
        normalized_path,
    )

logger = logging.getLogger(__name__)

class PluginNotFoundError(Exception):
    pass


class PluginLoadError(Exception):
    pass


class UnsupportedSystemError(Exception):
    pass


def _get_nuke_version() -> str:
    return f"{nuke.NUKE_VERSION_MAJOR}.{nuke.NUKE_VERSION_MINOR}"


def _get_operating_system_name() -> str:
    operating_system = platform.system().lower()

    if "linux" in operating_system:
        return "linux"
    if "windows" in operating_system:
        return "windows"
    if "darwin" in operating_system:
        return "macos"

    raise UnsupportedSystemError(f"System '{operating_system}' is not supported.")


def _library_filename() -> str:
    operating_system = _get_operating_system_name()
    if operating_system == "windows":
        return f"{NODE_CLASS_NAME}.dll"
    if operating_system == "linux":
        return f"{NODE_CLASS_NAME}.so"
    return f"{NODE_CLASS_NAME}.dylib"


def _is_minor_version_folder(name: str) -> bool:
    parts = name.split(".")
    return len(parts) == 2 and all(part.isdigit() for part in parts)


def _resolve_version_folder() -> str:
    requested = _get_nuke_version()
    plugin_bin_root = os.path.join(INSTALLATION_PATH, PLUGIN_BIN_DIRECTORY)

    if os.path.isdir(os.path.join(plugin_bin_root, requested)):
        return requested

    if not os.path.isdir(plugin_bin_root):
        return requested

    try:
        available = [
            entry
            for entry in os.listdir(plugin_bin_root)
            if _is_minor_version_folder(entry)
            and os.path.isdir(os.path.join(plugin_bin_root, entry))
        ]
    except OSError:
        return requested

    try:
        requested_major, requested_minor = (int(part) for part in requested.split(".", 1))
    except ValueError:
        return requested

    same_major = []
    for entry in available:
        major, minor = (int(part) for part in entry.split(".", 1))
        if major == requested_major:
            same_major.append((minor, entry))

    if not same_major:
        return requested

    lower_or_equal = [entry for minor, entry in same_major if minor <= requested_minor]
    if lower_or_equal:
        selected = max(lower_or_equal, key=lambda version: int(version.split(".", 1)[1]))
    else:
        selected = min(
            (entry for _, entry in same_major),
            key=lambda version: int(version.split(".", 1)[1]),
        )

    logger.warning(
        "%s binary folder '%s' not found, using '%s' fallback.",
        NODE_CLASS_NAME,
        requested,
        selected,
    )
    return selected


def _legacy_arch_folders() -> list[str]:
    architecture = (platform.machine() or platform.processor() or "").strip().lower()
    if architecture in {"amd64", "x64", "x86_64", "x86-64", "em64t"}:
        return ["x86_64"]
    if architecture in {"arm64", "aarch64"}:
        return ["aarch64", "x86_64"]
    return []


def _candidate_plugin_paths() -> list[str]:
    version_folder = _resolve_version_folder()
    os_name = _get_operating_system_name()
    base_path = os.path.join(INSTALLATION_PATH, PLUGIN_BIN_DIRECTORY, version_folder, os_name)
    candidates = [normalized_path(base_path)]
    for arch_folder in _legacy_arch_folders():
        candidates.append(normalized_path(os.path.join(base_path, arch_folder)))
    return candidates


def _build_binary_path(plugin_path: str) -> str:
    return normalized_path(os.path.join(plugin_path, _library_filename()))


def _is_plugin_path_registered(plugin_path: str) -> bool:
    target = normalized_path(plugin_path)
    try:
        return any(normalized_path(path) == target for path in nuke.pluginPath())
    except Exception:
        return False


def _ensure_plugin_path_registered(plugin_path: str) -> None:
    if _is_plugin_path_registered(plugin_path):
        return
    nuke.pluginAddPath(str(plugin_path))  # ty:ignore[unresolved-attribute]


def _is_node_class_available() -> bool:
    return hasattr(nuke, "nodes") and hasattr(nuke.nodes, NODE_CLASS_NAME)


def _load_binary(binary_path: str) -> None:
    try:
        nuke.load(binary_path)  # ty:ignore[unresolved-attribute]
    except Exception as error:
        raise PluginLoadError(
            f"Unable to load '{NODE_CLASS_NAME}' from '{binary_path}': {error}"
        ) from error

    if not _is_node_class_available():
        raise PluginLoadError(
            f"Binary '{binary_path}' loaded but node class '{NODE_CLASS_NAME}' is unavailable."
        )


def ensure_node_class_loaded() -> str:
    plugin_path = next((path for path in _candidate_plugin_paths() if os.path.isdir(path)), "")
    if not plugin_path:
        raise PluginNotFoundError(
            (
                f"{NODE_CLASS_NAME} is installed, but this Nuke version "
                f"'{nuke.NUKE_VERSION_STRING}' is not available in this package."
            )
        )

    binary_path = _build_binary_path(plugin_path)
    if not os.path.isfile(binary_path):
        raise PluginNotFoundError(f"{NODE_CLASS_NAME} binary was not found at '{binary_path}'.")

    _ensure_plugin_path_registered(plugin_path)
    _load_binary(binary_path)
    return plugin_path


def add_plugin_path() -> str:
    os.environ[PLUGIN_LOADED_ENV_VAR] = "0"
    os.environ.pop(PLUGIN_BINARY_PATH_ENV_VAR, None)

    plugin_path = ensure_node_class_loaded()
    os.environ[PLUGIN_LOADED_ENV_VAR] = "1"
    os.environ[PLUGIN_BINARY_PATH_ENV_VAR] = plugin_path
    return plugin_path


def add_plugin_path_safe() -> bool:
    try:
        plugin_path = add_plugin_path()
        logger.info("%s plugin loaded successfully from '%s'.", NODE_CLASS_NAME, plugin_path)
        return True
    except Exception:
        logger.exception("%s plugin loading failed.", NODE_CLASS_NAME)
        return False
