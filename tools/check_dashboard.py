#!/usr/bin/env python3
"""Validate dashboards against the provisioning and the Loki label contract.

  python tools/check_dashboard.py [dashboards/*.json]
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ALLOWED_LABELS = {"app", "kind", "algo", "trace", "phase", "buffer", "budget", "mode"}
PIPELINE_LABELS = {"__error__"}  # allowed only as a pipeline filter, never in a stream selector
SELECTOR = re.compile(r"\{([^{}]*)\}")
MATCHER = re.compile(r'([A-Za-z_][A-Za-z0-9_]*)\s*(=~|!~|!=|=)\s*"')
GROUPING = re.compile(r"\b(?:by|without)\s*\(([^)]*)\)")


def provisioned_uids() -> set:
    uids = set()
    for f in (ROOT / "deploy" / "grafana" / "provisioning" / "datasources").glob("*.yml"):
        uids.update(re.findall(r"^\s*uid:\s*(\S+)", f.read_text(), re.M))
    return uids


def expressions(dash: dict):
    for p in dash.get("panels", []):
        for t in p.get("targets", []):
            yield p.get("title", "?"), t.get("expr", "")


def check_expr(expr: str) -> list:
    errs = []
    selectors = SELECTOR.findall(expr)
    if "vector(" not in expr and not selectors:
        errs.append("no stream selector")
    for sel in selectors:
        names = [m.group(1) for m in MATCHER.finditer(sel)]
        if not names:
            errs.append(f"selector without matchers: {{{sel}}}")
        for n in names:
            if n not in ALLOWED_LABELS:
                errs.append(f"label '{n}' is not in the label contract")
    for grp in GROUPING.findall(expr):
        for n in (x.strip() for x in grp.split(",") if x.strip()):
            if n not in ALLOWED_LABELS:
                errs.append(f"grouping label '{n}' is not in the label contract")
    for m in re.finditer(r"\|\s*([A-Za-z_][A-Za-z0-9_]*)\s*(=~|!~|!=|=)\s*\"", expr):
        if m.group(1) not in ALLOWED_LABELS | PIPELINE_LABELS:
            errs.append(f"pipeline filter on unknown label '{m.group(1)}'")
    return errs


def check(path: Path) -> list:
    errs = []
    dash = json.loads(path.read_text())
    uids = provisioned_uids()
    if "loki" not in uids:
        errs.append("datasource uid 'loki' is not provisioned")

    def ds_ok(ds):
        return isinstance(ds, dict) and ds.get("uid") in uids

    for p in dash.get("panels", []):
        if not ds_ok(p.get("datasource")):
            errs.append(f"panel '{p.get('title')}': datasource uid not provisioned")
        for t in p.get("targets", []):
            if not ds_ok(t.get("datasource")):
                errs.append(f"panel '{p.get('title')}': target datasource uid not provisioned")
    for v in dash.get("templating", {}).get("list", []):
        if v.get("type") == "query" and not ds_ok(v.get("datasource")):
            errs.append(f"variable '{v.get('name')}': datasource uid not provisioned")
        if v.get("type") == "query":
            lab = v.get("query", {}).get("label")
            if lab not in ALLOWED_LABELS:
                errs.append(f"variable '{v.get('name')}': label '{lab}' not in the label contract")
    for title, expr in expressions(dash):
        errs += [f"panel '{title}': {e}" for e in check_expr(expr)]
    return errs


def main(argv) -> int:
    files = [Path(a) for a in argv] or sorted((ROOT / "dashboards").glob("*.json"))
    bad = 0
    for f in files:
        errs = check(f)
        for e in errs:
            print(f"{f.name}: {e}")
        bad += len(errs)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
