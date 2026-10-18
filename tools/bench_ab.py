#!/usr/bin/env python3
"""One interleaved A/B session: Phase 1 baseline build vs the current tree, same machine, same minutes.

  python tools/bench_ab.py [--baseline-ref v0.1-baseline] [--rounds 3] [--jobs N] [--m10-traces a b]

Builds both sides in a scratch directory (offline FetchContent from --deps-dir), alternates budgetcc_bench
baseline/current for --rounds rounds, then times the same matrix subset on both (baseline serial, current
--jobs N). Writes results/bench_p1.json, results/bench_p2.json and two lines in results/bench_history.jsonl.
The laptop's CPU speed drifts several-fold between sessions, so only numbers from one session are compared.
"""
import argparse
import json
import statistics
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FIELDS = ["events_per_s", "events_per_s_no_telemetry", "cpu_per_ack_ns", "allocs_per_pkt"]


def run(cmd, **kw):
    print("+", " ".join(str(c) for c in cmd), flush=True)
    return subprocess.run([str(c) for c in cmd], check=True, **kw)


def exe(build, name):
    for cand in (build / f"{name}.exe", build / name):
        if cand.exists():
            return cand
    raise SystemExit(f"{name} not built in {build}")


def build(src, out, deps, lto, sha):
    deps = Path(deps).resolve()
    args = ["cmake", "-S", src, "-B", out, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release", f"-DBUDGETCC_GIT_SHA={sha}",
            "-DFETCHCONTENT_FULLY_DISCONNECTED=ON"]
    for var, d in (("FMT", "fmt-src"), ("CLI11", "cli11-src"), ("HTTPLIB", "httplib-src"), ("GOOGLETEST", "googletest-src")):
        args.append(f"-DFETCHCONTENT_SOURCE_DIR_{var}={deps / d}")
    if lto:
        args.append("-DBUDGETCC_LTO=ON")
    run(args)
    run(["cmake", "--build", out, "--target", "budgetcc", "budgetcc_bench"])


def bench_once(path, phase):
    out = subprocess.run([str(path), "--out", str(Path(tempfile.gettempdir()) / "bcc_ab.json"),
                          "--phase", phase], capture_output=True, text=True, check=True).stdout
    return json.loads(out)


def timed_matrix(path, phase, traces, jobs, out_file):
    cmd = [path, "matrix", "--phase", phase, "--out", out_file]
    for t in traces:
        cmd += ["--trace", t]
    if jobs > 1:
        cmd += ["--jobs", jobs]
    run(cmd, stderr=subprocess.DEVNULL)
    return json.load(open(out_file))["matrix_runtime_s"]


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--baseline-ref", default="v0.1-baseline")
    ap.add_argument("--rounds", type=int, default=3)
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--deps-dir", default=str(ROOT / "build" / "dev-mingw" / "_deps"))
    ap.add_argument("--m10-traces", nargs="+", default=["att_lte_driving", "step_down"])
    a = ap.parse_args(argv)
    sha = subprocess.run(["git", "rev-parse", "--short=12", "HEAD"], cwd=ROOT, capture_output=True, text=True,
                         check=True).stdout.strip()
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        base_src = tmp / "base_src"
        base_src.mkdir()
        archive = subprocess.run(["git", "archive", a.baseline_ref], cwd=ROOT, capture_output=True, check=True).stdout
        subprocess.run(["tar", "-x", "-C", str(base_src)], input=archive, check=True)
        (base_src / "traces").mkdir(exist_ok=True)
        subprocess.run([sys.executable, str(ROOT / "traces" / "fetch_real.py")], check=True)
        run(["python", "-c", "import shutil,sys;shutil.copytree(sys.argv[1],sys.argv[2],dirs_exist_ok=True)",
             ROOT / "traces" / "real", base_src / "traces" / "real"])
        build(base_src, tmp / "base_build", a.deps_dir, False, a.baseline_ref)
        build(ROOT, tmp / "cur_build", a.deps_dir, True, sha)
        sides = {"p1": exe(tmp / "base_build", "budgetcc_bench"), "p2": exe(tmp / "cur_build", "budgetcc_bench")}
        rounds = {"p1": [], "p2": []}
        for r in range(a.rounds):
            for side in ("p1", "p2"):
                res = bench_once(sides[side], side)
                rounds[side].append(res)
                print(r, side, {k: res["bench"][k] for k in FIELDS}, flush=True)
        m10 = {"p1": timed_matrix(exe(tmp / "base_build", "budgetcc"), "p1", a.m10_traces, 1, str(tmp / "m1.json")),
               "p2": timed_matrix(exe(tmp / "cur_build", "budgetcc"), "p2", a.m10_traces, a.jobs, str(tmp / "m2.json"))}
    names = {"p1": ("results/bench_p1.json", a.baseline_ref), "p2": ("results/bench_p2.json", sha)}
    for side, (path, ref) in names.items():
        first = rounds[side][0]
        bench = {k: statistics.median(r["bench"][k] for r in rounds[side]) for k in FIELDS}
        bench["matrix_runtime_s"] = m10[side]
        first["bench"] = bench
        first["git_sha"] = ref
        first["m10_subset"] = {"traces": a.m10_traces, "jobs": 1 if side == "p1" else a.jobs}
        (ROOT / path).write_text(json.dumps(first, separators=(",", ":")) + "\n")
        with open(ROOT / "results" / "bench_history.jsonl", "a") as f:
            f.write(json.dumps({"git_sha": ref, "change": "AB-" + side, **bench}, separators=(",", ":")) + "\n")
    print("speedup M7:", rounds_speed(rounds, "events_per_s"), "M8:", rounds_speed(rounds, "cpu_per_ack_ns", True))
    return 0


def rounds_speed(rounds, key, lower_better=False):
    p1 = statistics.median(r["bench"][key] for r in rounds["p1"])
    p2 = statistics.median(r["bench"][key] for r in rounds["p2"])
    return round(p1 / p2 if lower_better else p2 / p1, 2)


if __name__ == "__main__":
    sys.exit(main())
