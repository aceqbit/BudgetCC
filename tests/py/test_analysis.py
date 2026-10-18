import copy
import json
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import analysis as an  # noqa: E402


def run(algo, trace, util, p95=100.0, mode="bulk", buffer=1000, budget=150.0, rec=None, jain=None):
    return {"run": "r0", "algo": algo, "trace": trace, "buffer": buffer, "budget_ms": budget, "mode": mode,
            "flows": 1, "utilisation_pct": util, "p95_owd_ms": p95, "p99_owd_ms": p95 + 10, "violation_pct": 1.0,
            "violation_pct_d100": 2.0, "violation_pct_d150": 1.0, "violation_pct_d300": 0.5, "recovery_ms": rec,
            "recovery_censored": 0, "jain": jain, "delivered_bytes": 1, "dropped_pkts": 0, "events": 1}


def results(runs):
    return {"schema": 1, "phase": "p1", "git_sha": "abc",
            "env": {k: "x" for k in an.ENV_FIELDS}, "runs": runs}


def test_improvement_formulas():
    assert an.improvement(200.0, 150.0, True) == pytest.approx(25.0)
    assert an.improvement(50.0, 75.0, False) == pytest.approx(50.0)
    assert an.improvement(0.0, 1.0, True) is None
    assert an.pp_difference(60.0, 72.5) == pytest.approx(12.5)


def test_geometric_mean_and_median_min_max():
    assert an.geometric_mean([1.0, 4.0]) == pytest.approx(2.0)
    assert an.geometric_mean([2.0, 2.0, 2.0]) == pytest.approx(2.0)
    with pytest.raises(ValueError):
        an.geometric_mean([1.0, 0.0])
    assert an.median_min_max([3, 1, 2, None]) == (2, 1, 3)
    assert an.median_min_max([None]) is None
    assert an.median_min_max([1, 2, 3, 4])[0] == 2.5


def test_headline_table_hand_computed():
    runs = [run("cubic", f"t{i}", u, p95=p) for i, (u, p) in enumerate([(90, 300), (80, 200), (70, 400)])]
    runs.append(run("budgetcc", "t0", 50.0, budget=100.0))
    runs.append(run("budgetcc", "t0", 60.0, budget=150.0))
    t = an.headline_table(results(runs), 1000)
    cubic = [r for r in t.splitlines() if r.startswith("| cubic")][0]
    assert "80.0 [70.0-90.0]" in cubic and "300.0 [200.0-400.0]" in cubic and cubic.endswith("| 3 |")
    budget = [r for r in t.splitlines() if r.startswith("| budgetcc")][0]
    assert "60.0 [60.0-60.0]" in budget  # only D=150 enters the headline


def test_recovery_ignores_null_and_empty_gives_na():
    runs = [run("reno", "a", 1, rec=None), run("reno", "b", 1, rec=100.0), run("reno", "c", 1, rec=300.0)]
    t = an.headline_table(results(runs), 1000)
    assert "200 [100-300]" in t
    assert an.cell(None, 1) == "n/a"


def test_validation_rejects_missing_fields():
    good = results([run("reno", "a", 1)])
    an.validate_results(good)
    bad = copy.deepcopy(good)
    del bad["runs"][0]["p95_owd_ms"]
    with pytest.raises(an.SchemaError):
        an.validate_results(bad)
    bad = copy.deepcopy(good)
    del bad["env"]["cpu"]
    with pytest.raises(an.SchemaError):
        an.validate_results(bad)
    bad = copy.deepcopy(good)
    bad["schema"] = 2
    with pytest.raises(an.SchemaError):
        an.validate_results(bad)


def test_engineering_table_with_and_without_phase2():
    b1 = {"bench": {"events_per_s": 1000.0, "cpu_per_ack_ns": 400.0, "allocs_per_pkt": 5.0, "matrix_runtime_s": 100.0}}
    b2 = {"bench": {"events_per_s": 3000.0, "cpu_per_ack_ns": 100.0, "allocs_per_pkt": 0.0, "matrix_runtime_s": 25.0}}
    t = an.engineering_table(b1, b2)
    assert "200.0" in t.split("M7")[1].splitlines()[0]  # +200% events/s
    assert "75.0" in t.split("M8")[1].splitlines()[0]   # -75% CPU
    assert "100.0" in t.split("M9")[1].splitlines()[0]  # allocations gone
    assert "Phase 2" not in an.engineering_table(b1)


def test_readme_block_replacement_is_idempotent(tmp_path):
    readme = tmp_path / "README.md"
    readme.write_text(f"intro\n{an.START}\nold\n{an.END}\noutro\n", encoding="utf-8")
    p1 = results([run("cubic", "a", 50.0)])
    block = an.render_block(p1)
    assert an.update_readme(readme, block) is True
    first = readme.read_text(encoding="utf-8")
    assert "old" not in first and first.startswith("intro\n") and first.endswith("outro\n")
    assert an.update_readme(readme, block) is False
    assert readme.read_text(encoding="utf-8") == first


def test_readme_without_markers_fails(tmp_path):
    readme = tmp_path / "README.md"
    readme.write_text("nothing here", encoding="utf-8")
    with pytest.raises(an.SchemaError):
        an.update_readme(readme, "x")


def test_cli_end_to_end(tmp_path):
    p1 = tmp_path / "p1.json"
    p1.write_text(json.dumps(results([run("cubic", "a", 50.0)])), encoding="utf-8")
    readme = tmp_path / "README.md"
    readme.write_text(f"{an.START}\n{an.END}\n", encoding="utf-8")
    assert an.main(["readme", "--p1", str(p1), "--readme", str(readme)]) == 0
    assert "| cubic |" in readme.read_text(encoding="utf-8")
