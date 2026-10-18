# Progress

Start: 2026-10-18T08:40:29Z (UTC)

## Tools (G0)
- git 2.x, CMake 4.0.1, Python 3.14.0, pytest, ninja (pip), g++ 14.1.0 (MSYS2 UCRT64), gh (not authenticated; pushing via git credential manager).
- Missing locally: MSVC, clang, docker, WSL distro, sanitizer runtimes. These run in CI only.

## Gates
- G0-G3: done (commits 32408f6..2ba8bf6); lines backfilled at G4 close. 171 C++ tests + 21 pytest green locally.
- G4: telemetry, Loki pusher, compose, dashboard v1, smoke tool, loki-smoke CI job. Docker unavailable locally: smoke job runs in CI; dashboard screenshot is a manual step.
- G4 fixes: flush() raced the drain loop (now only a drain started after the request clears it); MSVC C4244 in summary.cpp; Loki mocks made race-free. A TSan segfault in LokiPusher tests appeared once and did not reproduce on rerun (cause unknown, watching).
- G5: Phase 1 matrix 250/250 runs (results/phase1.json, git f110d12, matrix 5637 s serial), bench_p1.json, README block generated; tag v0.1-baseline. CI status unverifiable locally (gh not authenticed).
- Plan cut (user request, speed): no retroactive per-step A/B builds; one interleaved P1-vs-P2 A/B session at the end; A4 cut.
- Plan trimmed (user): speed mode in CLAUDE.md section 1; cuts: A4, dashboard v2, per-step benches, fresh-clone reproduce.
- A2 cut (sweep ran: best k .25/margin .1/w_down .3 on tune traces, not applied; spec defaults kept).
- G6-G8: E1-E7, --jobs, A3, A1 done; phase2.json 250/250; A/B bench: M7 1.54x, M8 1.53x, M9 0, M10 370x (subset). Tag v1.0-optimised.
