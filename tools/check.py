#!/usr/bin/env python3
"""configure + build + ctest + pytest. Must pass before every commit."""
import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ci_util import run_logged  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent


def default_preset() -> str:
    if os.environ.get("BCC_PRESET"):
        return os.environ["BCC_PRESET"]
    if sys.platform == "win32":
        return "windows-msvc" if shutil.which("cl") else "dev-mingw"
    return "linux-gcc"


def run(cmd):
    rc = run_logged(cmd, f"check.py: {Path(str(cmd[0])).name} {cmd[1] if len(cmd) > 1 else ''} failed", cwd=ROOT)
    if rc != 0:
        sys.exit(rc)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--preset", default=default_preset())
    ap.add_argument("--skip-py", action="store_true")
    ap.add_argument("--cmake-arg", action="append", default=[])
    a = ap.parse_args()

    toolchain = os.environ.get("VCPKG_ROOT")
    extra = list(a.cmake_arg)
    if toolchain and os.environ.get("BCC_USE_VCPKG"):
        extra.append(f"-DCMAKE_TOOLCHAIN_FILE={Path(toolchain) / 'scripts/buildsystems/vcpkg.cmake'}")
    sha = subprocess.run(["git", "rev-parse", "--short=12", "HEAD"], cwd=ROOT,
                         capture_output=True, text=True).stdout.strip() or "unknown"
    extra.append(f"-DBUDGETCC_GIT_SHA={sha}")
    run(["cmake", "--preset", a.preset, *extra])
    run(["cmake", "--build", "--preset", a.preset])
    run(["ctest", "--preset", a.preset])
    if not a.skip_py:
        run([sys.executable, "-m", "pytest", "-q", "tests/py"])


if __name__ == "__main__":
    main()
