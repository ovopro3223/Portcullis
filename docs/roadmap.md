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
