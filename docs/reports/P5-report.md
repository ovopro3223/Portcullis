# P5 report: handwritten bench parsers, runner, report generator, CI

Branch `p5-bench`, worktree `Portcullis-worktrees/p5-bench`. Touched only `bench/`, `scripts/gen-benchmarks.py`, `tests/bench_handwritten.test.cpp`, `.github/workflows/ci.yml` and this file. Nothing under `include/portcullis/`.

## What exists

| Path | Role |
| --- | --- |
| `bench/handwritten/{tls_heartbeat,png_chunks,tiff_ifd}.hpp` | The three bounds-checked comparison parsers. `bytes.hpp` holds the unchecked `load_be/le16/32` they share. |
| `bench/samples.hpp` | Deterministic sample builders (TLS record, PNG with real CRCs, TIFF in either byte order). Shared by the tests and the runner, so a Portcullis row and a handwritten row parse the same bytes. |
| `bench/bench.cpp`, `bench/CMakeLists.txt` | Runner `portcullis_bench`; ctest entry `bench_smoke` (20 repetitions). |
| `scripts/gen-benchmarks.py` | JSON lines to `BENCHMARKS.md`. Stdlib only. |
| `tests/bench_handwritten.test.cpp` | 21 doctest cases, picked up by the existing `tests/` glob. |
| `.github/workflows/ci.yml` | `-Werror`, compile-fail step, `bench-smoke` in every job. |

Commits on `p5-bench`: TLS parser, PNG parser, TIFF parser, runner, generator, CI. Each parser landed test-first (test file written, build seen failing on the missing header, then implemented).

## Test names (`portcullis_tests`)

TLS: `tls_heartbeat_accepts_valid_record`, `tls_heartbeat_accepts_empty_and_maximum_payload`, `tls_heartbeat_rejects_heartbleed_record`, `tls_heartbeat_rejects_payload_length_past_end`, `tls_heartbeat_rejects_every_truncation`.

PNG: `png_chunks_accepts_valid_file`, `png_chunks_accepts_empty_data_chunks_and_ignores_bytes_after_iend`, `png_chunks_rejects_bad_signature`, `png_chunks_rejects_length_past_end`, `png_chunks_rejects_missing_iend`, `png_chunks_rejects_every_truncation`.

TIFF: `tiff_ifd_accepts_valid_file_in_both_byte_orders`, `tiff_ifd_sums_value_bytes`, `tiff_ifd_rejects_entry_count_past_end`, `tiff_ifd_rejects_bad_header`, `tiff_ifd_rejects_ifd_offset_outside_file`, `tiff_ifd_rejects_out_of_line_value_past_end`, `tiff_ifd_treats_values_up_to_four_bytes_as_inline`, `tiff_ifd_skips_unknown_field_types`, `tiff_ifd_rejects_every_truncation`, `tiff_ifd_table_only_variant_skips_value_range_checks`.

"Rejects every truncation" tries every strict prefix of a valid sample, not one hand-picked cut.

**The tests were mutation-checked.** For each parser check I removed it in turn (TLS payload and padding fit, PNG length fit, TIFF IFD-offset floor, per-entry fit, out-of-line offset check, 64-bit size arithmetic, inline/out-of-line boundary) and confirmed the suite fails or the binary faults. Two checks survived, both for reasons worth knowing: the PNG `length > 2^31 - 1` cap is a spec rule that no buffer under 2 GiB can distinguish from the fit check (kept, commented in the test); and an up-front whole-table check in the TIFF parser was behaviourally identical to the per-entry checks, so I removed it rather than keep dead logic.

## What each handwritten parser checks (the parity contract)

The ratio in `BENCHMARKS.md` is honest only if the Portcullis schema checks the same things. Each header states this at the top; summary:

- **TLS heartbeat**, per the design's `Heartbeat`: `type:U8, len:U16BE, payload:Bytes<len>, padding:Bytes<16>`. Checks that each piece fits. No constraint on `type`; bytes after the padding are ignored. RFC 6520 lets padding run to the end of the record (at least 16); the design fixes it at 16 and so does this.
- **PNG**: 8-byte signature; every chunk's `length+type+data+crc` fits, with `remaining < length` compared in `size_t` (never `pos + length`); length at most 2^31 - 1; the stream must reach an `IEND` chunk, so a buffer that stops early is "truncated". **Not checked:** CRC values, chunk ordering, chunk type letter-case bits, IEND length 0. Bytes after IEND are ignored (as libpng does); `end_offset` reports where the PNG ended.
- **TIFF**: header (`II`/`MM`, magic 42, IFD offset at least 8 and inside the file); each 12-byte entry and the trailing next-IFD word fit; for every entry of a known type (1-12) whose `n * size > 4`, the out-of-line range `[offset, offset + n*size)` lies inside the file, computed in 64 bits. Values of 4 bytes or fewer are inline and not range-checked. Unknown field types are skipped, as TIFF readers do. **Not done:** following the next-IFD chain, looking at tags, rejecting count 0.

## Decisions I made that the integrator should know

