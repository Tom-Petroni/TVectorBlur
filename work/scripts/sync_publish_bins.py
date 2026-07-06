from __future__ import annotations

import shutil
from pathlib import Path


WORK_ROOT = Path(__file__).resolve().parents[1]
REPO_ROOT = WORK_ROOT.parent
WORK_BIN_ROOT = WORK_ROOT / "TVectorBlur" / "bin"
PUBLISH_BIN_ROOT = REPO_ROOT / "publish" / "TVectorBlur" / "bin"


def sync_publish_bins() -> int:
    if not WORK_BIN_ROOT.exists():
        raise SystemExit(f"Work bin folder not found: {WORK_BIN_ROOT}")

    if PUBLISH_BIN_ROOT.exists():
        shutil.rmtree(PUBLISH_BIN_ROOT)

    shutil.copytree(WORK_BIN_ROOT, PUBLISH_BIN_ROOT)
    print(PUBLISH_BIN_ROOT)
    return 0


if __name__ == "__main__":
    raise SystemExit(sync_publish_bins())
