# Decisions

Recorded when made. Later entries override earlier ones. Each: what, why, what it rules out.

## D-001 — Project purpose (2026-10-31)

A header-only C++20 library through which untrusted bytes can only be read via declared contracts, so a parser written with it cannot read or write outside the bounds the contract establishes. Open-source, and a portfolio piece: the planning, decisions, agent briefs and integration reports are part of what it demonstrates.
Rules out: a general "prevents buffer overflow" claim (false — code that does not go through the library is unprotected); anything whose safety rests on convention rather than the type system.

## D-002 — Working name: Portcullis (2026-10-31)

The castle gate that decides what gets in. Rename allowed any time before first public release; namespace `portcullis`, final name is a later decision.

## D-003 — Scope v1 (2026-10-31)

Binary formats with fixed or length-prefixed structure (headers, TLV, chunked containers, arrays with count fields, tagged unions). In: the core library, a structure-aware fuzzer from the same schema, an optional ARM MTE arena, a six-CVE replay suite (2 TLS, 2 PNG, 2 TIFF), benchmarks, CI.
Rules out for v1: a C API, text formats (JSON/XML/HTTP), deep containers (PDF/zip), the write side, CHERI. All recorded in the roadmap.

## D-004 — Free tooling, zero library dependencies (2026-10-31)

The library has no dependencies. Tooling is clang/LLVM, gcc, CMake, Ninja, doctest, libFuzzer and the clang sanitizers, QEMU, GitHub Actions — all free. Test/tooling dependencies are vendored or pinned and listed in `THIRD_PARTY.md`.

## D-005 — C++20, header-only, guarantees in the type system (2026-10-31)

The protection is that the type system makes raw access unrepresentable: `Untrusted` has no element accessor, `parse<>` is the only consumer, `Parsed<S>` exists only after every check passed. This needs templates and compile-time evaluation, so the library is C++20 header-only.
Rules out: a runtime-only "validator" that can be bypassed; a C-macro core.

## D-006 — Compilers (2026-10-31)

clang and gcc on Linux (CI), clang on Windows (the owner's machine, D-009). `-std=c++20 -Wall -Wextra -Werror` on both. MSVC is best-effort: nothing in the design depends on it, nothing is blocked on it, no workaround is written for it.

## D-007 — NTTP syntax: names as literals, references as values (2026-10-31)

Field names are string literals (`Field<"len", U16BE>`). References are structural NTTP values: `Bytes<Ref("len")>`, `Array<T, Ref("count")>`, `Choice<Tag("type"), ...>` — with parentheses, not angle brackets.
Why: a class-type non-type template parameter (`template <auto>`) is the one C++20 form that gives both `Bytes<16>` and `Bytes<Ref("len")>` from a single template. `Ref`/`Tag` are small structural types wrapping a `fixed_string`.
Rules out: a separate `BytesRef` combinator; a string-only reference that could not also be a compile-time integer length.

## D-008 — Array element cap, default 4096 (2026-10-31)

`Array<T, Ref("count"), Cap>` rejects with `TooManyElements` before parsing any element when the count exceeds `Cap` (default 4096). A count field cannot make the parser do unbounded work or allocate unboundedly.
Rules out: trusting a count field; discovering the cap only after a large allocation.

## D-009 — Windows toolchain: MSYS2 clang64 (2026-10-31)

`winget install MSYS2.MSYS2`, then `pacman -S mingw-w64-clang-x86_64-{clang,compiler-rt,lld,cmake,ninja}`. A clang with libc++/compiler-rt and a Unix-like environment that matches the Linux fuzzing/sanitizer story. Verified on this machine: clang 23.1.4, cmake 4.4.5, ninja 1.13.2.
Rules out: a Visual Studio dependency; plain `LLVM.LLVM` over the MSVC STL.

## D-010 — One read path (2026-10-31)

Every byte read in the library goes through `Cursor::take`/`Cursor::read`, which check `remaining()` before touching a byte and leave the cursor untouched on failure. There is no other read path. The `fixed_prefix` early-out (D-005, spec §5.3) is an additional single check, not a bypass.
Rules out: a hand-rolled read anywhere in `parse`/`fuzz`/`arena`; reading an untrusted length into a `memcpy` size.

## D-011 — Agent execution model (2026-10-31/11-01)

Each unit is built by a Claude agent in its own git worktree against the frozen interfaces (Appendix A), with an integrator (this session) who re-verifies every unit live before merging — not on the agent's report. Verification runs on WSL Linux under both clang and gcc; this caught cross-toolchain bugs a single Windows clang build hid (missing `<string>`, a doctest `<ciso646>`/`-Werror` interaction).
Wave 2's units (fuzzer, CVE contracts) were run on **Opus specifically**, because the Sonnet real-time cyber safeguard flagged the (defensive) CVE-replay work as a false positive and stopped the P4 Sonnet agent; Opus with explicit defensive framing (schemas that enforce documented invariants; trigger vectors synthesized from format specs, never exploit artifacts) completed them without issue.
Rules out: trusting an agent's "green" report without a live re-run; using Sonnet for the CVE/fuzz units.

## D-012 — CVE selection may swap provisional candidates; benchmarks compare like-for-like (2026-11-01)

The six CVEs are chosen so each is a bounds violation driven by a length or count field expressible as a contract. Three are locked (Heartbleed, DTLS, PNG palette); a provisional candidate that turns out not to be a clean length/count bound is swapped for one that is, in the same library, with the reason recorded in its `README.md` (so CVE-2016-9535 predictor and CVE-2017-9147 field-presence became CVE-2004-1307 strip-count and CVE-2010-4665 IFD-entry-count).
Benchmarks compare Portcullis against a *careful* hand-written bounds-checked parser on identical bytes, validating the same structure (the honest cost, not a strawman). Where Portcullis's linear model does less than the hand-written parser — following TIFF out-of-line offsets — the comparison uses the handwritten `<false>` table-only variant and says so; the gap is a roadmap item, not hidden in the number.
Rules out: keeping a CVE that cannot be expressed as a bounds contract; a benchmark that compares unlike work or flatters either side.
