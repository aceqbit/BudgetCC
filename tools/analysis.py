#!/usr/bin/env python3
"""Results analysis: validate results JSON, aggregate across traces, generate the README results block.

  python tools/analysis.py readme --p1 results/phase1.json [--p2 results/phase2.json]
                                  [--bench1 results/bench_p1.json] [--bench2 results/bench_p2.json]
                                  [--readme README.md]
"""
import argparse
import json
import math
import statistics
import sys
from pathlib import Path

START = "<!-- RESULTS:START -->"
END = "<!-- RESULTS:END -->"
ALGOS = ["reno", "cubic", "bbr_lite", "gcc_lite", "budgetcc"]
RUN_FIELDS = ["run", "algo", "trace", "buffer", "budget_ms", "mode", "flows", "utilisation_pct", "p95_owd_ms",
              "p99_owd_ms", "violation_pct", "violation_pct_d100", "violation_pct_d150", "violation_pct_d300",
              "recovery_ms", "recovery_censored", "jain", "delivered_bytes", "dropped_pkts", "events"]
ENV_FIELDS = ["cpu", "os", "compiler", "version", "flags", "build_type", "power_plan"]
BENCH_FIELDS = ["events_per_s", "cpu_per_ack_ns", "allocs_per_pkt", "matrix_runtime_s"]

# (label, run field, lower is better, decimals)
METRICS = [
    ("M1 utilisation (%)", "utilisation_pct", False, 1),
    ("M2 p95 one-way delay (ms)", "p95_owd_ms", True, 1),
    ("M3 p99 one-way delay (ms)", "p99_owd_ms", True, 1),
    ("M4 violation at D=150 (%)", "violation_pct_d150", True, 2),
    ("M5 recovery (ms)", "recovery_ms", True, 0),
]
ENGINEERING = [
    ("M7 simulation speed (events/s)", "events_per_s", False, 0),
    ("M8 CPU per ACK (ns)", "cpu_per_ack_ns", True, 1),
    ("M9 allocations per packet", "allocs_per_pkt", True, 3),
    ("M10 matrix runtime (s)", "matrix_runtime_s", True, 1),
]


class SchemaError(ValueError):
    pass


def load_results(path):
    data = json.loads(Path(path).read_text(encoding="utf-8"))
    validate_results(data, str(path))
    return data


def validate_results(data, name="results"):
    for k in ("schema", "phase", "git_sha", "env", "runs"):
        if k not in data:
            raise SchemaError(f"{name}: missing '{k}'")
    if data["schema"] != 1:
        raise SchemaError(f"{name}: unsupported schema {data['schema']}")
    for k in ENV_FIELDS:
        if k not in data["env"]:
            raise SchemaError(f"{name}: env lacks '{k}'")
    for i, r in enumerate(data["runs"]):
        for k in RUN_FIELDS:
            if k not in r:
                raise SchemaError(f"{name}: run {i} lacks '{k}'")


def validate_bench(data, name="bench"):
    for k in ("schema", "phase", "git_sha", "env", "bench"):
        if k not in data:
            raise SchemaError(f"{name}: missing '{k}'")
    for k in BENCH_FIELDS:
        if k not in data["bench"]:
            raise SchemaError(f"{name}: bench lacks '{k}'")


def load_bench(path):
    data = json.loads(Path(path).read_text(encoding="utf-8"))
    validate_bench(data, str(path))
    return data


# ------------------------------------------------------------------ statistics
def median_min_max(values):
    v = [x for x in values if x is not None]
    if not v:
        return None
    return statistics.median(v), min(v), max(v)


def geometric_mean(values):
    if not values or any(x <= 0 for x in values):
        raise ValueError("geometric mean needs positive values")
    return math.exp(sum(math.log(x) for x in values) / len(values))


def improvement(p1, p2, lower_is_better):
    """Percent change as defined in the spec; positive is better."""
    if p1 == 0:
        return None
    return (p1 - p2) / p1 * 100.0 if lower_is_better else (p2 - p1) / p1 * 100.0


def pp_difference(p1, p2):
    return p2 - p1


# ------------------------------------------------------------------ selection
def select(runs, algo, mode="bulk", buffer=1000, budget=None):
    out = [r for r in runs if r["algo"] == algo and r["mode"] == mode and r["buffer"] == buffer]
    if budget is not None:
        out = [r for r in out if r["budget_ms"] == budget]
    return out


def stat(runs, field):
    return median_min_max([r[field] for r in runs])


def fmt(v, d):
    return f"{v:,.{d}f}"


def cell(st, d):
    if st is None:
        return "n/a"
    med, lo, hi = st
    return f"{fmt(med, d)} [{fmt(lo, d)}-{fmt(hi, d)}]"


# ------------------------------------------------------------------ tables
def headline_table(results, buffer):
    """One row per algorithm (BudgetCC at D=150), bulk mode, median [min-max] across traces."""
    runs = results["runs"]
    head = "| Algorithm | " + " | ".join(m[0] for m in METRICS) + " | traces |"
    sep = "|---|" + "---|" * (len(METRICS) + 1)
    rows = [head, sep]
    for a in ALGOS:
        sel = select(runs, a, "bulk", buffer, 150.0 if a == "budgetcc" else None)
        if not sel:
            continue
        cells = [cell(stat(sel, f), d) for _, f, _, d in METRICS]
        rows.append(f"| {a} | " + " | ".join(cells) + f" | {len(sel)} |")
    return "\n".join(rows)


