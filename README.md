# Portcullis

**Contracts for untrusted bytes in C++.** A header-only C++20 library through
which raw input can only be read by way of a declared schema, so a parser
written with it cannot read or write outside the bounds the contract
establishes.

> Status: in development. Narrow claim, measured honestly — a parser written
> *through* Portcullis cannot read or write out of bounds; code that does not
> go through the library is not protected by it. Evidence (replayed parser
> CVEs, structure-aware fuzzing, benchmarks) lands with the waves; see
> `docs/superpowers/plans/2026-10-31-portcullis.md`.

```cpp
using Heartbeat = portcullis::Struct<
  portcullis::Field<"type",    portcullis::U8>,
  portcullis::Field<"len",     portcullis::U16BE>,
  portcullis::Field<"payload", portcullis::Bytes<portcullis::Ref("len")>>,
  portcullis::Field<"padding", portcullis::Bytes<16>>
>;
auto r = portcullis::parse<Heartbeat>(portcullis::Untrusted(bytes));
// CVE-2014-0160 input (declared 65535, one byte present) -> Error{path:"payload", kind:Truncated}
```

Free tooling only. MIT licensed.
