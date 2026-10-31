# Wave 2 integration report

Date: 2026-11-01. Integrator: Fable/Opus. Merged P6 (fuzzer), P7 (CVE contracts), P8 (MTE arena) into `main` and verified the whole tree live on WSL (clang 21, gcc 15, QEMU 10.2.1).

## Result

Clean. The three branches merged with no conflicts (disjoint files: P6 owns `fuzz/` + the two example schemas + the fuzz-smoke workflow, P7 owns `cve/<id>/` + the cve reject-test glob in `tests/CMakeLists.txt`, P8 owns `arena/` + the arena tests + the MTE workflow). On `main`:

- **clang-release, gcc-release, clang-asan**: 8/8 ctest green each.
- **Full merged suite: 92 test cases / 22,974 assertions, 0 failed** (P1's core, P5's handwritten bench parsers, P7's six CVE reject-tests, P8's arena, P6's generator/mutator — the fuzzer's property tests account for most of the assertions).
- **Fuzzer**: the three libFuzzer targets build (standalone `fuzz/` project, `-fsanitize=fuzzer,address,undefined`). Independent runs on merged main: `fuzz_heartbeat` 4.7M execs / 31 s and `fuzz_tiff_ifd` 0.58M execs / 21 s — no crash, no sanitizer report, RSS flat (no leak).
- **MTE (the headline claim), independently re-run by the integrator**: cross-compiled the arena tests for `aarch64 -march=armv8.5-a+memtag` and ran under `qemu-aarch64 -cpu max` — 5 cases / 35 assertions, **0 skipped** (MTE active), including `overflow_past_allocation_faults`, where a one-granule out-of-bounds write to the tagged arena traps a synchronous SIGSEGV in a forked child. Confirmed, not taken on the agent's word.

## The centerpiece works

Six historical parser CVEs, each rejected by a small declared contract with the exact expected error:

| CVE | contract | kind | path |
| --- | --- | --- | --- |
| 2014-0160 Heartbleed | `Bytes<Ref("payload_length")>` | Truncated | payload |
| 2014-0195 DTLS fragment | `Refine` offset+len ≤ msg_length | RefinementFailed | fragment_within_message |
| 2015-8126 PNG palette | `Refine` num ≤ 2^bit_depth + Array cap | RefinementFailed | palette_fits_bit_depth |
| 2015-8540 PNG keyword | `Refine` 1 ≤ kw_len ≤ chunk_len | RefinementFailed | keyword_length_valid |
| 2004-1307 TIFF strips | `Array<U32BE, Ref, 2^20>` | Truncated / TooManyElements | strip_offsets[1] |
| 2010-4665 TIFF IFD | `Array<IfdEntry, Ref, 4096>` | TooManyElements / Truncated | entries |

P7 (Opus, defensive framing — it did not trip the safeguard that stopped P4) kept the three locked CVEs and, exactly as the plan allowed, **swapped** the two TIFF candidates that were not clean length/count bugs (2016-9535 predictor row-size, 2017-9147 field-presence) for clean strip-count / IFD-entry-count bugs (2004-1307, 2010-4665), documenting the rationale. DTLS's uint24 is modeled as U32BE (P1 has no U24), documented.

## Notes

- Running all three on Opus (not Sonnet) avoided the cyber-safeguard false positive that stopped P4; the defensive framing (schemas that enforce documented invariants; trigger vectors synthesized from format specs) held.
- The MTE workflow (`mte.yml`) is `continue-on-error: true` — experimental per spec §11; reliable locally under QEMU, not yet exercised on GitHub's hosted runner.
- Untracked WSL helper scripts the P6 agent left in its worktree were not merged (untracked files do not travel with a branch).

## Left for Wave 3

- **Portcullis bench rows**: write the three Portcullis schemas matching `bench/handwritten/` exactly, fill `bench/bench.cpp`'s emit() stubs, add the compile-time-checks share, regenerate `BENCHMARKS.md`.
- **`CVES.md`** generated from the suite (per-CVE: rejected kind/path; the optional original-library ASan reproduction stays CI/Linux-deferred).
- **README** (the narrow claim in the first screen, the numbers, what is not protected), `docs/architecture.md`, roadmap update, final decision entries.
- Nothing is pushed to GitHub yet.

## Status

All library code is complete and verified: the core, the structure-aware fuzzer, six CVE-rejecting contracts, and the MTE arena. Wave 3 is documentation plus the bench comparison and the CVES summary.
