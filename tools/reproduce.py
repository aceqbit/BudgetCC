#!/usr/bin/env python3
"""Regenerate results JSON, benchmark JSON and the README results block from scratch.

  python tools/reproduce.py --phase p1|p2|all [--preset release] [--jobs N]

Builds the given CMake preset, runs the matrix and the benchmark for each phase, then regenerates the README block.
The matrix and the benchmark of one phase run back to back on this machine so the env blocks match.
"""
import argparse
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PHASES = {"p1": ("results/phase1.json", "results/bench_p1.json"), "p2": ("results/phase2.json", "results/bench_p2.json")}


def run(cmd):
    print("+", " ".join(str(c) for c in cmd), flush=True)
    subprocess.run([str(c) for c in cmd], cwd=ROOT, check=True)


def find_exe(build_dir, name):
    for cand in (build_dir / f"{name}.exe", build_dir / name, build_dir / "bin" / f"{name}.exe", build_dir / "bin" / name):
        if cand.exists():
            return cand
    raise SystemExit(f"{name} not found under {build_dir}; build first")


def reproduce_phase(phase, exe_dir, jobs):
    matrix_json, bench_json = PHASES[phase]
    run([sys.executable, "traces/fetch_real.py"])
    run([sys.executable, "traces/gen_synthetic.py"])
    cmd = [find_exe(exe_dir, "budgetcc"), "matrix", "--phase", phase, "--out", matrix_json]
    if jobs > 1 and phase == "p2":
        cmd += ["--jobs", jobs]
    run(cmd)
    run([find_exe(exe_dir, "budgetcc_bench"), "--phase", phase, "--out", bench_json, "--matrix-json", matrix_json,
         "--history", "results/bench_history.jsonl", "--change", f"{phase}-reproduce"])


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--phase", choices=["p1", "p2", "all"], required=True)
    ap.add_argument("--preset", default="release")
    ap.add_argument("--jobs", type=int, default=1)
    a = ap.parse_args(argv)
    run(["cmake", "--preset", a.preset])
    run(["cmake", "--build", "--preset", a.preset])
    exe_dir = ROOT / "build" / a.preset
    phases = ["p1", "p2"] if a.phase == "all" else [a.phase]
    for p in phases:
        reproduce_phase(p, exe_dir, a.jobs)
    cmd = [sys.executable, "tools/analysis.py", "readme", "--p1", PHASES["p1"][0], "--bench1", PHASES["p1"][1]]
    if "p2" in phases:
        cmd += ["--p2", PHASES["p2"][0], "--bench2", PHASES["p2"][1]]
    run(cmd)
    return 0


if __name__ == "__main__":
    sys.exit(main())
