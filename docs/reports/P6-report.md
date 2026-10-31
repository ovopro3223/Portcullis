# P6 — structure-aware fuzzing — report

Worktree `p6-fuzz`, branch `p6-fuzz`, built on P1 (frozen) as merged into `main`.
Measured in WSL Ubuntu: clang 21.1.8, gcc 15.2.0, cmake 4.2.3, ninja 1.13.2.

## What was built

One schema walk, two products, plus the libFuzzer plumbing — all driven by the
same `for_each_field` and combinator traits the parser uses. No second
description of any format exists in the fuzzer.

| File | What it is |
| --- | --- |
| `include/portcullis/fuzz/generator.hpp` | `Generator<S>` — `valid()` and `violating()` |
| `include/portcullis/fuzz/mutator.hpp` | `mutate<S>(buf, size, max_size, seed)` |
| `include/portcullis/fuzz/libfuzzer.hpp` | `PORTCULLIS_FUZZ_TARGET(S)` |
| `tests/schemas/png_chunks.hpp` | PNG-style container: Ref-counted array (+cap), Ref length, Choice tag |
| `tests/schemas/tiff_ifd.hpp` | TIFF IFD: Ref-counted array (+cap), Ref length, value-range Refine |
| `fuzz/targets/{heartbeat,png_chunks,tiff_ifd}.cpp` | the three libFuzzer targets |
| `fuzz/CMakeLists.txt` | standalone clang+Linux project, `-fsanitize=fuzzer,address,undefined` |
| `.github/workflows/fuzz-smoke.yml` | 60 s per target on a matrix, ASan+UBSan |

The frozen Appendix-A signatures are implemented exactly as written; **no change
requests were needed** (`docs/change-requests/` is untouched).

### Design notes

- **References are resolved by name**, through a value map built as the walk
  proceeds, exactly as the parser resolves a `Ref`. A generated length/count/tag
  is therefore always consistent with the bytes that follow it.
- **Refinements are not inverted** (spec §6). `valid()` keeps unreferenced
  ("free") fields small so range refines pass by construction, and verifies each
  candidate with `parse<S>`; `violating()` generates candidates and lets the
  contract reject them. The `tiff_ifd` `magic_in_range` refine exercises this.
- **One violation at a time.** `violating()` fixes the violation kind per seed
  (so coverage spreads across seeds) and injects exactly one defect into an
  otherwise valid instance: a truncation, a bad tag byte (body still a real
  case), an over-cap count byte (zero elements emitted), or free fields drawn
  wide to trip a refine.
- **The mutator bootstraps.** When an input does not scan (truncated, over-cap
  count, unknown tag), `mutate` usually seeds a freshly generated valid/violating
  structure instead of flipping noise. This is what lets targets with a
  multi-byte fixed prefix (`tiff_ifd` = 4, `heartbeat` = 3) reach deep parser
  states quickly: without it, `tiff_ifd` stalled at cov 33; with it, cov 321.
- **`fuzz::last_op`** is a `thread_local` observability hook (not part of the
  frozen interface) so `field_level_ops_are_reachable` can confirm each
  field-level op actually fires.

## Tests — all green

Full suite: **73 doctest cases, 22,881 assertions, 0 failed**, on `clang-asan`
and `gcc-release` (65 from P1 + the 8 below). The `mutate` tests are built with
the `clang-asan` preset, as the brief requires. All 8 compile-fail (`cf.*`)
tests still pass.

Generator (`tests/fuzz_generator.test.cpp`):
- `valid_outputs_parse` — 1000 seeds × {heartbeat, png_chunks, tiff_ifd}, every output parses.
- `valid_hits_boundaries` — heartbeat length 0; png count 0 and the cap (16); every Choice case (0/1/2) all appear.
- `deterministic_per_seed` — same seed reproduces `valid()` and `violating()`.
- `violating_outputs_fail` — 1000 seeds × 3 schemas, every output fails to parse.
- `violations_cover_each_kind` — over 1000 seeds, each schema shows every kind it can produce: heartbeat {Truncated}; png {Truncated, BadTag, TooManyElements}; tiff {Truncated, TooManyElements, RefinementFailed}.

Mutator (`tests/fuzz_mutator.test.cpp`, built under `clang-asan`):
- `mutate_respects_max_size` — 10,000 iterations over random and generated inputs across all three schemas; the return is never above `max_size` and ASan reports no out-of-bounds read/write (Review Focus 5).
- `field_level_ops_are_reachable` — from a valid PNG, length-change, tag-swap and truncate-at-boundary all occur.
- `unparseable_input_still_mutates` — a non-scanning input is always changed and the size stays > 0.

## 60-second fuzz runs (WSL, `-fsanitize=fuzzer,address,undefined`)

Each target ran 60 s. **No crash, no sanitizer report** on any of them.

| target | runs | exec/s | cov | features | corpus | peak RSS |
| --- | --- | --- | --- | --- | --- | --- |
| `fuzz_heartbeat` | 9,118,860 | ~149k | 154 | 170 | 4 | 508 MB |
| `fuzz_png_chunks` | 2,143,855 | ~35k | 412 | 1100 | 25 | 438 MB |
| `fuzz_tiff_ifd` | 1,840,477 | ~30k | 321 | 819 | 16 | 434 MB |

RSS plateaus (no unbounded growth, no leak — LSan reported none), well under the
2048 MB default limit.

## What I could not run here

- **`.github/workflows/fuzz-smoke.yml` on GitHub Actions** — the repo is not
  pushed (by design; the integrator pushes). The workflow is verified only by
  reproducing its exact steps locally in WSL (configure `fuzz/`, build, run each
  target 60 s). It installs `clang cmake ninja-build` on `ubuntu-latest`, matching
  `ci.yml`.
- **Windows / MSVC** — libFuzzer and the sanitizers are clang+Linux only (spec
  §11); `fuzz/CMakeLists.txt` fails fast elsewhere. Windows coverage stays with
  the unit and compile-fail tests in `ci.yml`.
- **CVE-contract fuzzing** — driving the P7 contracts and the original parsers
  with this mutator is Wave 3 (Task 9.2), not P6.

## Open questions for the integrator

- The two example schemas (`png_chunks`, `tiff_ifd`) are shaped to exercise the
  constructs, not to be faithful format contracts. If P7 lands real PNG/TIFF
  contracts with the same names under `tests/schemas/`, deconflict there.
- `fuzz/` is a standalone CMake project so P6 need not touch the top-level
  `CMakeLists.txt`. If you prefer it wired in as `add_subdirectory(fuzz)` under a
  `PORTCULLIS_BUILD_FUZZ` option, that is a one-line top-level change.
