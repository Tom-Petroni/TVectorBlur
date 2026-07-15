"""Validate that a packaged TCollection node imports and resolves binaries without Nuke."""

from __future__ import annotations

import argparse
import importlib
import os
import platform
import shutil
import sys
from pathlib import Path
from types import ModuleType, SimpleNamespace

SUPPORTED_OS_NAMES = {"windows", "linux"}


class FakeNuke(ModuleType):
    """Minimal Nuke stub used to validate plugin packages in CI."""

    def __init__(self, node_class: str, version: str) -> None:
        super().__init__("nuke")
        major, minor = version.split(".", 1)
        self.NUKE_VERSION_MAJOR = int(major)
        self.NUKE_VERSION_MINOR = int(minor)
        self.NUKE_VERSION_STRING = f"{version}v1"
        self._node_class = node_class
        self._plugin_paths: list[str] = []
        self._loaded_binaries: list[str] = []
        self.nodes = SimpleNamespace()

    def pluginAddPath(self, path: str) -> None:  # noqa: N802
        normalized = path.replace("\\", "/")
        if normalized not in self._plugin_paths:
            self._plugin_paths.append(normalized)

    def pluginAppendPath(self, path: str) -> None:  # noqa: N802
        self.pluginAddPath(path)

    def pluginPath(self) -> list[str]:  # noqa: N802
        return list(self._plugin_paths)

    def load(self, target: str) -> None:
        normalized = target.replace("\\", "/")
        if os.path.isfile(target):
            self._loaded_binaries.append(normalized)
            setattr(self.nodes, self._node_class, object())
            return

        if normalized == self._node_class:
            if not self._loaded_binaries:
                raise RuntimeError(f"Cannot load '{target}' before a binary path is known.")
            setattr(self.nodes, self._node_class, object())
            return

        raise RuntimeError(f"Unknown fake load target: {target}")

    def tprint(self, *args, **kwargs) -> None:
        print(*args, **kwargs)

    def addBeforeScriptLoad(self, callback) -> None:  # noqa: N802
        self._before_script_load = callback

    def addOnScriptNew(self, callback) -> None:  # noqa: N802
        self._on_script_new = callback

    def addOnScriptLoad(self, callback) -> None:  # noqa: N802
        self._on_script_load = callback

    def addOnCreate(self, callback, nodeClass=None) -> None:  # noqa: N802, N803
        self._on_create = (callback, nodeClass)


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--package-root", required=True)
    parser.add_argument("--package-name", required=True)
    parser.add_argument("--node-class", required=True)
    parser.add_argument(
        "--include-os",
        action="append",
        dest="included_os_names",
        help="Optional OS filter. Repeat for multiple values.",
    )
    return parser.parse_args()


def _version_key(version: str) -> tuple[int, int]:
    major, minor = version.split(".", 1)
    return (int(major), int(minor))


def _discover_targets(
    package_root: Path,
    package_name: str,
    included_os_names: set[str],
) -> list[tuple[str, str, str]]:
    bin_root = package_root / package_name / "bin"
    if not bin_root.is_dir():
        raise RuntimeError(f"Missing package bin directory: {bin_root}")

    targets: list[tuple[str, str, str]] = []
    version_dirs = []
    for candidate in bin_root.iterdir():
        if not candidate.is_dir():
            continue
        parts = candidate.name.split(".", 1)
        if len(parts) != 2 or not parts[0].isdigit() or not parts[1].isdigit():
            continue
        version_dirs.append(candidate)

    for version_dir in sorted(version_dirs, key=lambda path: _version_key(path.name)):
        for os_dir in sorted(version_dir.iterdir()):
            if not os_dir.is_dir() or os_dir.name not in included_os_names:
                continue
            for arch_dir in sorted(os_dir.iterdir()):
                if arch_dir.is_dir():
                    targets.append((version_dir.name, os_dir.name, arch_dir.name))

    if not targets:
        raise RuntimeError(f"No version/os/arch targets found under {bin_root}")
    return targets


def _clear_package_modules(package_name: str) -> None:
    for module_name in list(sys.modules):
        if module_name == package_name or module_name.startswith(f"{package_name}."):
            del sys.modules[module_name]


def _clear_package_env(package_name: str) -> None:
    prefix = package_name.upper()
    for key in list(os.environ):
        if key.startswith(prefix):
            os.environ.pop(key, None)


def _patch_platform(os_name: str, arch_name: str):
    original_system = platform.system
    original_machine = platform.machine
    original_processor = platform.processor

    def fake_system() -> str:
        if os_name == "windows":
            return "Windows"
        if os_name == "linux":
            return "Linux"
        if os_name == "macos":
            return "Darwin"
        return os_name

    def fake_machine() -> str:
        return arch_name

    platform.system = fake_system
    platform.machine = fake_machine
    platform.processor = fake_machine
    return original_system, original_machine, original_processor


def _restore_platform(originals) -> None:
    original_system, original_machine, original_processor = originals
    platform.system = original_system
    platform.machine = original_machine
    platform.processor = original_processor


def _expected_binary_name(node_class: str, os_name: str) -> str:
    if os_name == "windows":
        return f"{node_class}.dll"
    if os_name == "linux":
        return f"lib{node_class}.so"
    return f"lib{node_class}.dylib"


def _validate_target(package_root: Path, package_name: str, node_class: str, version: str, os_name: str, arch_name: str) -> None:
    _clear_package_modules(package_name)
    _clear_package_env(package_name)

    fake_nuke = FakeNuke(node_class=node_class, version=version)
    sys.modules["nuke"] = fake_nuke
    platform_originals = _patch_platform(os_name=os_name, arch_name=arch_name)
    sys.path.insert(0, str(package_root))

    try:
        importlib.import_module(f"{package_name}.init")
    finally:
        sys.path.pop(0)
        _restore_platform(platform_originals)

    expected_binary = (
        package_root
        / package_name
        / "bin"
        / version
        / os_name
        / arch_name
        / _expected_binary_name(node_class=node_class, os_name=os_name)
    )
    if not expected_binary.is_file():
        raise RuntimeError(f"Expected binary missing after import validation: {expected_binary}")

    if not any(Path(path) == expected_binary for path in fake_nuke._loaded_binaries):
        raise RuntimeError(
            f"Package import did not load expected binary for {version} {os_name} {arch_name}: {expected_binary}"
        )

    resolved_path = os.environ.get(f"{package_name.upper()}_PLUGIN_BIN_PATH")
    if resolved_path != str(expected_binary.parent).replace("\\", "/"):
        raise RuntimeError(
            f"Unexpected resolved plugin path for {version} {os_name} {arch_name}: {resolved_path}"
        )


def main() -> int:
    args = _parse_args()
    package_root = Path(args.package_root).resolve()
    package_name = args.package_name
    node_class = args.node_class
    included_os_names = set(args.included_os_names or SUPPORTED_OS_NAMES)

    if not shutil.which("python"):
        print("Python executable is not available in PATH.", file=sys.stderr)
        return 1

    targets = _discover_targets(
        package_root=package_root,
        package_name=package_name,
        included_os_names=included_os_names,
    )
    print(f"Validating package '{package_name}' from {package_root}")

    for version, os_name, arch_name in targets:
        print(f"- import check {package_name} {version} {os_name} {arch_name}")
        _validate_target(
            package_root=package_root,
            package_name=package_name,
            node_class=node_class,
            version=version,
            os_name=os_name,
            arch_name=arch_name,
        )

    print("Package validation OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
