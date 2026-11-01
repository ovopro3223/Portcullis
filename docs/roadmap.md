# Roadmap

Deliberately out of scope for v1 (spec §12). Not ordered by priority.

- **C API facade** over pre-declared contracts — the guarantees live in the C++ type system (D-005); a C surface over a fixed set of contracts can wrap it without changing the core.
- **Text formats** (JSON, XML, HTTP headers) on the same bytes-in/types-out core; v1 does not build them.
- **Deep container formats** (PDF, zip) — too large for a first proof.
- **The write side** — serialization from a `Parsed<S>` back to bytes.
- **Generated documentation** from a schema (the third product of the one-walker design, after parser and fuzzer).
- **MSVC support** (D-006 is best-effort only in v1).
- **CHERI** as a second hardware backend beside MTE.
- **More CVE families** once the first three are proven.

## Discovered during implementation (Wave 2–3)

- **Out-of-line offsets / seeking.** v1 is a linear parser: it reads fields in order. A format whose entries point to values elsewhere by offset (TIFF, many container formats) is only parsed as its linear table — following offsets would need a seek primitive and a notion of the whole buffer, not just what remains. The TIFF benchmark compares against the handwritten `parse_tiff_ifd<false>` (table only) for this reason.
- **Runtime-selected endianness.** Primitives are fixed-endian (`U16BE` vs `U16LE`). A format with a byte-order mark (TIFF `II`/`MM`) needs either two schemas or a primitive whose endianness is chosen at parse time from an earlier field.
- **Sentinel-terminated arrays.** A stream that repeats until a marker (PNG chunks until `IEND`) has no count field, so v1 has no single-schema form for it; a consumer parses one element schema at a time. A `Repeat<T, Until<…>>` combinator would close this.
- **Non-allocating `Parsed`.** `Array<T, …>` materializes a `std::vector`, which dominates the parse cost for array-heavy formats (TIFF in BENCHMARKS.md). A lazy or caller-provided-storage `Parsed` would cut that.
- **Layout report in BENCHMARKS.md.** The "checks resolved at compile time" and "compile time per schema" sections are still placeholders; a small tool emitting `fixed_prefix_v`/`field_count_v` per schema and per-schema compile wall-time would fill them.
