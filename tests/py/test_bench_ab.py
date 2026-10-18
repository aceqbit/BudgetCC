import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import bench_ab  # noqa: E402


def rounds(p1, p2, key="events_per_s"):
    return {"p1": [{"bench": {key: v}} for v in p1], "p2": [{"bench": {key: v}} for v in p2]}


def test_speedup_uses_medians_and_direction():
    r = rounds([100, 200, 300], [400, 500, 900])
    assert bench_ab.rounds_speed(r, "events_per_s") == 2.5  # 500 / 200
    r = rounds([100, 100, 100], [25, 25, 25], "cpu_per_ack_ns")
    assert bench_ab.rounds_speed(r, "cpu_per_ack_ns", lower_better=True) == 4.0
