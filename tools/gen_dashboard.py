#!/usr/bin/env python3
"""Generate the Grafana dashboards so every query stays consistent with the Loki label contract
(CLAUDE.md decision 11): labels are app, kind, algo, trace, phase, buffer, budget, mode.

  python tools/gen_dashboard.py            # writes dashboards/budgetcc_v1.json (and v2 when present)
"""
import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DS = {"type": "loki", "uid": "loki"}

# Stream selectors. Variables always use regex matchers so "All" and multi-select both work.
SAMPLE = '{app="budgetcc", kind="sample", trace=~"$trace", buffer=~"$buffer", phase=~"$phase", mode=~"$mode", budget=~"$budget"}'
SUMMARY = '{app="budgetcc", kind="summary", trace=~"$trace", buffer=~"$buffer", phase=~"$phase", mode=~"$mode", budget=~"$budget"}'
SUMMARY_ALL_PHASES = '{app="budgetcc", kind="summary", trace=~"$trace", buffer=~"$buffer", mode=~"$mode", budget=~"$budget"}'


def unwrap(selector: str, field: str) -> str:
    return f'{selector} | json | unwrap {field} | __error__=""'


def target(expr: str, ref: str, legend: str = "", instant: bool = False) -> dict:
    t = {"datasource": DS, "expr": expr, "refId": ref, "legendFormat": legend,
         "queryType": "instant" if instant else "range"}
    if instant:
        t["instant"] = True
        t["range"] = False
    return t


def timeseries(pid, title, x, y, w, h, targets, unit, description="", thresholds=None, overrides=None):
    defaults = {
        "unit": unit,
        "custom": {"drawStyle": "line", "lineWidth": 1, "fillOpacity": 0, "showPoints": "never",
                   "spanNulls": True, "thresholdsStyle": {"mode": "dashed" if thresholds else "off"}},
    }
    if thresholds:
        defaults["thresholds"] = {"mode": "absolute",
                                  "steps": [{"color": "green", "value": None}, {"color": "red", "value": thresholds}]}
    return {"id": pid, "type": "timeseries", "title": title, "description": description,
            "datasource": DS, "gridPos": {"x": x, "y": y, "w": w, "h": h}, "interval": "100ms",
            "targets": targets, "fieldConfig": {"defaults": defaults, "overrides": overrides or []},
            "options": {"legend": {"displayMode": "list", "placement": "bottom", "calcs": []},
                        "tooltip": {"mode": "multi", "sort": "desc"}}}


def summary_table(pid, title, x, y, w, h, metrics, description=""):
    """One instant query per metric, grouped by algo, merged into one row per algorithm."""
    targets, rename = [], {}
    for i, (field, label) in enumerate(metrics):
        ref = chr(ord("A") + i)
        expr = f"avg_over_time({unwrap(SUMMARY, field)} [$__range]) by (algo)"
        targets.append(target(expr, ref, "", instant=True))
        rename[f"Value #{ref}"] = label
    return {"id": pid, "type": "table", "title": title, "description": description, "datasource": DS,
            "gridPos": {"x": x, "y": y, "w": w, "h": h}, "targets": targets,
            "transformations": [
                {"id": "labelsToFields", "options": {"mode": "columns"}},
                {"id": "merge", "options": {}},
                {"id": "organize", "options": {"excludeByName": {"Time": True}, "renameByName": {"algo": "algorithm", **rename}}},
            ],
            "fieldConfig": {"defaults": {"decimals": 2}, "overrides": []},
            "options": {"showHeader": True, "sortBy": [{"displayName": "algorithm", "desc": False}]}}


def query_var(name, label, default):
    return {"name": name, "label": label, "type": "query", "datasource": DS, "refresh": 2, "sort": 1,
            "includeAll": False, "multi": False, "hide": 0,
            "query": {"label": label, "refId": "LokiVariableQueryEditor-VariableQuery",
                      "stream": '{app="budgetcc"}', "type": 1},
            "current": {"text": default, "value": default}}


