# Progress

Start: 2026-10-18T08:40:29Z (UTC)

## Tools (G0)
- git 2.x, CMake 4.0.1, Python 3.14.0, pytest, ninja (pip), g++ 14.1.0 (MSYS2 UCRT64), gh (not authenticated; pushing via git credential manager).
- Missing locally: MSVC, clang, docker, WSL distro, sanitizer runtimes. These run in CI only.

## Gates
- G0-G3: done (commits 32408f6..2ba8bf6); lines backfilled at G4 close. 171 C++ tests + 21 pytest green locally.
- G4: telemetry, Loki pusher, compose, dashboard v1, smoke tool, loki-smoke CI job. Docker unavailable locally: smoke job runs in CI; dashboard screenshot is a manual step.
- G4 fixes: flush() raced the drain loop (now only a drain started after the request clears it); MSVC C4244 in summary.cpp; Loki mocks made race-free. A TSan segfault in LokiPusher tests appeared once and did not reproduce on rerun (cause unknown, watching).
