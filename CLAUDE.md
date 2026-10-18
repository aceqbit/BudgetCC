# CLAUDE.md — BudgetCC

C++17 deterministic trace-driven congestion-control testbed, five controllers (Reno, Cubic, BBR-lite, GCC-lite, BudgetCC), Loki + Grafana telemetry.
Spec of record: `docs/design_source.md`. Section 4 below resolves the spec's ambiguities; otherwise the spec wins. Every decision is mirrored in `docs/decisions.md`.

## 1. Operating mode
- Work autonomously. Ask only when blocked on credentials or permissions. Any other ambiguity: decide, write 3 lines in `docs/decisions.md` (decision / alternative / reason), continue.
- Gates G0–G8: G0 skeleton+CI; G1 core+link+traces; G2 transport+Reno+Cubic+runner; G3 BBR-lite, GCC-lite, BudgetCC v0, video, multi-flow, M5/M6, matrix; G4 telemetry+Loki+Grafana v1; G5 Phase 1 matrix+tag; G6 Phase 2 engineering E1–E7; G7 Phase 2 algorithm A1–A4+rerun+results; G8 verify+tag. At the end of each gate append one line to `docs/PROGRESS.md`.
- Speed mode (current, supersedes G6-G8 detail in docs/PROMPTS.md): deliver measured improvement, nothing else.
  - CUT (log in PROGRESS.md and README "Limitations"): A4; dashboard v2 (v1 stays); per-step retroactive benchmarks; fresh-clone full reproduce (build + ctest only); figures beyond the two required; GCC-lite tuning (it is reported as measured, low utilisation noted); any feature that does not move an M1-M10 number.
  - KEEP: A3 (done), A1 (spec controller), A2 (27-combo sweep, automated), `--jobs` matrix, phase-2 matrix, one interleaved A/B benchmark session (`tools/bench_ab.py`: `v0.1-baseline` build vs HEAD, 3 rounds, medians, M10 on a fixed matrix subset), `tools/analysis.py` improvement tables.
  - FIX, do not defend: if a metric regresses or a feature does not work (e.g. BudgetCC violation worse than baseline), find the cause, fix it once, re-measure; if still bad, report the number and cut the feature.
  - README opens (above Quick start) with ONE generated table: M1-M10 Phase 1 vs Phase 2 vs Change %, plus speedup x for M7/M8/M10. No prose claims without a number from `results/*.json`.
- Overrun cut order: A4, parallel harness, GCC-lite, RTP-video mode. Never cut: deterministic emulator, metrics, Phase 1 baseline, tests, README. Log every cut in `PROGRESS.md` and README "Limitations".
- No features outside section 12. No placeholder code, no TODO in pushed code, no dead code.

## 2. Environment and build
- Everything under `tools/` and `scripts/` is Python 3.
- Toolchains: MSVC (windows-latest), GCC + Clang (ubuntu-latest). CMake >= 3.24, Ninja, `CMakePresets.json`. Deps: fmt, CLI11, cpp-httplib, GoogleTest (vcpkg manifest in CI; CMake FetchContent fallback at pinned tags locally).
- Warnings: `/W4 /WX /permissive-` or `-Wall -Wextra -Wpedantic -Werror`. FP determinism: `-ffp-contract=off` / `/fp:precise`. No `-ffast-math`.
- Portable code only; `constexpr size_t kCacheLine = 64`.
- Phase 1 build = default Release, no LTO. Phase 2 enables LTO.
- `python tools/check.py` = configure + build + ctest + pytest; green before every commit.

CLI contract:
```
budgetcc run    --algo <reno|cubic|bbr_lite|gcc_lite|budgetcc> [--algo ...] --trace <name|path> --buffer <100|1000>
                [--budget-ms 150] [--mode bulk|video] [--prop-delay-ms 20] [--duration-s 60] [--seed 1]
                [--phase p1|p2] [--loki URL] [--csv DIR] [--out FILE] [--cc-param key=value ...]
budgetcc matrix --phase p1|p2 [--jobs N] [--loki URL] --out results/phase1.json
budgetcc_bench  --out results/bench.json [--loki URL]
python tools/analysis.py ...   python tools/reproduce.py --phase p1|p2|all
```

