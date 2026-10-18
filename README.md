# BudgetCC

A deterministic, trace-driven congestion-control testbed in C++17 that replays cellular delivery traces (Mahimahi format), runs five controllers (Reno, Cubic, BBR-lite, GCC-lite, BudgetCC) over the same transport, and evaluates BudgetCC, a controller that takes an application one-way-delay budget as input. Per-interval telemetry goes to Loki and Grafana. The build is staged: Phase 1 is a deliberately unoptimised baseline, Phase 2 optimises the algorithm and the code and re-runs the identical experiments.

## Quick start

Windows (MSVC developer prompt) / Linux:

```
python tools/check.py            # configure + build + ctest + pytest
build/<preset>/budgetcc run --algo cubic --trace step_down --buffer 1000
```

Presets: `windows-msvc`, `linux-gcc`, `linux-clang`, `asan-ubsan`, `tsan`, `release`, `dev-mingw` (local MSYS2 g++).

## Architecture

See `docs/design_source.md`. Sender (traffic source, transport, pacer, controller) -> link emulator (trace-driven bottleneck, drop-tail, propagation) -> receiver -> feedback. Telemetry: 10 ms samples -> ring -> JSON lines / CSV -> Loki.

## Metrics

M1 utilisation, M2 p95 OWD, M3 p99 OWD, M4 budget violation, M5 recovery time, M6 Jain fairness, M7 events/s, M8 CPU per ACK, M9 allocations per packet, M10 matrix runtime. Definitions: `CLAUDE.md` section 4, decision 5, and `docs/decisions.md`.

## Results

<!-- RESULTS:START -->
Phase 1 (git f110d1285685), 250 runs. Median across traces [min-max]. Baselines are the project's own simplified implementations; Phase 1 was intentionally unoptimised.

#### Deep buffer (1000 packets), bulk traffic

| Algorithm | M1 utilisation (%) | M2 p95 one-way delay (ms) | M3 p99 one-way delay (ms) | M4 violation at D=150 (%) | M5 recovery (ms) | traces |
|---|---|---|---|---|---|---|
| reno | 99.2 [75.4-99.9] | 1,569.0 [479.0-3,076.0] | 2,234.0 [670.4-10,874.4] | 94.93 [30.76-99.86] | 6,840 [2,670-40,060] | 10 |
| cubic | 99.5 [89.1-99.9] | 1,917.0 [1,019.0-4,270.3] | 3,013.3 [1,388.7-11,291.1] | 96.75 [67.69-99.86] | 7,715 [2,840-40,060] | 10 |
| bbr_lite | 99.7 [96.0-99.9] | 723.2 [246.9-4,622.5] | 1,447.2 [273.0-10,168.2] | 54.39 [33.40-65.21] | 8,080 [740-40,060] | 10 |
| gcc_lite | 14.0 [4.3-89.4] | 55.5 [23.9-1,006.7] | 174.9 [36.6-8,087.5] | 1.23 [0.00-7.30] | 2,210 [0-5,340] | 10 |
| budgetcc | 92.9 [86.1-97.3] | 308.4 [144.9-5,348.9] | 1,434.1 [160.1-9,654.8] | 13.13 [2.46-90.83] | 3,430 [495-6,910] | 10 |

#### Shallow buffer (100 packets), bulk traffic

| Algorithm | M1 utilisation (%) | M2 p95 one-way delay (ms) | M3 p99 one-way delay (ms) | M4 violation at D=150 (%) | M5 recovery (ms) | traces |
|---|---|---|---|---|---|---|
| reno | 96.2 [59.3-99.9] | 160.2 [99.0-339.4] | 344.7 [119.0-804.0] | 6.54 [0.18-39.86] | 1,765 [220-14,060] | 10 |
| cubic | 96.8 [56.5-99.9] | 207.0 [108.9-368.5] | 349.4 [119.2-932.7] | 9.53 [0.18-45.44] | 2,200 [260-40,060] | 10 |
| bbr_lite | 99.6 [94.7-99.9] | 230.4 [103.5-383.5] | 353.5 [119.9-1,196.3] | 14.04 [0.00-49.02] | 2,200 [275-4,975] | 10 |
| gcc_lite | 14.0 [4.3-89.3] | 53.3 [23.9-1,006.7] | 174.9 [36.6-8,087.5] | 1.23 [0.00-7.17] | 2,200 [0-3,800] | 10 |
| budgetcc | 96.5 [78.9-98.7] | 170.2 [104.0-395.6] | 320.5 [120.7-1,376.0] | 6.16 [0.17-49.73] | 810 [280-3,470] | 10 |

