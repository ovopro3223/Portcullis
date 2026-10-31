# Build plan and agent protocol

The full task-by-task plan is `docs/superpowers/plans/2026-10-31-portcullis.md`. This page is the wave structure and the protocol the agents work under.

## Waves and ownership

| Wave | Unit | Owner | Depends on |
| --- | --- | --- | --- |
| 0 | Toolchain, scaffold, core, decisions, briefs, CI skeleton | integrator | — |
| 1 | **P1** schema + visit + layout + parse | strongest agent | Wave 0 |
| 1 | **P4** CVE research, vulnerable builds, PoCs | agent | Wave 0 |
| 1 | **P5** handwritten bench parsers, bench runner, CI matrix | agent | Wave 0 |
| 2 | **P6** fuzz generator, mutator, libFuzzer hook | agent | P1 |
| 2 | **P7** six CVE contracts + reject tests (one agent per family) | 3 agents | P1, P4 |
| 2 | **P8** TaggedArena + MTE + QEMU job | agent | P1 |
| 3 | Integration: run everything live, wire harnesses, generate CVES.md / BENCHMARKS.md, README, reports | integrator | all |

Wave 1's P4 and P5 need no library code and run alongside P1. Wave 2 starts once P1 is merged (P7 also needs P4).

## Agent protocol

1. **One unit, one worktree.** The integrator creates it: `git worktree add ../Portcullis-worktrees/<branch> -b <branch> main`. The agent works only inside its own worktree and touches only its unit's files. It does not edit another unit's headers; if a frozen interface (docs/01-design.md Appendix A) genuinely needs to change, it writes a note in `docs/change-requests/` and stops.
2. **Driven by a brief.** Each agent reads its `docs/briefs/<P>.md`, plus `docs/01-design.md` (the spec and frozen interfaces) and its tasks in the full plan. The brief names the tasks, the interfaces consumed and produced, the test names, and what "done" means.
3. **TDD.** Write the failing test, run it to see it fail, implement the minimum, run it to see it pass, commit. Frequent commits.
4. **Report.** On finishing, write `docs/reports/<P>-report.md`: what was built, every test that runs (names), measurements, and open questions — anything the frozen interfaces forced that reads wrong, so the integrator can weigh a change request.
5. **Integrator gates.** The integrator re-verifies every agent's claims by building and running the real thing, not by trusting the report, then merges. Bugs that only appear once units are merged and run together go in `docs/reports/waveN-integration-report.md`. (This is the lesson that paid off repeatedly on the owner's previous project: a unit's own green suite is not proof the system works.)

## Verification commands

```bash
export PATH="/c/msys64/clang64/bin:$PATH"   # Windows (MSYS2 clang64)
cmake --preset msys2-clang64
cmake --build --preset msys2-clang64
ctest --preset msys2-clang64 --output-on-failure   # unit + compile-fail tests
```

CI runs clang and gcc on Linux, an ASan build, and the MSYS2 clang64 build on Windows; fuzz targets and the MTE/QEMU job are Linux-only. The repository is pushed only when the owner says so; the integrator reads the first CI run with `gh run view --log-failed`.
