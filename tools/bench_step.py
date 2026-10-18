#!/usr/bin/env python3
"""Run budgetcc_bench for the code at HEAD and append one line to results/bench_history.jsonl.

  python tools/bench_step.py E1 [--preset dev-mingw] [--phase p2]

Build the preset first (python tools/check.py). The history line carries the sha of HEAD, so run it after the
commit that contains the change.
"""
import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
# Fewer scheduler interruptions on a busy desktop; 0 (no flag) elsewhere.
HIGH_PRIORITY = getattr(subprocess, "HIGH_PRIORITY_CLASS", 0)


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("change")
    ap.add_argument("--preset", default="dev-mingw")
    ap.add_argument("--phase", default="p2")
    a = ap.parse_args(argv)
    build = ROOT / "build" / a.preset
    exe = next((c for c in (build / "budgetcc_bench.exe", build / "budgetcc_bench") if c.exists()), None)
    if exe is None:
        raise SystemExit(f"budgetcc_bench not found under {build}; build first")
    sha = subprocess.run(["git", "rev-parse", "--short=12", "HEAD"], cwd=ROOT, capture_output=True, text=True,
                         check=True).stdout.strip()
    with tempfile.TemporaryDirectory() as d:
        subprocess.run([str(exe), "--out", str(Path(d) / "bench.json"), "--phase", a.phase, "--git-sha", sha,
                        "--history", str(ROOT / "results" / "bench_history.jsonl"), "--change", a.change],
                       cwd=ROOT, check=True, creationflags=HIGH_PRIORITY)
    return 0


if __name__ == "__main__":
    sys.exit(main())
