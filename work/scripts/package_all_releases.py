from __future__ import annotations

from pathlib import Path

from package_release import package_target
from sync_publish_bins import sync_publish_bins


WORK_ROOT = Path(__file__).resolve().parents[1]
REPO_ROOT = WORK_ROOT.parent


def main() -> int:
    bin_root = REPO_ROOT / "publish" / "TVectorBlur" / "bin"
    if not bin_root.exists() or not any(bin_root.iterdir()):
        sync_publish_bins()

    if not bin_root.exists():
        raise SystemExit("No bin directory found. Build the plugin first.")

    built_targets: list[str] = []
    for nuke_dir in sorted(path for path in bin_root.iterdir() if path.is_dir()):
        for os_dir in sorted(path for path in nuke_dir.iterdir() if path.is_dir()):
            for arch_dir in sorted(path for path in os_dir.iterdir() if path.is_dir()):
                built_targets.append(f"{nuke_dir.name}/{os_dir.name}/{arch_dir.name}")

    if not built_targets:
        raise SystemExit("No built targets were found under bin/.")

    dist_dir = WORK_ROOT / "dist"
    dist_dir.mkdir(parents=True, exist_ok=True)

    for target in built_targets:
        archive = package_target(target, dist_dir)
        print(archive)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
