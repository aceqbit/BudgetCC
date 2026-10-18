import subprocess
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import bench_step  # noqa: E402


def test_missing_binary_is_reported():
    with pytest.raises(SystemExit) as e:
        bench_step.main(["E1", "--preset", "no-such-preset"])
    assert "not found" in str(e.value)


def test_passes_head_sha_change_and_history(monkeypatch, tmp_path):
    root = tmp_path
    (root / "build" / "p").mkdir(parents=True)
    (root / "build" / "p" / "budgetcc_bench").write_text("")
    (root / "build" / "p" / "budgetcc_bench.exe").write_text("")
    monkeypatch.setattr(bench_step, "ROOT", root)
    calls = []

    def fake_run(cmd, **kw):
        calls.append(cmd)
        return subprocess.CompletedProcess(cmd, 0, stdout="abc123def456\n")

    monkeypatch.setattr(bench_step.subprocess, "run", fake_run)
    assert bench_step.main(["E7", "--preset", "p"]) == 0
    bench_cmd = calls[-1]
    assert bench_cmd[bench_cmd.index("--git-sha") + 1] == "abc123def456"
    assert bench_cmd[bench_cmd.index("--change") + 1] == "E7"
    assert bench_cmd[bench_cmd.index("--history") + 1].endswith("bench_history.jsonl")