## 3. Architecture invariants
- `int64` ns time; only the Simulator advances it. Packet 1250 B on wire (1200 payload). Delivery opportunity 1500 B per trace line; trace loops.
- Drop-tail FIFO byte limit = `buffer_pkts x 1250`. Propagation 20 ms forward, 20 ms return; return path has no queue/loss.
- Datagram transport, no retransmission. Loss = 3 later packets acked, or timeout `max(200 ms, 2 x srtt)`.
- Control step and telemetry sample every 10 ms simulated.
- Controller interface exactly as in the spec. Sender/Receiver instantiable N times (flow id = SSRC).

## 4. Decisions
1. Credit accumulates only while the queue is non-empty, resets to 0 when it empties. Head packet leaves when `credit >= 1250`; leftover carries while backlogged.
2. 12-byte RTP header, 16-bit sequence unwrapped to uint64 at the receiver, network byte order.
3. Reno/Cubic: `inflightCap = cwnd`; `pacingRate = gain x cwnd / srtt`, gain 2.0 in slow start else 1.25; IW 10 packets; srtt 100 ms before first sample.
4. Video: 30 fps; key frame every 60th frame at 4x; average frame size = `pacingRate/30`; 1200 B payloads; drop a frame if its head packet waited > 1 s.
5. Metrics: M1 = wire bytes received by T / sum of 1500 B opportunities in [0,T). Percentiles nearest-rank. M4 at the run's D; summaries also carry violation at D = 100/150/300. M5: drop event at t when mean capacity over [t,t+200ms) <= 50% of mean over [t-1s,t); merge events < 2 s apart; recovery = first time >= t where ground-truth queue delay stays <= Q* for 100 ms; Q* = (150 - owd_min) x 0.8; censored if none before next event/run end; run value = median over events, null if none. M6 = Jain on per-flow delivered bytes.
6. Fairness runs: 10 traces x deep x {5 same-algo pairs + BudgetCC-vs-Cubic} = 60 runs, BudgetCC D=150. Matrix = 250.
7. Video-mode BudgetCC D = 150. Bulk BudgetCC D in {100,150,300}.
8. M8 = median ns/ACK of a replay microbench (7 runs); M9 = global operator-new count after 5 s simulated / packets sent in that window; M7 = events/s (one trace, Cubic, 60 s, with and without telemetry, 7 runs, median).
9. A2 tuning split fixed before the sweep: tune on 2 real traces (one stationary, one driving) + sawtooth + step_down; report on the other 4 real + step_up + outage_2s. Objective: maximise mean utilisation subject to violation <= 5% at D=150, deep buffer.
10. Real traces fetched by `traces/fetch_real.py` from the Mahimahi repo at a pinned commit with SHA-256 in `traces/MANIFEST.json`; not vendored. Synthetic traces (60 s) from `traces/gen_synthetic.py`: step_down 12->3 Mbps at 20 s; step_up 3->12 at 20 s; sawtooth 2->14 Mbps over 10 s repeating; outage_2s 10 Mbps with 0 for 2 s from 30 s.
11. Loki labels = `app, kind, algo, trace, phase, buffer, budget, mode`; budget `none` for baselines; mode in bulk|video|fairness|bench.
12. Phase 1 numbers live in committed `results/phase1.json` and tag `v0.1-baseline`.

