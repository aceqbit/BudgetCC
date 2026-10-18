import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "traces"))

import gen_synthetic as g  # noqa: E402


def window_mbps(ts, lo_ms, hi_ms):
    n = sum(1 for t in ts if lo_ms < t <= hi_ms)
    return n * 12.0 / (hi_ms - lo_ms)


def test_duration_is_60s():
    for name in g.RATES:
        ts = g.generate(name)
        assert ts == sorted(ts)
        assert 59_990 <= ts[-1] <= 60_000


def test_step_down_windows():
    ts = g.generate("step_down")
    assert abs(window_mbps(ts, 5_000, 15_000) - 12.0) <= 0.12
    assert abs(window_mbps(ts, 25_000, 55_000) - 3.0) <= 0.03


def test_step_up_windows():
    ts = g.generate("step_up")
    assert abs(window_mbps(ts, 5_000, 15_000) - 3.0) <= 0.03
    assert abs(window_mbps(ts, 25_000, 55_000) - 12.0) <= 0.12


def test_outage_windows():
    ts = g.generate("outage_2s")
    assert abs(window_mbps(ts, 5_000, 25_000) - 10.0) <= 0.1
    assert window_mbps(ts, 30_000, 32_000) == 0.0
    assert abs(window_mbps(ts, 33_000, 58_000) - 10.0) <= 0.1


def test_sawtooth_is_a_repeating_ramp():
    ts = g.generate("sawtooth")
    for cycle in range(6):
        base = cycle * 10_000
        # mean of a 2->14 ramp over [base+2 s, base+4 s] = 2 + 12 * 0.3 = 5.6
        assert abs(window_mbps(ts, base + 2_000, base + 4_000) - 5.6) <= 0.056
        # last second of the ramp: 2 + 12 * 0.95 = 13.4
        assert abs(window_mbps(ts, base + 9_000, base + 10_000) - 13.4) <= 0.134


def test_committed_files_match_generator():
    for name in g.RATES:
        committed = (g.OUT_DIR / f"{name}.trace").read_text()
        assert committed == "".join(f"{t}\n" for t in g.generate(name))
