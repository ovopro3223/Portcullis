# Portcullis

**Contracts for untrusted bytes, in C++.** A header-only C++20 library through which raw input can only be read by way of a declared schema — so a parser written with it cannot read or write outside the bounds the contract establishes.

```cpp
#include <portcullis/portcullis.hpp>
using namespace portcullis;

// The TLS heartbeat record — the one Heartbleed got wrong.
using Heartbeat = Struct<
  Field<"type",    U8>,
  Field<"len",     U16BE>,
  Field<"payload", Bytes<Ref("len")>>,   // len is checked against the bytes present
  Field<"padding", Bytes<16>>
>;

auto r = parse<Heartbeat>(Untrusted(bytes));
// CVE-2014-0160: a record declaring len = 65535 with one byte present ->
//   Error{ kind: Truncated, path: "payload", need: 65535, have: 1 }
if (r) use(r->get<"payload">());          // a length-checked std::span, never a raw pointer
```

That five-line schema rejects the Heartbleed input. [Five other historical parser CVEs](CVES.md) are rejected the same way — by the length or count contract the original code forgot.

## The claim, stated carefully

A parser written **through** Portcullis cannot read or write out of bounds: `Untrusted` has no way to read its bytes except through a schema, the single read path checks every access, and a `Parsed<S>` exists only after every field and refinement has passed. The guarantee is in the **type system**, not in the programmer remembering to check — a raw read of the input *does not compile* (there are tests that prove it).

It does **not** make an arbitrary program memory-safe. Code that does not go through the library is not protected by it. This is a tool for the one place a violated length field becomes a memory-safety event — the boundary where untrusted bytes are parsed — not a general defense. Everything below is measured, and a goal that is missed is reported as missed.

## The evidence

- **Six real CVEs rejected** ([CVES.md](CVES.md)). Heartbleed, an OpenSSL DTLS fragment overflow, two libpng bugs, two libtiff count-field bugs — each rejected by a small contract with a specific, named error, proven by a test that runs in CI on clang and gcc. Trigger inputs are synthesized from the public format specs, not redistributed exploits.
- **Structure-aware fuzzing**. One schema drives both the parser and a libFuzzer-based fuzzer (same walk of the fields, no second description). The three fuzz targets ran millions of executions each under AddressSanitizer with **no crash and no leak**; `fuzz_heartbeat` alone did 4.7M runs in 31 s.
- **Benchmarks, honest** ([BENCHMARKS.md](BENCHMARKS.md)). Against a careful hand-written, bounds-checked parser, Portcullis costs roughly **10–50×** (it materializes a typed `Parsed` result, and an array materializes a vector). In absolute terms it still parses millions of inputs per second — far above any network or disk rate — and the cost buys a compile-time guarantee the hand-written parser gets only from the author's vigilance.
- **Optional hardware tagging**. On ARM with MTE, `TaggedArena` holds validated output in tagged memory so an out-of-bounds access faults — verified under `qemu-aarch64 -cpu max`, where a one-granule overread traps a synchronous `SIGSEGV`. Experimental; heap only, 16-byte granule, detection not prevention.

The full suite is **92 test cases / 22,974 assertions, green on clang 21 and gcc 15** (and ASan).

## How it works

A schema is a **type**. Primitives (`U8`, `U16BE`, `U32LE`, …), `Bytes<N | Ref("f")>`, `Array<T, N | Ref("f"), Cap>`, `Choice<Tag("f"), Case<v, T>…>`, `Optional<T, Pred>` and `Refine<Pred, "name">` compose into a `Struct<Field<"n", T>…>`. `parse<S>(Untrusted)` returns a `Result<Parsed<S>>` — a value, never an exception — and on failure an `Error` carrying the field path (`"frames[3].payload"`), the kind (`Truncated`, `OutOfRange`, `RefinementFailed`, `BadTag`, `TooManyElements`), and the offset.

Three rules make the guarantee hold, each enforced at compile time:

- **One door.** `Untrusted` exposes only its size; the only thing that reads its bytes is the parser's `Cursor`, which checks the remaining length before every read. A snippet that tries `untrusted[0]` does not compile.
- **References point backward.** `Ref("f")` and the predicates of `Optional`/`Refine` may only name an *earlier* field, and a length/count reference must target an unsigned integer. There is no "length from the future".
- **Bounded work.** `Array<T, Ref("count"), Cap>` rejects a count past `Cap` (default 4096) *before* allocating or looping, so a hostile count field cannot exhaust memory or time.

The fixed-offset prefix of a schema is computed at compile time (`fixed_prefix_v<S>`), so the runtime cost scales with the number of *variable-length* fields, not the size of the schema. See [docs/01-design.md](docs/01-design.md) and [docs/architecture.md](docs/architecture.md).

## Quick start

Header-only, zero dependencies. Prerequisites: a C++20 compiler (clang 17+ or gcc 13+), CMake 3.25+, Ninja. On Windows, the project builds with MSYS2 clang64; the fuzzer, the sanitizers and the MTE/QEMU job are Linux.

```bash
cmake --preset clang-release      # or gcc-release; msys2-clang64 on Windows
cmake --build --preset clang-release
ctest --preset clang-release      # unit tests + the "must not compile" tests
```

To use it, put `include/portcullis/` on your include path and `#include <portcullis/portcullis.hpp>`. Define a schema as above and call `parse<S>`.

## Limitations (v1)

Binary, length-prefixed or fixed-layout formats (headers, TLV, chunked containers, arrays with count fields, tagged unions). Not yet: following out-of-line offsets (so a TIFF-style format is parsed as its linear IFD table only), runtime-selected endianness, sentinel-terminated streams as a single schema (a PNG chunk stream is parsed one chunk at a time by the consumer), text formats, a C API, and the write side. The `Parsed` result allocates for arrays. Full list and rationale: [docs/roadmap.md](docs/roadmap.md).

## How it was built

Portcullis was built by Claude agents working in parallel git worktrees — one unit per agent against frozen interfaces ([docs/01-design.md](docs/01-design.md) §5) and a written plan ([docs/superpowers/plans](docs/superpowers/plans/)), with an integrator who re-verified every unit live before merging. The decisions are logged with their reasons ([docs/00-decisions.md](docs/00-decisions.md)); the bugs that only appeared once units were merged and run on a second toolchain are in [docs/reports/](docs/reports/) (`wave1-integration-report.md`, `wave2-integration-report.md`). That process — not only the code — is part of what this repository demonstrates.

## License

MIT. See [LICENSE](LICENSE). Test/tooling components (doctest) are listed in [THIRD_PARTY.md](THIRD_PARTY.md); the library itself has no dependencies.
