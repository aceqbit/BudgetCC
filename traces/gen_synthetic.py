#!/usr/bin/env python3
"""Generate the four synthetic 60 s Mahimahi-format traces.

12 Mbps = one 1500-byte delivery opportunity per millisecond. A trace is a list of
millisecond timestamps, one line per opportunity.
"""
import argparse
from pathlib import Path

DURATION_MS = 60_000
OUT_DIR = Path(__file__).resolve().parent / "synthetic"


def rate_step_down(t_s: float) -> float:
    return 12.0 if t_s < 20.0 else 3.0


def rate_step_up(t_s: float) -> float:
    return 3.0 if t_s < 20.0 else 12.0


def rate_sawtooth(t_s: float) -> float:
    return 2.0 + (14.0 - 2.0) * ((t_s % 10.0) / 10.0)


def rate_outage_2s(t_s: float) -> float:
    return 0.0 if 30.0 <= t_s < 32.0 else 10.0


RATES = {
    "step_down": rate_step_down,
    "step_up": rate_step_up,
    "sawtooth": rate_sawtooth,
    "outage_2s": rate_outage_2s,
}


def generate(name: str, duration_ms: int = DURATION_MS) -> list:
    """Return the opportunity timestamps (ms) for a named synthetic trace."""
    rate = RATES[name]
    out = []
    acc = 0.0
    for ms in range(1, duration_ms + 1):
        acc += rate((ms - 0.5) / 1000.0) / 12.0
        n = int(acc + 1e-9)
        acc -= n
        out.extend([ms] * n)
    return out


def write_all(out_dir: Path = OUT_DIR) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    for name in RATES:
        (out_dir / f"{name}.trace").write_text("".join(f"{t}\n" for t in generate(name)), newline="\n")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", type=Path, default=OUT_DIR)
    write_all(ap.parse_args().out)


if __name__ == "__main__":
    main()
