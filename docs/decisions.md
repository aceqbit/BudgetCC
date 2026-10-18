# Decisions

Format: decision / alternative / reason. Items 1–12 are inherited from `CLAUDE.md` section 4 (see there for the full text); items below them were made during implementation.

1. Credit accumulates only while backlogged; resets to 0 when the queue empties. Alt: unlimited credit accumulation. Reason: spec says idle opportunities are wasted.
2. 12-byte RTP header, 16-bit seq unwrapped at receiver. 3. Window controllers: pacing = gain x cwnd/srtt. 4. Video mode per CLAUDE.md. 5. Metric definitions per CLAUDE.md. 6. 60 fairness runs, matrix = 250. 7. Video BudgetCC D=150. 8. Bench definitions. 9. Tuning split fixed before sweep. 10. Real traces fetched with SHA-256 pinning. 11. Loki labels incl. budget and mode. 12. Phase 1 results committed.

## Implementation decisions

13. **Toolchain locally.** MSYS2 UCRT64 g++ 14.1 + CMake + Ninja (pip). Alt: MSVC. Reason: no Visual Studio or Docker or WSL distro on the development machine. MSVC, Clang, sanitizers and coverage run in CI only; they are unverified locally.
14. **Dependencies.** CI uses the vcpkg manifest (baseline `bf19794…`). Local builds fall back to CMake FetchContent at pinned tags (fmt 11.0.2, CLI11 v2.4.2, cpp-httplib v0.18.1, googletest v1.15.2). Alt: pacman packages. Reason: pacman install failed on a dependency conflict.
15. **Power plan.** Benchmarks run under the machine's active plan (recorded in `env.power_plan`), not switched to High performance. Alt: `powercfg /setactive`. Reason: changing system power settings was not requested; the plan is recorded so results are honest.
