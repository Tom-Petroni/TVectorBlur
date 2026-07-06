from __future__ import annotations

import argparse
import json
import shutil
from pathlib import Path

from sync_publish_bins import sync_publish_bins


WORK_ROOT = Path(__file__).resolve().parents[1]
REPO_ROOT = WORK_ROOT.parent
PACKAGE_FILES = [
    "__init__.py",
    "init.py",
    "menu.py",
    "_consts.py",
    "_menu_creator.py",
    "_node_setup.py",
    "_plugin_loader.py",
]
PACKAGE_DIRS = [
    "resources",
]
PUBLISH_ROOT_FILES = [
    "init.py",
]


def read_version() -> str:
    return (REPO_ROOT / "VERSION").read_text(encoding="utf-8").strip()


def copy_tree(src: Path, dst: Path) -> None:
    if src.is_dir():
        shutil.copytree(src, dst, dirs_exist_ok=True)
    else:
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)


def build_manifest(version: str, target: str, package_name: str) -> dict:
    parts = target.split("/")
    nuke_version = parts[0] if len(parts) > 0 else "unknown"
    operating_system = parts[1] if len(parts) > 1 else "unknown"
    return {
        "product": "TVectorBlur",
        "version": version,
        "package": package_name,
        "target": {
            "nuke_version": nuke_version,
            "os": operating_system,
        },
    }


def package_target(target: str, output_dir: Path) -> Path:
    version = read_version()
    target_path = REPO_ROOT / "publish" / "TVectorBlur" / "bin" / target
    if not target_path.is_dir():
        sync_publish_bins()
        if not target_path.is_dir():
            raise FileNotFoundError(f"Built target folder not found: {target_path}")

    parts = target.split("/")
    if len(parts) != 2:
        raise ValueError("Target must look like '<nuke>/<os>', for example '16.0/windows'.")

    nuke_version, operating_system = parts
    package_name = f"TVectorBlur-{version}-nuke{nuke_version}-{operating_system}"
    staging_dir = output_dir / package_name
    if staging_dir.exists():
        shutil.rmtree(staging_dir)

    plugin_root = staging_dir / "TVectorBlur"
    plugin_root.mkdir(parents=True, exist_ok=True)

    for rel_path in PUBLISH_ROOT_FILES:
        copy_tree(REPO_ROOT / "publish" / rel_path, staging_dir / rel_path)

    for rel_path in PACKAGE_FILES:
        copy_tree(REPO_ROOT / "publish" / "TVectorBlur" / rel_path, plugin_root / rel_path)

    for rel_path in PACKAGE_DIRS:
        copy_tree(REPO_ROOT / "publish" / "TVectorBlur" / rel_path, plugin_root / rel_path)

    copy_tree(target_path, plugin_root / "bin" / target)

    (staging_dir / "INSTALL.txt").write_text(
        "\n".join(
            [
                "TVectorBlur installation",
                "",
                "1. Copy the 'TVectorBlur' folder into your .nuke directory or studio package location.",
                "2. Make sure your global .nuke/init.py adds the package path if needed.",
                "3. Restart Nuke.",
                "",
                f"Included target: Nuke {nuke_version} / {operating_system}",
            ]
        ),
        encoding="utf-8",
    )

    manifest_path = staging_dir / "manifest.json"
    manifest_path.write_text(
        json.dumps(build_manifest(version, target, package_name), indent=2),
        encoding="utf-8",
    )

    archive_base = output_dir / package_name
    shutil.make_archive(str(archive_base), "zip", root_dir=staging_dir)
    shutil.rmtree(staging_dir)
    return Path(f"{archive_base}.zip")


def main() -> int:
    parser = argparse.ArgumentParser(description="Package TVectorBlur for a built Nuke/OS target.")
    parser.add_argument("--target", required=True, help="Target path under bin/, e.g. 16.0/windows")
    parser.add_argument("--output-dir", default=str(WORK_ROOT / "dist"), help="Where to write the zip archive")
    args = parser.parse_args()

    output_dir = Path(args.output_dir).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    archive_path = package_target(args.target, output_dir)
    print(archive_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
