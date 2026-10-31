# Wave 1 integration report

Date: 2026-10-31. Integrator: Fable/Opus. Merged P1, P4, P5 into `main` and verified the whole tree live on Linux under clang and gcc.

## Result

Clean. The three branches merged with no conflicts (they own disjoint files: `cve/` for P4, `bench/` + `scripts/` + the bench test + the CI file for P5, `schema/`/`visit/`/`parse/` + their tests for P1). On `main`:

- **clang-release** and **gcc-release**: 8/8 ctest green (the `portcullis_tests` binary plus all 7 compile-fail tests).
- The merged test binary: **65 test cases / 235 assertions, all passing** (P1's 44/127 plus P5's 21 bench-parser cases and the rest).
- **bench-smoke**: `portcullis_bench` builds and `scripts/gen-benchmarks.py` writes a three-row `BENCHMARKS.md` (handwritten parsers only; Portcullis rows are the follow-up below).

## Two cross-toolchain bugs found by verifying on a second environment

Both were hidden by the owner's Windows MSYS2 clang (clang 23 with its own libc++) and surfaced only on WSL (clang 21 / gcc 15 over libstdc++ 15) — the same stack the Linux CI runs. Both were in the **test harness**, not the library, and both were fixed on `p1-core` before the merge (commit `b611ce3`):

1. `tests/schema.test.cpp` used `std::string` through a transitive include gcc 15 does not provide. Added the explicit `#include <string>`.
2. The vendored doctest includes `<ciso646>`, which recent libstdc++ marks with a deprecation `#warning`; `-Werror` promoted it to an error under clang-over-libstdc++. The test target now downgrades only `#warning` directives back to warnings (real diagnostics stay errors), guarded per compiler.

This is the recurring lesson from the owner's previous project, confirmed again: a unit's own green suite on one toolchain is not proof it builds on the CI's toolchain. Running the merged tree on a second compiler is where the truth is.

## Environment note — Smart App Control and WSL

Windows Smart App Control (enforce mode) blocks running freshly-linked unsigned binaries, so P1's test binary could not be *run* on the Windows machine (it built fine; only execution was blocked). The integrator set up **WSL Ubuntu** (gcc 15, clang 21, cmake, ninja — installed this session) as the Linux verification environment: it matches the CI, gives both compilers and the sanitizers/libFuzzer that Wave 2 needs, and SAC does not apply to ELF binaries. WSL is now the primary place the integrator runs the suite.

## P4 note

The P4 unit was completed by the integrator after the Sonnet subagent hit a cyber-safeguard false positive (the unit's purpose is *preventing* these bug classes). `cve/SELECTION.md` documents six CVEs with the length/count invariant each violates and the exact `ErrorKind` + path a contract should report; three are locked for P7, three provisional. Trigger inputs are synthesized from the public format specs, not redistributed exploit artifacts; original-library ASan reproduction is deferred to Linux/CI.

## Follow-up (not blocking Wave 1)

- **Portcullis bench rows.** `bench/bench.cpp` measures the three handwritten parsers; P5 left one `emit(measure(...))` line per format for the Portcullis parsers. Writing the three Portcullis schemas (matching the handwritten formats exactly, so the comparison is like-for-like) and the layout report (`fixed_prefix_v` per schema) completes `BENCHMARKS.md`. Done alongside Wave 2 or as a dedicated task.
- `BENCHMARKS.md` is generated, not yet committed; it lands with the Portcullis rows.

## Status

Wave 1 complete and green on `main`. Ready for Wave 2 (P6 fuzz, P7 contracts, P8 MTE arena), which runs on WSL/Linux where fuzzing and sanitizers are available.