def custom_var(name, options, default, multi=False):
    cur = default if isinstance(default, list) else [default]
    var = {"name": name, "label": name, "type": "custom", "query": ",".join(options), "multi": multi,
           "includeAll": multi, "allValue": ".*" if multi else None, "hide": 0,
           "options": [{"selected": o in cur, "text": o, "value": o} for o in options],
           "current": {"text": cur if multi else cur[0], "value": cur if multi else cur[0]}}
    if not multi:
        var.pop("allValue")
    return var


def variables():
    return [
        query_var("trace", "trace", "step_down"),
        query_var("buffer", "buffer", "1000"),
        query_var("phase", "phase", "p1"),
        custom_var("mode", ["bulk", "video", "fairness", "bench"], "bulk"),
        custom_var("budget", ["none", "100", "150", "300"], ["none", "150"], multi=True),
        custom_var("budget_line", ["100", "150", "300"], "150"),
    ]


def base(uid, title, panels):
    return {"uid": uid, "title": title, "schemaVersion": 39, "version": 1, "editable": False,
            "timezone": "browser", "refresh": "", "time": {"from": "now-15m", "to": "now"},
            "tags": ["budgetcc"], "templating": {"list": variables()}, "annotations": {"list": []},
            "panels": panels}


def v1_panels():
    rate = timeseries(
        1, "Rate versus capacity", 0, 0, 24, 9,
        [target(f"avg_over_time({unwrap(SAMPLE, 'rate_mbps')} [$__interval]) by (algo)", "A", "{{algo}}"),
         target(f"max(max_over_time({unwrap(SAMPLE, 'cap_mbps')} [$__interval]))", "B", "capacity")],
        "Mbits", "Goodput received per 10 ms interval against the trace capacity.",
        overrides=[{"matcher": {"id": "byName", "options": "capacity"},
                    "properties": [{"id": "custom.lineStyle", "value": {"fill": "dash", "dash": [8, 6]}},
                                   {"id": "color", "value": {"mode": "fixed", "fixedColor": "dark-gray"}}]}])
    queue = timeseries(
        2, "Queue delay with budget line", 0, 9, 12, 9,
        [target(f"max_over_time({unwrap(SAMPLE, 'q_ms')} [$__interval]) by (algo)", "A", "{{algo}}"),
         target("vector($budget_line)", "B", "budget D")],
        "ms", "Ground-truth queue delay at the bottleneck. The red dashed line is the budget D.", thresholds=150,
        overrides=[{"matcher": {"id": "byName", "options": "budget D"},
                    "properties": [{"id": "custom.lineStyle", "value": {"fill": "dash", "dash": [4, 4]}},
                                   {"id": "color", "value": {"mode": "fixed", "fixedColor": "red"}}]}])
    inflight = timeseries(
        3, "In-flight data", 12, 9, 12, 9,
        [target(f"avg_over_time({unwrap(SAMPLE, 'inflight_kb')} [$__interval]) by (algo)", "A", "{{algo}}")],
        "kbytes", "Bytes sent and not yet acknowledged: window behaviour and bufferbloat.")
    table = summary_table(
        4, "Summary (M1-M6)", 0, 18, 24, 9,
        [("utilisation_pct", "M1 utilisation %"), ("p95_owd_ms", "M2 p95 OWD ms"), ("p99_owd_ms", "M3 p99 OWD ms"),
         ("violation_pct", "M4 violation %"), ("recovery_ms", "M5 recovery ms"), ("jain", "M6 Jain")],
        "One row per algorithm for the selected trace, buffer, phase and mode.")
    return [rate, queue, inflight, table]


def build_v1() -> dict:
    return base("budgetcc-v1", "BudgetCC v1", v1_panels())


BUILDERS = {"budgetcc_v1": build_v1}


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", type=Path, default=ROOT / "dashboards")
    a = ap.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    for name, fn in BUILDERS.items():
        (a.out / f"{name}.json").write_text(json.dumps(fn(), indent=2) + "\n", newline="\n")


if __name__ == "__main__":
    main()
