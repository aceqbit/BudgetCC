#!/usr/bin/env python3
"""Clone and bootstrap vcpkg at the manifest baseline (used by CI on Windows and Linux)."""
import json
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ci_util import run_logged  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent


def main() -> int:
    dest = os.environ["VCPKG_ROOT"]
    baseline = json.loads((ROOT / "vcpkg.json").read_text())["builtin-baseline"]
    steps = [
        (["git", "clone", "https://github.com/microsoft/vcpkg", dest], None),
        (["git", "checkout", baseline], dest),
    ]
    if sys.platform == "win32":
        steps.append((["cmd", "/c", "bootstrap-vcpkg.bat", "-disableMetrics"], dest))
    else:
        steps.append((["./bootstrap-vcpkg.sh", "-disableMetrics"], dest))
    for cmd, cwd in steps:
        rc = run_logged(cmd, "vcpkg setup failed", cwd=cwd)
        if rc != 0:
            return rc
    return 0


if __name__ == "__main__":
    sys.exit(main())