def budget_sweep_table(results):
    runs = results["runs"]
    rows = ["| D (ms) | Utilisation (%) | p95 delay (ms) | Violation at D (%) | traces |", "|---|---|---|---|---|"]
    for d in (100.0, 150.0, 300.0):
        sel = select(runs, "budgetcc", "bulk", 1000, d)
        if sel:
            rows.append(f"| {d:.0f} | {cell(stat(sel, 'utilisation_pct'), 1)} | {cell(stat(sel, 'p95_owd_ms'), 1)} | "
                        f"{cell(stat(sel, 'violation_pct'), 2)} | {len(sel)} |")
    return "\n".join(rows)


def fairness_table(results):
    runs = [r for r in results["runs"] if r["mode"] == "fairness"]
    names = sorted({r["algo"] for r in runs})
    rows = ["| Pair | Jain index | traces |", "|---|---|---|"]
    for n in names:
        sel = [r for r in runs if r["algo"] == n]
        rows.append(f"| {n} | {cell(stat(sel, 'jain'), 3)} | {len(sel)} |")
    return "\n".join(rows)


def video_table(results):
    runs = results["runs"]
    rows = ["| Algorithm | Utilisation (%) | p95 delay (ms) | Violation at D=150 (%) |", "|---|---|---|---|"]
    for a in ALGOS:
        sel = select(runs, a, "video", 1000, 150.0 if a == "budgetcc" else None)
        if sel:
            rows.append(f"| {a} | {cell(stat(sel, 'utilisation_pct'), 1)} | {cell(stat(sel, 'p95_owd_ms'), 1)} | "
                        f"{cell(stat(sel, 'violation_pct_d150'), 2)} |")
    return "\n".join(rows)


def engineering_table(bench1, bench2=None):
    if bench2 is None:
        rows = ["| Metric | Phase 1 |", "|---|---|"]
        for label, f, _, d in ENGINEERING:
            rows.append(f"| {label} | {fmt(bench1['bench'][f], d)} |")
        return "\n".join(rows)
    rows = ["| Metric | Phase 1 | Phase 2 | Change % |", "|---|---|---|---|"]
    for label, f, lower, d in ENGINEERING:
        a, b = bench1["bench"][f], bench2["bench"][f]
        imp = improvement(a, b, lower)
        rows.append(f"| {label} | {fmt(a, d)} | {fmt(b, d)} | {'n/a' if imp is None else fmt(imp, 1)} |")
    return "\n".join(rows)


def env_line(data):
    e = data["env"]
    return (f"{e['cpu']}; {e['os'].strip()}; {e['compiler']} {e['version']}; flags `{e['flags']}`; build {e['build_type']}; "
            f"power plan: {e['power_plan'].strip()}; git {data['git_sha']}")


def render_block(p1, bench1=None, p2=None, bench2=None):
    parts = [f"Phase 1 (git {p1['git_sha']}), {len(p1['runs'])} runs. Median across traces [min-max]. Baselines are the "
             "project's own simplified implementations; Phase 1 was intentionally unoptimised.", ""]
    parts += ["#### Deep buffer (1000 packets), bulk traffic", "", headline_table(p1, 1000), ""]
    parts += ["#### Shallow buffer (100 packets), bulk traffic", "", headline_table(p1, 100), ""]
    parts += ["#### BudgetCC budget sweep (deep buffer)", "", budget_sweep_table(p1), ""]
    parts += ["#### RTP-video mode (deep buffer)", "", video_table(p1), ""]
    parts += ["#### Fairness (deep buffer, two flows)", "", fairness_table(p1), ""]
    if bench1 is not None:
        parts += ["#### Engineering metrics", "", engineering_table(bench1, bench2), ""]
    parts += ["#### Environment of measurement", "", env_line(bench1 if bench1 is not None else p1)]
    if p2 is not None:
        parts += ["", f"Phase 2 results: git {p2['git_sha']}, {len(p2['runs'])} runs."]
    return "\n".join(parts)


def update_readme(readme_path, block):
    text = Path(readme_path).read_text(encoding="utf-8")
    a, b = text.find(START), text.find(END)
    if a < 0 or b < a:
        raise SchemaError(f"{readme_path}: RESULTS markers missing")
    new = text[: a + len(START)] + "\n" + block + "\n" + text[b:]
    if new != text:
        Path(readme_path).write_text(new, encoding="utf-8", newline="\n")
    return new != text


def main(argv=None):
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    rd = sub.add_parser("readme")
    rd.add_argument("--p1", required=True)
    rd.add_argument("--p2")
    rd.add_argument("--bench1")
    rd.add_argument("--bench2")
    rd.add_argument("--readme", default="README.md")
    a = ap.parse_args(argv)
    p1 = load_results(a.p1)
    p2 = load_results(a.p2) if a.p2 else None
    b1 = load_bench(a.bench1) if a.bench1 else None
    b2 = load_bench(a.bench2) if a.bench2 else None
    changed = update_readme(a.readme, render_block(p1, b1, p2, b2))
    print("README results block", "updated" if changed else "unchanged")
    return 0


if __name__ == "__main__":
    sys.exit(main())
