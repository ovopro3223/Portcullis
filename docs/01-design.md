# Portcullis — design spec

Date: 2026-10-31 · Status: approved in conversation, written for review · Working name: **Portcullis** (the castle gate that decides what gets in; final name is a later decision, as with CueP3D's D-002).

## 1. Purpose and intent

A header-only C++20 library for parsing **untrusted bytes** through declared contracts, so that a parser written with it cannot read or write outside the bounds the contract establishes. It is zod's idea — composable, declarative schemas with refinements — brought to the one place where a violated length field is a memory-safety event rather than a validation error.

**Who it is for.** Teams writing their own parsers for binary protocols and containers in C++: network stacks, embedded/IoT firmware, game netcode, file-format libraries. Each of them re-writes the same length-prefixed, tag-length-value, chunked structures by hand, and each hand-written parser re-creates the same class of bug.

**What success looks like.**

- The claim is narrow and measurable: *"a parser written through Portcullis cannot read or write out of bounds, and here is the evidence"* — never "prevents buffer overflow" in general. Code that does not go through the library is not protected by it, and the README says so in the first screen.
- The evidence is real: historical parser CVEs replayed against contracts, structure-aware fuzzing under sanitizers, and benchmarks with the cost of the guarantees stated plainly.
- The project also stands as a portfolio piece for the owner: the planning, decisions log, agent briefs and integration reports are part of what it demonstrates, as in CueP3D.

**Honesty rules carried over from CueP3D.** Every number in the README comes from a reproducible script and names the commit and toolchain it was measured on. A goal that is missed is reported as missed, never adjusted to pass. Decisions are recorded when made (`docs/00-decisions.md`), with what they rule out.

## 2. Scope of v1

In:

- Binary formats with fixed or length-prefixed structure: headers, tag-length-value, chunked containers, arrays with count fields, tagged unions.
- The core library (`include/portcullis/`), a structure-aware fuzzer derived from the same schema, an optional ARM MTE-backed arena, the CVE replay suite (6 CVEs: 2 TLS, 2 PNG, 2 TIFF), benchmarks, CI.
- Compilers: clang and gcc on Linux (CI), clang on Windows (the owner's machine). MSVC is best-effort: nothing in the design depends on it, nothing is blocked on it.

Out (recorded in the roadmap, not forgotten):

- A C API. The guarantees live in the C++ type system; a C facade over pre-declared contracts can be added later without changing the core.
- Text formats (JSON/XML/HTTP). The core is bytes-in/types-out and format-agnostic, so they can come later; v1 does not build them.
- Deep container formats (PDF, zip). Too large for a first proof.
- Serialization (the write side), generated documentation, CHERI.

## 3. Constraints

- **Free tooling only.** clang/LLVM, CMake, Ninja, doctest (single header, MIT), libFuzzer and the sanitizers that ship with clang, QEMU for the MTE job, GitHub Actions. Nothing paid, nothing hosted.
- **Zero dependencies for the library.** Test and tooling dependencies are allowed and listed.
- **C++20 required.** String literals as non-type template parameters (`Field<"len", …>`), lambdas in unevaluated contexts, `consteval`/`constexpr` layout analysis, `std::span`.
- Repository text is English; the working conversation with the owner is Levantine Arabic.
- Work proceeds in planned waves with agents owning one unit each in separate worktrees, an integrator merging and verifying by running the real thing — the method that found CueP3D's integration-only bugs.

## 4. Architecture

```
include/portcullis/
  core/    Untrusted, Cursor, Parsed<T>, Error, Result
  schema/  primitives (U8, U16BE/LE, U32BE/LE, U64BE/LE, I8…), Bytes<N | Ref<"f">>,
           Array<T, N | Ref<"f">>, Choice<Tag<"f">, Case<v, T>…>, Optional<T, Pred>,
           Struct<Field<"name", T>…>, Refine<pred, "name">
  visit/   for_each_field<Schema>: the one schema walker; also layout analysis
           (fixed_prefix, fixed_size) built on it
  parse/   parse<Schema>(Untrusted) -> Result<Parsed<Schema>>
  fuzz/    Generator<Schema>, structure-aware mutator, libFuzzer custom-mutator hook
  arena/   TaggedArena: MTE on aarch64 Linux with MTE, plain bump arena elsewhere;
           materialize<T>(Parsed<T>, arena)
tests/     unit (doctest), compile-time (static_assert), must-not-compile
cve/       one directory per replayed CVE (see §8)
bench/     throughput vs handwritten parser, compile-time share, compile cost
docs/      00-decisions.md, 01-design.md (this spec, promoted), 02-plan.md, briefs/, reports/,
           change-requests/, roadmap.md
.github/   ci.yml, fuzz-smoke.yml, mte.yml
```

**Dependency rules.** `schema` is types only; it does nothing. `visit` is the only module that knows how to walk a schema. `parse` and `fuzz` are both consumers of `visit` and know nothing of each other. `arena` depends on `core` only. Nothing outside `core` touches raw memory.

**One walker, two products.** The parser and the fuzzer use the same `for_each_field<Schema>` and the same layout analysis. There is no second description of a format anywhere — if a schema compiles, it parses, it fuzzes, and its fixed layout is known at compile time.

## 5. Public API and semantics

### 5.1 The only door

```cpp
auto in = portcullis::Untrusted(std::span<const std::byte>(buf, n));
auto r  = portcullis::parse<Heartbeat>(in);
if (!r) { /* r.error() */ }
const auto& hb = *r;                   // Parsed<Heartbeat>, read-only
hb.get<"len">();                       // uint16_t — a copied value
hb.get<"payload">();                   // std::span<const std::byte>, length already checked
```

- `Untrusted` wraps a span and exposes **no** element access: no `data()`, no `operator[]`, no iterators. The only consumer is `parse<>`. A test asserts that code which tries to read it does not compile.
- `Cursor` (internal) holds `[pos, end)` and every read checks the remaining length before touching a byte. It is the single read path in the library.
- `Parsed<T>` is constructed only after every field and every refinement has passed. It is immutable. Integer fields are copied out; byte ranges are `std::span<const std::byte>` views into the original buffer, with their length established by the contract. Its lifetime is bound to the input buffer (documented; `materialize()` into an arena copies out).

### 5.2 Schema primitives

| Combinator | Meaning | Checks |
| --- | --- | --- |
| `U8`, `U16BE/LE`, `U32BE/LE`, `U64BE/LE`, `I8`… | fixed-width integers, explicit endianness | remaining ≥ width |
| `Bytes<N>` | exactly N bytes | remaining ≥ N |
| `Bytes<Ref<"f">>` | as many bytes as the earlier field `f` says | `f ≤ remaining`, evaluated before any span exists |
| `Array<T, N>` / `Array<T, Ref<"f">>` | N or `f` elements of T, parsed in order | per element, plus a configurable element cap for `Ref` counts |
| `Choice<Tag<"f">, Case<v1, A>, Case<v2, B>…>` | tagged union on an earlier field | unknown tag → `BadTag` |
| `Optional<T, Pred>` | present iff `Pred(parsed-so-far)` | — |
| `Struct<Field<"n", T>…>` | fields in declaration order, nestable | — |
| `Refine<pred, "name">` | a predicate over the fields parsed so far | failure → `RefinementFailed{"name"}` |

**Reference rule.** `Ref<"f">` and the predicates of `Optional`/`Refine` may only name fields declared *earlier* in the same `Struct` (or an enclosing one), and the target of a `Ref` used as a length, count or tag must be an unsigned integer primitive. Both are enforced at compile time — a forward reference, or a `Ref` to a non-integer field, does not compile. There is no "length from the future".

**Worked example — the Heartbleed record.**

```cpp
using Heartbeat = Struct<
  Field<"type",    U8>,
  Field<"len",     U16BE>,
  Field<"payload", Bytes<Ref<"len">>>,    // len ≤ remaining is checked here, automatically
  Field<"padding", Bytes<16>>
>;
```

`Bytes<Ref<"len">>` alone rejects the CVE-2014-0160 input (declared 65535, one byte present) with `Error{path: "payload", kind: Truncated, at: 3, need: 65535, have: 1}`. `Refine` is for rules the library cannot know: CRCs, cross-field invariants, value ranges.

### 5.3 Compile-time layout analysis

`fixed_prefix<S>` is the number of bytes of `S` whose offsets are known at compile time (everything before the first variable-length field); `fixed_size<S>` is defined when the whole schema is fixed. The parser performs **one** bounds check for the fixed prefix instead of one per field, and a fully fixed schema (a header) costs one check total. `static_assert(fixed_size_v<ElfHeader64> == 64)` documents a layout and stops it drifting. This is the measurable answer to "validation costs latency": the runtime cost scales with the number of *variable* fields, not with the size of the schema.

### 5.4 Errors

`parse` returns `Result<Parsed<S>>`, never throws. An `Error` carries the field path (`"frames[3].payload"`), a kind (`Truncated`, `OutOfRange`, `RefinementFailed{name}`, `BadTag`, `TooManyElements`), the byte offset, and for `Truncated` the needed and available counts. Only the first failure is reported — after it, the remaining bytes have no meaning.

## 6. Structure-aware fuzzing

`portcullis::fuzz` walks the schema with the same `for_each_field` as the parser.

- **Generation.** For each field it picks values biased to boundaries (0, max, `len = remaining ± 1`, every `Choice` case), producing inputs that are structurally valid, plus inputs that violate exactly one contract on purpose.
- **Mutation.** A libFuzzer custom mutator (`LLVMFuzzerCustomMutator`) parses the current input with the schema and mutates at field level: grow a length field, swap a tag, truncate at a field boundary, duplicate an array element. The mutator understands the format, so it reaches deep states faster than byte-level mutation — the libprotobuf-mutator idea, without protobuf.
- **Refinements are not inverted.** Arbitrary predicates cannot be solved; candidates are generated and the contract rejects them, and the rejection rate is recorded.

Two uses, two separate claims:

1. **The library itself**: `parse` of any input never reads out of bounds — fuzz targets run under ASan/UBSan, 60 s each in CI, longer runs by hand.
2. **The CVE replay**: the same generator drives the *original* vulnerable parser (built at its vulnerable version, under ASan) and the contract-based rewrite. Time-to-first-crash versus no crash is the evidence (§8).

## 7. TaggedArena (ARM MTE), optional

`portcullis::arena::TaggedArena` — the last wave, time-boxed, and the one part of the design that extends protection beyond code that goes through the library.

- On aarch64 Linux with MTE (`HWCAP2_MTE`): an `mmap` region with `PROT_MTE`, synchronous tag checking via `prctl`, a random tag per allocation (`<arm_acle.h>`, `-march=armv8.5-a+memtag`). `materialize<T>(parsed, arena)` copies validated output into tagged granules. Any access outside an allocation — including by code elsewhere in the program — faults immediately.
- Elsewhere: the same API over a plain bump arena.
- CI runs the arena tests on QEMU user-mode aarch64 (`-cpu max` emulates MTE): a deliberate overflow must be caught.
- Documented limits, stated up front: 16-byte granule (an overflow that stays inside the last granule is not caught), heap only, detection by fault rather than prevention, Linux/Android only. It is presented as experimental.

## 8. Testing, proof, measurement

### 8.1 Four layers

1. **Unit** (doctest): every combinator, cursor bounds, every error kind, nesting, refinements.
2. **Compile-time**: `static_assert` on `fixed_prefix`/`fixed_size`; **must-not-compile** tests (CMake compiles a snippet and expects failure): a forward `Ref`, any raw access to `Untrusted`, mutation of `Parsed`. These are the most important tests in the repository: they prove the guarantee is in the types, not in a convention.
3. **Fuzz**: libFuzzer targets per schema family under ASan/UBSan (`fuzz-smoke.yml`, 60 s each).
4. **CVE replay** (§8.2).

### 8.2 CVE replay suite

Six CVEs, two per family (TLS, PNG, TIFF). Selection criteria, applied by a short research task in the plan: the bug is a bounds violation driven by a length or count field; a public proof-of-concept input exists (Magma's corpus or the public advisory) and its provenance can be stated; the vulnerable version builds under ASan on Linux. Candidates to verify first: CVE-2014-0160 (OpenSSL heartbeat), plus Magma's libpng, libtiff and OpenSSL bug set.

Each `cve/<id>/` contains:

- `README.md` — the bug, the exact field relationship that was violated, links.
- `contract.hpp` — only the part of the format involved, written with Portcullis.
- `poc/` — the triggering input and where it came from.
- `reject.test.cpp` — `parse<Contract>(poc)` returns an `Error` with the expected path and kind.
- `harness/` — the libFuzzer target against the original parser (vulnerable version, ASan) and against the contract rewrite.

`CVES.md` is generated from the suite: per CVE, rejected-with-path yes/no, time to first crash on the original, crashes on the rewrite (expected: none), and the commit and toolchain measured.

### 8.3 Benchmarks

`bench/` → `BENCHMARKS.md`, with the commit hash and compiler versions:

- Parse throughput (MB/s) for the three formats: a handwritten, bounds-checked C++ parser versus Portcullis — the honest cost of the guarantees.
- Share of checks resolved at compile time per format (`fixed_prefix` versus total fields).
- Compile time per schema — the price of the templates, reported rather than hidden.

### 8.4 CI

- `ci.yml`: Linux clang and gcc with `-Wall -Wextra -Werror`, tests, must-not-compile tests, ASan/UBSan builds; Windows clang (LLVM) tests.
- `fuzz-smoke.yml`: 60 s per fuzz target under ASan.
- `mte.yml`: cross-compile for aarch64, run the arena tests on QEMU.
- A dependency gate: the library has none; test/tooling dependencies are listed with licenses.

## 9. Definition of done (v1)

- Compiles warning-free on clang and gcc; the must-not-compile tests pass.
- 6/6 CVE inputs rejected with the correct field path; the rewrites survive their fuzz runs with zero crashes; the originals' time-to-crash recorded.
- `fuzz-smoke.yml` green; `BENCHMARKS.md` and `CVES.md` generated from scripts.
- The MTE arena test passes on QEMU; its limits are documented.
- README states the narrow claim, the numbers, and what the library does not protect.
- Everything free; decisions and reports in `docs/`.

## 10. Decisions to record at plan time

- Final name (working name Portcullis).
- Test framework: doctest (single header, MIT). CMake minimum version and Ninja as the generator.
- The fixed-string NTTP type used for field names, and the minimum clang/gcc versions that support the NTTP-lambda pattern the design relies on.
- The exact six CVEs after the research task.
- Element cap for `Array<T, Ref<…>>` (a configurable maximum, with a default) so a count field cannot make the parser do unbounded work.

## 11. Risks and mitigations

- **Template compile times and error messages.** Measured and reported (§8.3); schemas are kept flat where possible; `static_assert` messages name the field.
- **Compiler support for NTTP strings/lambdas.** Pinned minimum versions found in Wave 0 and checked in CI on both compilers.
- **Vulnerable-version builds for the CVE harnesses.** Use Magma's pinned versions and build scripts where possible; keep each harness self-contained with a pinned source tarball and checksum.
- **PoC redistribution.** Only inputs from public advisories or Magma's corpus, with provenance in each `README.md`; where redistribution is unclear, generate the input from a documented script instead.
- **MTE under QEMU.** If QEMU's emulation proves unreliable in CI, the arena stays in the tree with its tests runnable locally and the CI job marked experimental — the core does not depend on it.
- **Windows + libFuzzer.** Fuzz targets are built and run in the Linux jobs; Windows runs unit and compile-time tests.

## 12. Roadmap (after v1)

C API facade over pre-declared contracts; text formats on the same core; generated format documentation from the schema; MSVC support; the write side (serialization from `Parsed<T>`); more CVE families; CHERI as a second hardware backend.

---

## Appendix A — Frozen interfaces (after Wave 0)

These signatures are frozen: P1–P8 are written against them, and changes go through `docs/change-requests/`. Full bodies live in the headers; this is the contract between units.

### core (Wave 0, implemented)

```cpp
template <std::size_t N> struct fixed_string {           // core/fixed_string.hpp
  char value[N]{};
  constexpr fixed_string(const char (&s)[N]);
  constexpr std::string_view sv() const noexcept;        // drops the terminator
  constexpr bool operator==(const fixed_string&) const = default;
};
enum class Endian { Big, Little };                        // core/endian.hpp
class Untrusted {                                         // core/untrusted.hpp
  explicit Untrusted(std::span<const std::byte>) noexcept;
  std::size_t size() const noexcept;                      // the only accessor
};
class Cursor {                                            // core/cursor.hpp — the one read path
  explicit Cursor(const Untrusted&) noexcept;
  std::size_t remaining() const noexcept;
  std::size_t offset() const noexcept;
  [[nodiscard]] bool take(std::size_t n, std::span<const std::byte>& out) noexcept;
  template <std::unsigned_integral U> [[nodiscard]] bool read(Endian, U&) noexcept;
  template <std::signed_integral I>   [[nodiscard]] bool read(Endian, I&) noexcept;
};
enum class ErrorKind { Truncated, OutOfRange, RefinementFailed, BadTag, TooManyElements };
struct Error { ErrorKind kind; std::string path; std::size_t offset; std::size_t need=0, have=0;
               std::string_view name={}; std::uint64_t tag=0; };
template <class T> class Result {                         // core/result.hpp
  static Result ok(T); static Result fail(Error);
  bool has_value() const noexcept; explicit operator bool() const noexcept;
  const T& operator*() const&; const T* operator->() const; const Error& error() const;
};
```

### schema / visit / parse (Wave 1, P1)

```cpp
// schema: primitives U8..U64BE/LE, I8..I64BE/LE (value_type, width, endian);
//   Field<"n", T>, Struct<Items...>, Bytes<auto>, Array<T, auto, Cap=4096>,
//   Choice<Tag("f"), Case<v,T>...>, Optional<T, Pred>, Refine<Pred, "name">,
//   Ref("f"), Tag("f").
template <class S, class F> constexpr void for_each_field(F&&);          // visit
template <class S> inline constexpr std::size_t field_count_v;
template <class S, fixed_string Name> inline constexpr std::size_t field_index_v;
template <class S, fixed_string Name> using field_type_t = /* ... */;
template <class S> inline constexpr std::size_t fixed_prefix_v;          // layout
template <class S> inline constexpr bool is_fixed_v;
template <class S> inline constexpr std::size_t fixed_size_v;
template <class T> using value_of_t = /* ... */;                         // parse
template <class S> class Parsed { template <fixed_string N> const auto& get() const noexcept;
                                  std::span<const std::byte> bytes() const noexcept; };
template <class S> class Partial { template <fixed_string N> const auto& get() const noexcept;
                                   std::size_t remaining() const noexcept; };
template <fixed_string N, class V> decltype(auto) field(const V&) noexcept;
template <class S> Result<Parsed<S>> parse(const Untrusted&);
```

### fuzz / arena (Wave 2)

```cpp
template <class S> class Generator { explicit Generator(std::uint64_t seed) noexcept;
  std::vector<std::byte> valid(); std::vector<std::byte> violating(); };
template <class S> std::size_t mutate(std::span<std::byte> buf, std::size_t size,
                                      std::size_t max_size, std::uint32_t seed) noexcept;
#define PORTCULLIS_FUZZ_TARGET(S) /* LLVMFuzzerTestOneInput + LLVMFuzzerCustomMutator */
class TaggedArena { explicit TaggedArena(std::size_t capacity);
  void* allocate(std::size_t bytes, std::size_t align) noexcept;        // nullptr on exhaustion
  static bool mte_available() noexcept; bool tagging_active() const noexcept; };
template <class S> Parsed<S>* materialize(const Parsed<S>&, TaggedArena&) noexcept;
```