1. **`parse_tiff_ifd<false>` exists.** It skips only the out-of-line range check. Portcullis v1 reads a sequential cursor, so its TIFF schema may not follow offsets; if so, benchmark against `<false>` and say so in `BENCHMARKS.md`. The default (`true`) is the one that is safe to hand entries to a consumer. Do not silently compare Portcullis to the weaker variant.
2. **Entry-count limits differ.** The handwritten parser accepts up to 65535 entries (the field's maximum); `Array<T, Ref(...)>` caps at 4096 (D-008). Irrelevant to the benchmark sample (24 entries), relevant to hostile input.
3. **MB/s depends on the sample, not only the parser.** These parsers never read payload bytes, so MB/s scales with the payload share (PNG at 65 KB reads as 2.3 million MB/s because over 99% of it is IDAT). Rows carry `ns_per_parse` and a sample description for that reason, and `BENCHMARKS.md` says to compare parsers on the same row, never MB/s across formats.
4. **Method.** One clock read is longer than a heartbeat parse, so "N = 2000" means 2000 timed batches of back-to-back parses (batch calibrated to at least ~100 microseconds), median reported. This is throughput, not single-parse latency. Compiler barriers (`asm volatile` memory clobber on the input pointer, and on the result struct) stop the loop being hoisted; I checked the generated loop does the loads, compares and stores every iteration. Result structs are `static` so zero-init is not charged per parse.
5. **Optimisation is forced.** The presets build Debug; `bench/CMakeLists.txt` adds `-O2` unless the config is already Release-like, the runner records `optimized` per row, and the generator prints a warning banner if any row says `false`.
6. **A parser that rejects its own sample aborts the run**: timing a rejection measures nothing.

## Measured handwritten throughput

Windows 11, 24 logical CPUs, clang 23.1.4 (MSYS2 clang64), bench target `-O2`, 2000 batches, about a dozen runs across the session (ranges below; the early ones predate the `static` result-struct change, with no systematic difference):

| Format | Sample | ns / parse (range) | MB/s (range) |
| --- | --- | --- | --- |
| tls_heartbeat | 64 B payload, 83 B total | 0.48 - 0.64 | 129,000 - 173,000 |
| png_chunks | 13 chunks, 8 x 8 KiB IDAT, 65,757 B | 27.5 - 29.0 | 2.27 - 2.39 million |
| tiff_ifd | 24 entries, 448 B | 16.7 - 20.5 | 21,900 - 26,800 |

Run-to-run spread is 10-25% on this laptop; the generator's `BENCHMARKS.md` says so. Use ratios from a single run.

## Verification

- `cmake --preset msys2-clang64`, build, `ctest --preset msys2-clang64`: 4/4 pass with `-DPORTCULLIS_BUILD_BENCH=ON` (`portcullis_tests`, `cf.control`, `cf.untrusted_no_raw_access`, `bench_smoke`); 3/3 with it off.
- The exact CI Windows-job sequence (configure with `-DCMAKE_COMPILE_WARNING_AS_ERROR=ON`, build, `ctest -E '^cf\.'`, `ctest -R '^cf\.' --no-tests=error`, bench-smoke) was replayed locally from a clean build directory and passes.
- `python3 scripts/gen-benchmarks.py` writes the table with the three handwritten rows (and, tested with synthetic input, multi-parser rows with ratios, layout and compile sections, stdin input, a bad-JSON error and a missing-binary error). The generated `BENCHMARKS.md` is not committed: it lives at the repo root, outside this unit's files.
- `ci.yml` parses with PyYAML and has the required steps in both jobs.
- Extra clang warnings (`-Wconversion -Wsign-conversion -Wshadow -Wold-style-cast -Wuninitialized` and others) on the bench and test sources: clean apart from doctest's own `__COUNTER__` pedantic note.

## Not verified

- **CI has not run on GitHub.** The Linux jobs (gcc and ASan) were not executed; there is no gcc on this machine. I kept the code to constructs that compile identically under gcc and avoided the one pattern that tends to trigger gcc's `-Wmaybe-uninitialized` (uninitialised result structs passed by pointer), but the first push is the real test.
- The `clang-asan` bench-smoke builds the bench with sanitizers on: it checks the build and a short run, and the numbers are meaningless there.

## Environment gotchas (this machine)

- `python3` in the shell is the Microsoft Store stub and does nothing. The real interpreter is reachable as `py` (3.15) and `C:\Users\Apo\AppData\Local\Python\pythoncore-3.12-64\python.exe`. I ran the script with `py`; the script is plain Python 3.8+ with a `python3` shebang and runs unchanged on Linux CI.
- **Windows Code Integrity (Smart App Control) intermittently blocks a freshly linked unsigned exe** ("Application Control policy has blocked this file", event 3077, shows as `Permission denied` in bash and `BAD_COMMAND` in ctest). It hit `portcullis_bench.exe` twice, and a rebuild (`touch bench/bench.cpp`) produced a binary that ran. I did not touch any security setting. If a bench binary will not start, relink it.
- In the Bash tool here, a backslash-n typed inside a heredoc arrives as a real newline, which silently defeated a few scripted edits; I used the file tools for anything with backslashes.

## Handing off to the Wave 3 integrator

1. In `bench/bench.cpp`, add one line per format at the marked `INTEGRATOR` comments: `emit(measure(tls, "portcullis", n, <callable>));`, where the callable wraps the bytes in `Untrusted`, calls `parse<Schema>` and returns `r.has_value()`. Link target `portcullis::portcullis` is already on `portcullis_bench`. `escape()` the result so the parse cannot be elided. Nothing in the generator changes.
2. Write `bench/layout_report.cpp` printing JSON lines `{"format", "fixed_prefix_v", "fixed_fields", "total_fields"}` and a per-schema compile timer printing `{"schema", "compile_ms"}`; the generator renders both sections as soon as such rows are in its input (several `--input` flags can be combined, or concatenate the outputs).
3. Confirm the Portcullis schemas check what the "parity contract" above lists. Where they cannot (PNG length cap, CRC), say so beside the table rather than leaving the ratio to imply otherwise.