## 5. Metrics contract
Sample line (every 10 ms): `t_ms, run, rate_mbps, cap_mbps, q_ms, owd_ms, inflight_kb, lost` + optional `flow, chat_mbps`. Summary line / results entry: `run, algo, trace, buffer, budget_ms, mode, flows, utilisation_pct, p95_owd_ms, p99_owd_ms, violation_pct, violation_pct_d100, violation_pct_d150, violation_pct_d300, recovery_ms, recovery_censored, jain, delivered_bytes, dropped_pkts, events`. Bench entry: `events_per_s, cpu_per_ack_ns, allocs_per_pkt, matrix_runtime_s`. Results header: `schema, phase, git_sha, env{cpu, os, compiler, version, flags, build_type, power_plan}`.
Aggregation: median across traces with min and max; geometric mean for ratios. Improvement: lower-is-better (P1-P2)/P1x100; higher-is-better (P2-P1)/P1x100; utilisation also in pp.

## 6. Definition of done per file
Every pushed source file has: a deterministic network-free test file covering its assertions; its metric hook asserted by a test; green `tools/check.py`; >= 90% line coverage from its own tests (CI floor 80%); no warnings/clang-tidy findings; sanitizer runs for memory/UB code (ASan+UBSan) and threaded code (TSan); regression test before any bug fix. Test layers: unit, property (fixed seed), differential (new vs naive), golden (`tests/golden`, integers exact, floats rel 1e-9), integration (Loki smoke).

## 7. Git
`main` only, MIT. Conventional commits `feat(link): ... [M1,M4]` with `Tests: +N (total T) | Coverage: X%`. Push after every gate; never push red; no force-push, no `--no-verify`, no secrets/build outputs/data volumes. Tags `v0.1-baseline`, `v1.0-optimised`. Remote: https://github.com/aceqbit/BudgetCC.

## 8. README
Sections in order: what/why; Quick start; Architecture; Metrics M1–M10; Results (generated between `<!-- RESULTS:START -->` and `<!-- RESULTS:END -->`); Figures; Reproduce; Observability; Testing; Limitations and claims; Repo layout; Status checklist; Environment of measurement.

## 9. Phases
Phase 1 deliberately naive; mark each naive site `// PHASE1-NAIVE(E<n>)`. Phase 2 replaces them (E1–E7, A1–A4). Replaced naive implementations move to `tests/reference/` with a differential test. Engineering-only changes leave `tests/golden/` unchanged; algorithm changes regenerate goldens in a separate commit. After each E/A change optionally append a line via `tools/bench_step.py` (indicative); the claimed numbers are the A/B session. A3 must not regress baselines by > 2 pp utilisation or > 5% p95 delay without explanation in `docs/decisions.md`. Profile Phase 1 first into `docs/profile_p1.md`.

## 10. Observability
Loki push `POST /loki/api/v1/push`, timestamps unix-epoch ns strings, `base = now - longest trace length`, strictly increasing within a stream, batches 1000–5000, backoff on 429/5xx, raised limits in `deploy/loki-config.yaml`. Compose pins exact image versions, datasource uid `loki`. Dashboard v1: rate vs capacity, queue delay with budget line, in-flight, summary table, variables trace/buffer/phase. v2: phase comparison, budget sweep, improvement table, engineering panel.

## 11. Claims policy
Every number in README/docs comes from `results/*.json` through `tools/analysis.py`. Record the environment in every results file; never mix machines. State for each hypothesis whether it was met and on how many of 10 traces; list every trace where BudgetCC loses. README states: baselines are own simplified implementations; Phase 1 was intentionally unoptimised; no comparison against SCReAM/Copa; tuning used the fixed split. Never claim "first".

## 12. Out of scope
Radio-layer modelling, ECN/L4S/AQM, real codec, kernel work, multipath, real-socket mode, QUIC, RTCP stack, fuzzing, CI performance gate.

## 13. Pitfalls
MSVC: NOMINMAX, WIN32_LEAN_AND_MEAN, `_USE_MATH_DEFINES`, `/utf-8`, `/Zc:__cplusplus`; httplib needs `_WIN32_WINNT=0x0A00` and ws2_32. Number formatting via fmt only. TSan on GitHub runners: `sudo sysctl vm.mmap_rnd_bits=28`. Loki rejects far-future timestamps. No Docker locally -> CI smoke job + manual screenshot step. Determinism is per binary.