#### BudgetCC budget sweep (deep buffer)

| D (ms) | Utilisation (%) | p95 delay (ms) | Violation at D (%) | traces |
|---|---|---|---|---|
| 100 | 91.0 [67.3-97.5] | 291.5 [96.2-5,681.6] | 19.36 [3.02-69.30] | 10 |
| 150 | 92.9 [86.1-97.3] | 308.4 [144.9-5,348.9] | 13.13 [2.46-90.83] | 10 |
| 300 | 94.2 [86.2-99.6] | 637.0 [283.0-5,582.1] | 12.50 [1.05-66.12] | 10 |

#### RTP-video mode (deep buffer)

| Algorithm | Utilisation (%) | p95 delay (ms) | Violation at D=150 (%) |
|---|---|---|---|
| reno | 99.2 [75.4-99.9] | 1,569.0 [479.0-3,076.0] | 94.93 [30.76-99.86] |
| cubic | 99.5 [89.1-99.9] | 1,917.0 [1,019.0-4,270.3] | 96.75 [67.69-99.86] |
| bbr_lite | 99.7 [96.1-99.9] | 731.7 [246.9-4,622.5] | 52.33 [33.40-65.21] |
| gcc_lite | 14.0 [4.3-89.4] | 55.5 [23.9-1,006.7] | 1.23 [0.00-7.30] |
| budgetcc | 92.9 [86.1-97.3] | 308.4 [144.9-5,348.9] | 13.13 [2.46-90.83] |

#### Fairness (deep buffer, two flows)

| Pair | Jain index | traces |
|---|---|---|
| bbr_lite+bbr_lite | 0.998 [0.632-1.000] | 10 |
| budgetcc+budgetcc | 1.000 [0.986-1.000] | 10 |
| budgetcc+cubic | 0.517 [0.506-0.819] | 10 |
| cubic+cubic | 0.999 [0.968-1.000] | 10 |
| gcc_lite+gcc_lite | 0.994 [0.953-1.000] | 10 |
| reno+reno | 0.952 [0.904-1.000] | 10 |

#### Engineering metrics

| Metric | Phase 1 |
|---|---|
| M7 simulation speed (events/s) | 3,455,485 |
| M8 CPU per ACK (ns) | 39.7 |
| M9 allocations per packet | 4.945 |
| M10 matrix runtime (s) | 5,637.0 |

#### Environment of measurement

13th Gen Intel(R) Core(TM) i5-1335U; Microsoft Windows [Version 10.0.26200.9711]; gcc 14.1.0; flags `-O3 -DNDEBUG`; build Release; power plan: Power Scheme GUID: 381b4222-f694-41f0-9685-ff5bb260df2e  (Balanced); git 8569c3f593a0
<!-- RESULTS:END -->

## Figures

Generated by `tools/analysis.py` (empty until measured).

## Reproduce

Filled in per gate.

## Observability

`deploy/docker-compose.yml` (Loki 3100, Grafana 3000); dashboards in `dashboards/`.

## Testing

`python tools/check.py`; sanitizer and coverage jobs run in CI.

## Limitations and claims

- The baselines are this project's own simplified implementations, not the kernel's.
- BudgetCC was not compared against SCReAM or Copa.
- Not claimed: first delay-budget controller.

## Repo layout

`src/{core,link,transport,cc,telemetry,harness}`, `bench/`, `tests/`, `traces/`, `dashboards/`, `deploy/`, `tools/`, `docs/`, `results/`.

## Status

- [x] G0 skeleton
- [x] G1 core + link + traces
- [x] G2 transport + Reno + Cubic + runner
- [x] G3 remaining controllers, video, multi-flow, matrix
- [x] G4 telemetry + Loki + Grafana v1
- [x] G5 Phase 1 freeze
- [ ] G6 Phase 2 engineering
- [ ] G7 Phase 2 algorithm + rerun
- [ ] G8 verify + tag

## Environment of measurement

Recorded in each `results/*.json` header.
