#!/usr/bin/env python3
"""Push one tiny run to Loki, query it back and compare with what the CLI printed.

  python tools/smoke_loki.py --url http://localhost:3100 --exe build/linux-gcc/budgetcc
"""
import argparse
import json
import subprocess
import sys
import time
import urllib.parse
import urllib.request
from pathlib import Path

DURATION_S = 3
SAMPLES = DURATION_S * 100 - 1  # ticks at 10, 20, ... (T - 10 ms)
SELECTOR = '{app="budgetcc", kind="%s", algo="cubic", trace="step_down", phase="p1", buffer="100", budget="none", mode="bulk"}'


def http_json(url: str) -> dict:
    with urllib.request.urlopen(url, timeout=30) as r:
        return json.loads(r.read())


def wait_ready(base: str, timeout_s: int = 90) -> None:
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        try:
            with urllib.request.urlopen(base + "/ready", timeout=5) as r:
                if r.status == 200:
                    return
        except Exception:
            pass
        time.sleep(1)
    raise SystemExit(f"Loki at {base} did not become ready")


def query_range(base: str, query: str, limit: int = 5000) -> list:
    now = time.time()
    qs = urllib.parse.urlencode({"query": query, "limit": limit, "direction": "forward",
                                 "start": int((now - 900) * 1e9), "end": int((now + 60) * 1e9)})
    res = http_json(f"{base}/loki/api/v1/query_range?{qs}")
    assert res["status"] == "success", res
    return res["data"]["result"]


def query_instant(base: str, query: str) -> list:
    qs = urllib.parse.urlencode({"query": query, "time": int(time.time() * 1e9)})
    res = http_json(f"{base}/loki/api/v1/query?{qs}")
    assert res["status"] == "success", res
    return res["data"]["result"]


def compare(cli: dict, stored: dict) -> list:
    """Fields that differ between the CLI summary and the line stored in Loki."""
    return [k for k in cli if stored.get(k) != cli[k]]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", required=True)
    ap.add_argument("--exe", required=True)
    a = ap.parse_args()
    wait_ready(a.url)
    out = subprocess.run([str(Path(a.exe)), "run", "--algo", "cubic", "--trace", "step_down", "--buffer", "100",
                          "--duration-s", str(DURATION_S), "--phase", "p1", "--loki", a.url],
                         capture_output=True, text=True)
    if out.returncode != 0:
        print(out.stdout, out.stderr)
        return 1
    cli = json.loads(out.stdout)

    time.sleep(2)  # let the ingester make the entries queryable
    summary = query_range(a.url, SELECTOR % "summary")
    lines = [v[1] for s in summary for v in s["values"]]
    if len(lines) != 1:
        print(f"expected one summary line, got {len(lines)}")
        return 1
    diff = compare(cli, json.loads(lines[0]))
    if diff:
        print("summary fields differ from the CLI output:", diff)
        return 1

    samples = query_range(a.url, SELECTOR % "sample")
    n = sum(len(s["values"]) for s in samples)
    if n != SAMPLES:
        print(f"expected {SAMPLES} sample lines, got {n}")
        return 1
    first = json.loads(samples[0]["values"][0][1])
    for k in ("t_ms", "run", "rate_mbps", "cap_mbps", "q_ms", "owd_ms", "inflight_kb", "lost"):
        if k not in first:
            print("sample line misses field", k)
            return 1

    # the LogQL the dashboards rely on
    q = f'avg_over_time({SELECTOR % "sample"} | json | unwrap q_ms | __error__="" [15m]) by (algo)'
    res = query_instant(a.url, q)
    if not res or float(res[0]["value"][1]) < 0:
        print("unwrap query returned nothing:", res)
        return 1
    q = f'avg_over_time({SELECTOR % "summary"} | json | unwrap utilisation_pct | __error__="" [15m]) by (algo)'
    res = query_instant(a.url, q)
    if not res or abs(float(res[0]["value"][1]) - cli["utilisation_pct"]) > 1e-6:
        print("summary unwrap query disagrees with the CLI:", res)
        return 1
    print(f"ok: {n} samples and 1 summary line round-tripped through Loki")
    return 0


if __name__ == "__main__":
    sys.exit(main())
