# Architecture

A summary view. [docs/01-design.md](01-design.md) is the source of truth for every contract; this page orients a reader first.

## Units

```
include/portcullis/
  core/     Untrusted ──(friend)──> Cursor        the one door, the one read path
            Error, Result                          failures are values, not exceptions
            fixed_string                           field names as NTTP literals
  schema/   primitives, Field/Struct, Bytes,       schemas are TYPES; they do nothing
            Array, Choice/Case, Optional, Refine,
            Ref("f") / Tag("f")                     references as structural NTTP values
  visit/    for_each_field<S>                       the ONE schema walker
            fixed_prefix_v / is_fixed_v / fixed_size_v   compile-time layout
  parse/    parse<S>(Untrusted) -> Result<Parsed<S>>    reads via Cursor only
  fuzz/     Generator<S>, mutate<S>, PORTCULLIS_FUZZ_TARGET   same walker as parse
  arena/    TaggedArena, materialize<S>             optional ARM MTE hardening
```

**Dependency rules.** `schema` is types only. `visit` is the only module that knows how to walk a schema. `parse` and `fuzz` are both consumers of `visit` and know nothing of each other. `arena` depends on `core` only. Nothing outside `core` touches raw memory.

## One walker, two products

The parser and the fuzzer walk a schema with the **same** `for_each_field<S>`, and the fuzzer resolves `Ref`s by name exactly as the parser does. There is no second description of a format anywhere: if a schema compiles, it parses, it fuzzes, and its fixed layout is known at compile time. This is what makes the fuzzer trustworthy as a test of the parser — they cannot drift apart.

## Run-time data flow

1. `parse<S>(Untrusted in)` constructs a `Cursor` over `in` — the only thing that can read its bytes.
2. For a `Struct`, the parser checks the compile-time `fixed_prefix_v<S>` in a single bounds check, then reads each field in declaration order. Every read goes through `Cursor::take`/`read`, which checks `remaining()` first and leaves the cursor untouched on failure.
3. A `Bytes<Ref("len")>` resolves `len` from an already-parsed field and checks `len <= remaining` before any span exists. An `Array<T, Ref("n"), Cap>` rejects `n > Cap` before allocating. A `Refine<Pred, "name">` runs `Pred` over the fields parsed so far; a failure is `RefinementFailed{"name"}`.
4. The first failure stops the parse and returns an `Error` with the field path, kind and offset — after the first failure the remaining bytes have no meaning. On success, a `Parsed<S>` is constructed: integer fields are copied out, byte ranges are `std::span` views into the original buffer whose length the contract established. `Parsed<S>` is immutable, and there is no way to obtain a span before every check has passed.

## The three frozen contracts

- **`Untrusted` / `Cursor`** (`core/`) — the boundary. `Untrusted` offers only `size()`; `Cursor` is the single checked read path (D-010). A compile-fail test proves `Untrusted` has no element access.
- **The schema combinators** (`schema/`) — the vocabulary. Field names are literals (`Field<"len", U16BE>`); references are structural values (`Bytes<Ref("len")>`, `Choice<Tag("type"), …>`), because a class-type NTTP is the one C++20 form giving both `Bytes<16>` and `Bytes<Ref("len")>` from a single template (D-007).
- **`parse<S>` / `Parsed<S>` / `Error`** (`parse/`) — the result. Errors are values; the path names the field.

Amendments to these after Wave 0 go through `docs/change-requests/`, not silent edits. The full frozen-signature list is Appendix A of [docs/01-design.md](01-design.md).

## Where the safety actually comes from

Not from runtime checks a caller might forget — from the type system making the unsafe path unrepresentable. You cannot read `Untrusted` without a schema; you cannot get a `std::span` out of a `Parsed` that was not length-checked; you cannot write a `Ref` to a later field or a non-integer field, because it does not compile. The six replayed CVEs ([CVES.md](../CVES.md)) are the evidence that the contracts a careful author writes are exactly the ones that catch the bugs.
