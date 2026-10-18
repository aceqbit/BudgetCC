#!/usr/bin/env python3
"""Results analysis: validate results JSON, aggregate across traces, generate the README results block.

  python tools/analysis.py readme --p1 results/phase1.json [--p2 results/phase2.json]
                                  [--bench1 results/bench_p1.json] [--bench2 results/bench_p2.json]
                                  [--readme README.md]
  python tools/analysis.py doc --p1 .. --p2 .. --bench1 .. --bench2 .. [--out docs/results.md]
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


# ------------------------------------------------------------------ improvement document
def by_trace(results, algo, budget=None, mode="bulk", buffer=1000):
    return {r["trace"]: r for r in select(results["runs"], algo, mode, buffer, budget)}


def med(results, algo, field, budget=None, mode="bulk", buffer=1000):
    st = stat(select(results["runs"], algo, mode, buffer, budget), field)
    return None if st is None else st[0]


def pct(v):
    return "n/a" if v is None else f"{v:+.1f}%"


def fairness_median(results, pair):
    return statistics.median(x["jain"] for x in results["runs"] if x["mode"] == "fairness" and x["algo"] == pair)


def improvement_doc(p1, p2, b1, b2):
    """docs/results.md: every Phase 1 vs Phase 2 number, hypothesis checks and losing traces."""
    out = ["# Results: Phase 1 vs Phase 2", "",
           "Generated by `tools/analysis.py doc`. Deep buffer (1000 packets), bulk traffic, BudgetCC at D = 150 ms unless "
           "stated; median across the 10 traces. Change % = (P1 - P2) / P1 for lower-is-better metrics, "
           "(P2 - P1) / P1 for higher-is-better; positive is better.", ""]
    rows = ["| Metric | Phase 1 | Phase 2 | Change % | Change (abs) |", "|---|---|---|---|---|"]
    for label, f, lower, d in METRICS:
        a, b = med(p1, "budgetcc", f, 150), med(p2, "budgetcc", f, 150)
        extra = f"{b - a:+.1f} pp" if f == "utilisation_pct" else f"{b - a:+.{d}f}"
        rows.append(f"| {label} | {fmt(a, d)} | {fmt(b, d)} | {pct(improvement(a, b, lower))} | {extra} |")
    for pair in ("budgetcc+budgetcc", "budgetcc+cubic"):
        ja, jb = fairness_median(p1, pair), fairness_median(p2, pair)
        rows.append(f"| M6 Jain, {pair} | {ja:.3f} | {jb:.3f} | {pct(improvement(ja, jb, False))} | {jb - ja:+.3f} |")
    out += ["## 1. BudgetCC, Phase 1 (v0 threshold AIMD) vs Phase 2 (spec controller)", ""] + rows + [""]

    rows = ["| Algorithm | M1 util P1 -> P2 (pp change) | M2 p95 P1 -> P2 (ms) | M2 change | M3 p99 change | M4 viol P1 -> P2 (%) |",
            "|---|---|---|---|---|---|"]
    for algo in ALGOS:
        bud = 150 if algo == "budgetcc" else None
        u1, u2 = med(p1, algo, "utilisation_pct", bud), med(p2, algo, "utilisation_pct", bud)
        a1, a2 = med(p1, algo, "p95_owd_ms", bud), med(p2, algo, "p95_owd_ms", bud)
        c1, c2 = med(p1, algo, "p99_owd_ms", bud), med(p2, algo, "p99_owd_ms", bud)
        v1, v2 = med(p1, algo, "violation_pct_d150", bud), med(p2, algo, "violation_pct_d150", bud)
        rows.append(f"| {algo} | {u1:.1f} -> {u2:.1f} ({u2 - u1:+.1f}) | {a1:.0f} -> {a2:.0f} | "
                    f"{pct(improvement(a1, a2, True))} | {pct(improvement(c1, c2, True))} | {v1:.2f} -> {v2:.2f} |")
    out += ["## 2. Every algorithm, Phase 1 vs Phase 2", "",
            "Baseline controllers are unchanged; their differences come from batched feedback (A3).", ""] + rows + [""]

    rows = ["| Metric | BudgetCC P2 | Cubic P2 | vs Cubic % | BBR-lite P2 | vs BBR-lite % |", "|---|---|---|---|---|---|"]
    for label, f, lower, d in METRICS:
        b = med(p2, "budgetcc", f, 150)
        c, r = med(p2, "cubic", f), med(p2, "bbr_lite", f)
        rows.append(f"| {label} | {fmt(b, d)} | {fmt(c, d)} | {pct(improvement(c, b, lower))} | {fmt(r, d)} | "
                    f"{pct(improvement(r, b, lower))} |")
    out += ["## 3. BudgetCC vs Cubic and BBR-lite (Phase 2)", ""] + rows + [""]

    tr1, tr2 = by_trace(p1, "budgetcc", 150), by_trace(p2, "budgetcc", 150)
    cu, bb, rn = by_trace(p2, "cubic"), by_trace(p2, "bbr_lite"), by_trace(p2, "reno")
    rows = ["| Trace | util P1 | util P2 | p95 P1 | p95 P2 | p95 Cubic | viol P1 | viol P2 | recovery P2 |",
            "|---|---|---|---|---|---|---|---|---|"]
    for t in sorted(tr2):
        a, b = tr1[t], tr2[t]
        rows.append(f"| {t} | {a['utilisation_pct']:.1f} | {b['utilisation_pct']:.1f} | {a['p95_owd_ms']:.0f} | "
                    f"{b['p95_owd_ms']:.0f} | {cu[t]['p95_owd_ms']:.0f} | {a['violation_pct_d150']:.2f} | "
                    f"{b['violation_pct_d150']:.2f} | {b['recovery_ms']} |")
    out += ["## 4. Per trace, BudgetCC D = 150, deep buffer", ""] + rows + [""]

    gap = {t: max(cu[t]["utilisation_pct"], bb[t]["utilisation_pct"], rn[t]["utilisation_pct"]) - tr2[t]["utilisation_pct"]
           for t in tr2}
    h1 = {t for t in tr2 if tr2[t]["violation_pct_d150"] <= 5.0}
    h2 = {t for t in tr2 if gap[t] <= 25.0}
    h3 = {t for t in tr2 if tr2[t]["p95_owd_ms"] <= 0.5 * cu[t]["p95_owd_ms"]}
    n = len(tr2)
    allt = set(tr2)
    out += ["## 5. Spec targets (hypotheses)", "",
            "| Target | Met on | Traces that miss |", "|---|---|---|",
            f"| Violation <= 5% at D = 150 | {len(h1)} of {n} | {', '.join(sorted(allt - h1)) or 'none'} |",
            f"| Utilisation within 25 pp of the best throughput baseline | {len(h2)} of {n} | "
            f"{', '.join(f'{t} ({gap[t]:.0f} pp)' for t in sorted(allt - h2)) or 'none'} |",
            f"| p95 delay at most half of Cubic's | {len(h3)} of {n} | {', '.join(sorted(allt - h3)) or 'none'} |", ""]
    lose = sorted(t for t in tr2 if tr2[t]["p95_owd_ms"] > cu[t]["p95_owd_ms"]
                  or tr2[t]["violation_pct_d150"] > cu[t]["violation_pct_d150"])
    out += ["Traces where BudgetCC is worse than Cubic on p95 or violation: " + (", ".join(lose) or "none") + ".", ""]

    rows = ["| D (ms) | util P1 | util P2 | p95 P1 | p95 P2 | viol at D P1 | viol at D P2 |", "|---|---|---|---|---|---|---|"]
    for d_, f in ((100, "violation_pct_d100"), (150, "violation_pct_d150"), (300, "violation_pct_d300")):
        rows.append(f"| {d_} | {med(p1, 'budgetcc', 'utilisation_pct', d_):.1f} | {med(p2, 'budgetcc', 'utilisation_pct', d_):.1f} | "
                    f"{med(p1, 'budgetcc', 'p95_owd_ms', d_):.0f} | {med(p2, 'budgetcc', 'p95_owd_ms', d_):.0f} | "
                    f"{med(p1, 'budgetcc', f, d_):.2f} | {med(p2, 'budgetcc', f, d_):.2f} |")
    out += ["## 6. Budget sweep (deep buffer)", ""] + rows + [""]

    if b1 is not None and b2 is not None:
        rows = ["| Metric | Phase 1 | Phase 2 | Change % | Speedup x |", "|---|---|---|---|---|"]
        for label, f, lower, d in ENGINEERING:
            a, b = b1["bench"][f], b2["bench"][f]
            if f == "allocs_per_pkt":
                sp = "heap traffic removed" if b == 0 else f"{a / b:.2f}"
            else:
                sp = "n/a" if not a or not b else f"{(a / b if lower else b / a):.2f}"
            rows.append(f"| {label} | {fmt(a, d)} | {fmt(b, d)} | {pct(improvement(a, b, lower))} | {sp} |")
        out += ["## 7. Engineering metrics (one interleaved A/B session)", "", env_line(b2), ""] + rows + [""]
    out += ["## Environment of the matrices", "", f"- Phase 1: {env_line(p1)}", f"- Phase 2: {env_line(p2)}", ""]
    return "\n".join(out)


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
    dc = sub.add_parser("doc")
    for flag in ("--p1", "--p2", "--bench1", "--bench2"):
        dc.add_argument(flag, required=True)
    dc.add_argument("--out", default="docs/results.md")
    a = ap.parse_args(argv)
    if a.cmd == "doc":
        text = improvement_doc(load_results(a.p1), load_results(a.p2), load_bench(a.bench1), load_bench(a.bench2))
        Path(a.out).write_text(text + "\n", encoding="utf-8", newline="\n")
        print("wrote", a.out)
        return 0
    p1 = load_results(a.p1)
    p2 = load_results(a.p2) if a.p2 else None
    b1 = load_bench(a.bench1) if a.bench1 else None
    b2 = load_bench(a.bench2) if a.bench2 else None
    changed = update_readme(a.readme, render_block(p1, b1, p2, b2))
    print("README results block", "updated" if changed else "unchanged")
    return 0


if __name__ == "__main__":
    sys.exit(main())
