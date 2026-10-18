import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import check_dashboard as cd  # noqa: E402
import gen_dashboard as gd  # noqa: E402


def dashboards():
    files = sorted((ROOT / "dashboards").glob("*.json"))
    assert files, "no dashboards committed"
    return files


def test_dashboards_are_valid_json_and_pass_the_checker():
    for f in dashboards():
        json.loads(f.read_text())
        assert cd.check(f) == [], f.name


def test_committed_dashboards_match_the_generator():
    for name, fn in gd.BUILDERS.items():
        committed = (ROOT / "dashboards" / f"{name}.json").read_text()
        assert json.loads(committed) == json.loads(json.dumps(fn()))


def test_datasource_uid_is_provisioned():
    assert "loki" in cd.provisioned_uids()


def test_v1_has_the_required_panels_and_variables():
    d = json.loads((ROOT / "dashboards" / "budgetcc_v1.json").read_text())
    titles = [p["title"] for p in d["panels"]]
    assert titles == ["Rate versus capacity", "Queue delay with budget line", "In-flight data", "Summary (M1-M6)"]
    names = {v["name"] for v in d["templating"]["list"]}
    assert {"trace", "buffer", "phase"} <= names
    fields = " ".join(t["expr"] for p in d["panels"] for t in p["targets"])
    for f in ("rate_mbps", "cap_mbps", "q_ms", "inflight_kb", "utilisation_pct", "p95_owd_ms", "p99_owd_ms",
              "violation_pct", "recovery_ms", "jain"):
        assert f in fields, f


def test_checker_rejects_labels_outside_the_contract():
    assert cd.check_expr('avg_over_time({app="budgetcc", kind="sample"} | json | unwrap q_ms [1s]) by (algo)') == []
    assert cd.check_expr('{app="budgetcc", run="r0001"} | json') != []
    assert cd.check_expr('avg_over_time({app="budgetcc"} | json | unwrap q_ms [1s]) by (flow)') != []
    assert cd.check_expr('{app="budgetcc"} | json | foo="x"') != []
    assert cd.check_expr("vector(150)") == []
    assert cd.check_expr("rate(x[1s])") != []


def test_compose_pins_exact_image_versions():
    text = (ROOT / "deploy" / "docker-compose.yml").read_text()
    images = [ln.split("image:")[1].strip() for ln in text.splitlines() if "image:" in ln]
    assert len(images) == 2
    for img in images:
        assert ":" in img and not img.endswith(":latest")
    assert '"3100:3100"' in text and '"3000:3000"' in text


def test_loki_limits_are_raised():
    text = (ROOT / "deploy" / "loki-config.yaml").read_text()
    for key in ("ingestion_rate_mb", "ingestion_burst_size_mb", "per_stream_rate_limit", "per_stream_rate_limit_burst",
                "max_entries_limit_per_query", "max_query_series"):
        assert key in text, key
