# Portcullis v1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A header-only C++20 library through which untrusted bytes can only be read via declared contracts, proven on six replayed parser CVEs, structure-aware fuzzing and honest benchmarks.

**Architecture:** Schemas are types; one compile-time walker (`for_each_field`) feeds both the parser and the fuzzer; `Untrusted` is the only door to raw bytes and `Cursor` the only read path; `Parsed<S>` exists only after every field and refinement passed. An optional MTE-backed arena extends detection beyond code that goes through the library.

**Tech Stack:** C++20 (clang, gcc; MSYS2 clang64 on the owner's Windows machine), CMake ≥ 3.25 + Ninja, doctest (vendored single header, MIT), libFuzzer + ASan/UBSan (Linux CI), QEMU user-mode aarch64 (MTE job), Python 3 stdlib for report generators, GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-10-31-portcullis-design.md` — every task below argues from it; executors read both.

## Global Constraints

- Free tooling only: clang/LLVM, gcc, CMake, Ninja, doctest, libFuzzer/sanitizers, QEMU, GitHub Actions. Nothing paid, nothing hosted.
- Zero dependencies for the library (`include/portcullis/`). Test/tooling dependencies are vendored or fetched by pinned version and listed with licenses in `THIRD_PARTY.md`.
- C++20 required; `-std=c++20 -Wall -Wextra -Werror` on clang and gcc. MSVC is best-effort: never blocked on, never worked around in the design.
- One read path: every byte read goes through `Cursor::take`/`read_*`, which check `remaining()` first. No unchecked read path exists anywhere in the library (decision D-010; the fixed-prefix early-out is an extra check, not a bypass).
- `Untrusted` exposes `size()` only. `Parsed<S>` is immutable. `Ref("f")`, `Tag("f")` and predicates may only name fields declared earlier; a `Ref`/`Tag` target must be an unsigned integer primitive — both enforced at compile time.
- Errors are values (`Result<T>`), first failure only, with field path, kind, offset, and need/have for `Truncated`.
- `Array<T, Ref("count")>` has an element cap, default **4096** (D-008), exceeded → `TooManyElements` before any element is parsed.
- Repository text is English. Every published number comes from a script and names the commit and toolchain. A missed goal is reported as missed.
- Work runs in waves: one agent per unit in its own git worktree (`git worktree add ../Portcullis-worktrees/<branch> -b <branch> main`), driven by `docs/briefs/<P>.md`; the integrator merges and verifies by running the real thing.

## Review Focus

Inputs the spec implies but no task's tests exercised until these lines were added; each is pinned to its owning task.

1. **Huge counts.** `Array<T, Ref("count")>` with `count = 0xFFFFFFFF` must fail fast with `TooManyElements` and allocate nothing — Task 1.6, `cap_rejects_before_allocating`.
2. **Empty input.** `parse<S>` of a zero-length `Untrusted` where `fixed_prefix_v<S> > 0` → `Truncated` at offset 0 with `need = fixed_prefix_v<S>`, `have = 0` — Task 1.4, `empty_input_is_truncated_at_offset_zero`.
3. **Zero-length references.** `Bytes<Ref("len")>` with `len = 0` yields an empty span and succeeds; it must not be mistaken for truncation — Task 1.5, `zero_length_ref_is_valid`.
4. **Error paths inside containers.** A failure in the fourth element of a nested array reports `"frames[3].payload"`, not `"payload"` — Task 1.10, `nested_error_path_names_array_index`.
5. **Mutator size discipline.** `mutate()` must never return a size above `max_size` and must never read past `size` — Task 6.3, `mutate_respects_max_size` under ASan.

---

## File structure

```
CMakeLists.txt                      INTERFACE target portcullis::portcullis; subdirs tests/, bench/, cve/
CMakePresets.json                   clang-debug, clang-asan, clang-release, gcc-release, msys2-clang64
THIRD_PARTY.md                      doctest (MIT), pinned sources used by cve/vendor
include/portcullis/
  portcullis.hpp                    umbrella include
  core/fixed_string.hpp             fixed_string<N> (NTTP names)
  core/endian.hpp                   enum class Endian { Big, Little }
  core/untrusted.hpp                Untrusted
  core/cursor.hpp                   Cursor (the read path)
  core/error.hpp                    ErrorKind, Error
  core/result.hpp                   Result<T>
  schema/primitives.hpp             U8…U64BE/LE, I8…I64BE/LE
  schema/ref.hpp                    Ref, Tag (structural NTTP values), is_ref_v
  schema/combinators.hpp            Field, Struct, Bytes, Array, Choice, Case, Optional, Refine
  schema/partial.hpp                Partial<S> (fields-so-far view for predicates), field<"n">(v)
  visit/for_each_field.hpp          FieldInfo, for_each_field<S>
  visit/layout.hpp                  fixed_prefix_v, is_fixed_v, fixed_size_v
  parse/value_of.hpp                value_of_t<T> mapping
  parse/parsed.hpp                  Parsed<S>
  parse/path.hpp                    PathBuilder
  parse/parse.hpp                   parse<S>(const Untrusted&)
  fuzz/generator.hpp                Generator<S>
  fuzz/mutator.hpp                  mutate<S>()
  fuzz/libfuzzer.hpp                PORTCULLIS_FUZZ_TARGET(S)
  arena/tagged_arena.hpp            TaggedArena, materialize<S>()
tests/third_party/doctest.h         vendored, version pinned
tests/*.test.cpp                    one file per unit
tests/compile_fail/                 snippets that must NOT compile + CMake WILL_FAIL tests
tests/schemas/                      shared example schemas (heartbeat.hpp, png_chunk.hpp, tiff_ifd.hpp)
fuzz/targets/*.cpp                  libFuzzer targets per example schema
cve/SELECTION.md                    the six CVEs, criteria evidence, PoC provenance
cve/vendor/fetch.sh                 pinned tarballs + sha256, ASan builds of the vulnerable versions
cve/<id>/{README.md,contract.hpp,poc/,reject.test.cpp,harness/}
scripts/gen-cves.py                 → CVES.md
bench/handwritten/{tls,png,tiff}.hpp  naive bounds-checked parsers
bench/bench.cpp, scripts/gen-benchmarks.py → BENCHMARKS.md
docs/00-decisions.md, 01-design.md (spec promoted + §5 frozen interfaces), 02-plan.md, briefs/, reports/, roadmap.md
.github/workflows/{ci,fuzz-smoke,mte}.yml
```

## Waves and ownership

| Wave | Unit | Owner | Depends on |
| --- | --- | --- | --- |
| 0 | Toolchain, scaffold, core, decisions, briefs, CI skeleton | integrator | — |
| 1 | **P1** schema + visit + layout + parse (Tasks 1.1–1.12) | strongest agent | Wave 0 |
| 1 | **P4** CVE research, vulnerable builds, PoCs (Tasks 4.1–4.3) | agent | Wave 0 (no library code needed) |
| 1 | **P5** handwritten bench parsers + bench runner + CI matrix (Tasks 5.1–5.3) | agent | Wave 0 |
| 2 | **P6** fuzz generator, mutator, libFuzzer hook (Tasks 6.1–6.4) | agent | P1 |
| 2 | **P7** six CVE contracts + reject tests (Task 7.1 ×6, one agent per family) | 3 agents | P1, P4 |
| 2 | **P8** TaggedArena + MTE + QEMU job (Tasks 8.1–8.2) | agent | P1 |
| 3 | Integration: run everything live, wire harnesses, generate CVES.md/BENCHMARKS.md, README, reports (Tasks 9.1–9.3) | integrator | all |

Frozen after Wave 0 (changes go through `docs/change-requests/`): the interfaces in the **Interfaces** blocks of Tasks 0.2 and 1.1–1.3 — they are what P4–P8 are written against before P1 lands.

---

## Wave 0 — integrator

### Task 0.1: Toolchain and repository scaffold

**Files:**
- Create: `CMakeLists.txt`, `CMakePresets.json`, `.gitattributes` (`* text=auto eol=lf`), `.gitignore`, `LICENSE` (MIT, owner's name), `THIRD_PARTY.md`, `README.md` (stub: the narrow claim, status "in development")
- Create: `tests/CMakeLists.txt`, `tests/third_party/doctest.h` (v2.4.11, MIT, sha256 recorded in `THIRD_PARTY.md`), `tests/main.cpp` (`DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN`), `tests/smoke.test.cpp`
- Create: `docs/00-decisions.md` (D-001…D-010 below), `docs/01-design.md` (the spec, with a §5 "Frozen interfaces" section copied from Tasks 0.2/1.1–1.3), `docs/02-plan.md` (the wave table above + agent protocol from CueP3D), `docs/roadmap.md` (spec §12), `docs/briefs/P1.md … P8.md`

**Interfaces:**
- Produces: CMake target `portcullis::portcullis` (INTERFACE, `cxx_std_20`, include dir `include/`); presets `clang-debug`, `clang-asan` (`-fsanitize=address,undefined`), `clang-release`, `gcc-release`, `msys2-clang64`; `ctest` runs every `tests/*.test.cpp` through one `portcullis_tests` executable.

Decisions to write into `docs/00-decisions.md`: D-001 purpose (spec §1); D-002 working name Portcullis; D-003 v1 scope (spec §2); D-004 free tooling, zero library deps; D-005 C++20 header-only, guarantees live in the type system; D-006 compilers (clang + gcc on Linux CI, clang on Windows via MSYS2 clang64, MSVC best-effort); D-007 NTTP syntax — names as string literals (`Field<"len", U16BE>`), references as structural values (`Bytes<Ref("len")>`, `Choice<Tag("type"), …>`), because a class-type NTTP is the one C++20 form that gives both `Bytes<16>` and `Bytes<Ref("len")>` from a single `template<auto>`; D-008 `Array` element cap default 4096; D-009 Windows toolchain is MSYS2 clang64 (`winget install MSYS2.MSYS2`, then `pacman -S mingw-w64-clang-x86_64-toolchain mingw-w64-clang-x86_64-cmake mingw-w64-clang-x86_64-ninja`) — no Visual Studio dependency; D-010 no unchecked read path (Global Constraints).

- [ ] **Step 1: Install the toolchain** — `winget install MSYS2.MSYS2`, then in the clang64 shell the pacman line from D-009. Verify: `clang++ --version` prints a clang ≥ 17; `cmake --version` ≥ 3.25; `ninja --version` prints.
- [ ] **Step 2: Write `tests/smoke.test.cpp`** — `TEST_CASE("portcullis umbrella header compiles") { CHECK(true); }` including `<portcullis/portcullis.hpp>` (umbrella header initially includes nothing).
- [ ] **Step 3: Write `CMakeLists.txt`, presets, `tests/CMakeLists.txt`** — project `portcullis` VERSION 0.1.0, `add_library(portcullis INTERFACE)`, `target_include_directories`, `target_compile_features(cxx_std_20)`, warnings as errors on the test target, `enable_testing()`, `add_subdirectory(tests)`; tests use `file(GLOB ... *.test.cpp)`.
- [ ] **Step 4: Run** `cmake --preset msys2-clang64 && cmake --build --preset msys2-clang64 && ctest --preset msys2-clang64 --output-on-failure` — Expected: 1 test, PASS.
- [ ] **Step 5: Write the docs listed above and the eight briefs** — each brief: goal, the Interfaces blocks it consumes/produces (copied from this plan), its tasks' test names, "done" criteria, the worktree it works in, the rule that it touches only its own files, and the reporting format (`docs/reports/<P>-report.md`: what was built, every test that runs, measurements, open questions).
- [ ] **Step 6: Commit** — `git add -A && git commit -m "Wave 0: toolchain, scaffold, decisions D-001..D-010, briefs"`.

### Task 0.2: Core — `fixed_string`, `Untrusted`, `Cursor`, `Error`, `Result`

**Files:**
- Create: `include/portcullis/core/{fixed_string,endian,untrusted,cursor,error,result}.hpp`, update `include/portcullis/portcullis.hpp`
- Test: `tests/core.test.cpp`, `tests/compile_fail/CMakeLists.txt`, `tests/compile_fail/control.cpp`, `tests/compile_fail/untrusted_no_raw_access.cpp`

**Interfaces (frozen):**
```cpp
namespace portcullis {
template <std::size_t N> struct fixed_string {
  char value[N]{};
  constexpr fixed_string(const char (&s)[N]);
  constexpr std::string_view sv() const noexcept;                 // without the terminator
  constexpr bool operator==(const fixed_string&) const = default;
};
enum class Endian { Big, Little };
class Untrusted {
 public:
  explicit Untrusted(std::span<const std::byte> bytes) noexcept;
  std::size_t size() const noexcept;                              // the only accessor
 private:
  std::span<const std::byte> bytes_;
  friend class Cursor;
};
class Cursor {
 public:
  explicit Cursor(const Untrusted& in) noexcept;
  std::size_t remaining() const noexcept;
  std::size_t offset() const noexcept;
  [[nodiscard]] bool take(std::size_t n, std::span<const std::byte>& out) noexcept;   // false, untouched, if n > remaining()
  template <std::unsigned_integral U> [[nodiscard]] bool read(Endian e, U& out) noexcept; // width = sizeof(U)
  template <std::signed_integral I>   [[nodiscard]] bool read(Endian e, I& out) noexcept; // two's complement of the unsigned read
};
enum class ErrorKind { Truncated, OutOfRange, RefinementFailed, BadTag, TooManyElements };
struct Error {
  ErrorKind kind; std::string path; std::size_t offset;
  std::size_t need = 0, have = 0;      // Truncated
  std::string_view name = {};          // RefinementFailed
  std::uint64_t tag = 0;               // BadTag
};
template <class T> class Result {
 public:
  static Result ok(T value); static Result fail(Error e);
  bool has_value() const noexcept; explicit operator bool() const noexcept;
  const T& operator*() const&; const T* operator->() const;
  const Error& error() const;          // precondition: !has_value()
};
}
```

- [ ] **Step 1: Write failing tests in `tests/core.test.cpp`**:
  - `fixed_string_sv_drops_terminator`: `fixed_string("len").sv() == "len"` and `== fixed_string("len")`.
  - `untrusted_reports_size_only`: `Untrusted(span of 5).size() == 5`.
  - `cursor_take_refuses_past_end`: cursor over 3 bytes; `take(4, out)` is `false` and `offset()` stays 0; `take(3, out)` is `true`, `out.size() == 3`, `remaining() == 0`.
  - `cursor_read_big_and_little`: bytes `{0x12,0x34}` → `read(Big, u16) == 0x1234`, offset 2; fresh cursor `read(Little, u16) == 0x3412`.
  - `cursor_read_truncated_leaves_offset`: 1 byte, `read(Big, u16)` is `false`, `offset() == 0`.
  - `result_ok_and_fail`: `Result<int>::ok(7)` dereferences to 7; `Result<int>::fail(Error{ErrorKind::Truncated, "x", 3, 4, 1})` is falsy and `error().need == 4`.
- [ ] **Step 2: Run** `ctest --preset msys2-clang64 -R core` — Expected: build failure (headers missing).
- [ ] **Step 3: Implement the six headers** to the interfaces above. `Cursor::read` composes bytes from `take(sizeof(U))`; no memcpy of untrusted length.
- [ ] **Step 4: Run** the core tests — Expected: all PASS.
- [ ] **Step 5: Compile-fail infrastructure.** `tests/compile_fail/CMakeLists.txt` defines `add_compile_fail_test(name)` → `add_test(NAME cf.<name> COMMAND ${CMAKE_CXX_COMPILER} -std=c++20 -fsyntax-only -I${PROJECT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/<name>.cpp)` with `WILL_FAIL TRUE`; plus `cf.control` (`control.cpp` includes the umbrella header and must compile, `WILL_FAIL FALSE`) so a broken include path cannot pass every negative test. `untrusted_no_raw_access.cpp`: `Untrusted u(s); auto b = u[0];` and `u.data();` — must not compile.
- [ ] **Step 6: Run** `ctest --preset msys2-clang64 -R cf` — Expected: `cf.control` PASS, `cf.untrusted_no_raw_access` PASS (because compilation failed).
- [ ] **Step 7: Commit** — `git commit -m "core: fixed_string, Untrusted, Cursor, Error, Result; compile-fail harness"`.

### Task 0.3: CI skeleton

**Files:** Create `.github/workflows/ci.yml`.

- [ ] **Step 1: Write `ci.yml`** — matrix: `ubuntu-latest` × {clang, gcc} with `cmake --preset clang-release|gcc-release`, then `ctest --output-on-failure`; an ASan job (`clang-asan` preset); `windows-latest` with `msys2/setup-msys2@v2` (`msystem: CLANG64`, install the D-009 packages) running `msys2-clang64`. Triggers: push to `main`, pull_request.
- [ ] **Step 2: Verify locally what can be verified** — the msys2 path is the one the owner runs; Linux jobs run on first push (the repo is not pushed yet by design; the integrator reads the first run with `gh run view --log-failed`).
- [ ] **Step 3: Commit** — `git commit -m "CI: clang/gcc Linux, ASan, MSYS2 clang64 Windows"`.

---

## Wave 1

### P1 — schema, visit, layout, parse (one agent, worktree `p1-core`)

#### Task 1.1: Primitives, `Ref`/`Tag`, `Field`, `Struct`

**Files:** Create `include/portcullis/schema/{primitives,ref,combinators}.hpp`; Test: `tests/schema.test.cpp`

**Interfaces (frozen):**
```cpp
namespace portcullis {
struct U8  { using value_type = std::uint8_t;  static constexpr std::size_t width = 1; static constexpr Endian endian = Endian::Big; };
// U16BE U16LE U32BE U32LE U64BE U64LE I8 I16BE I16LE I32BE I32LE I64BE I64LE — same shape
template <class T> concept Primitive = requires { typename T::value_type; T::width; T::endian; };
template <std::size_t N> struct Ref { fixed_string<N> name; constexpr Ref(const char (&s)[N]); };   // Ref("len")
template <std::size_t N> struct Tag { fixed_string<N> name; constexpr Tag(const char (&s)[N]); };   // Tag("type")
template <class T> inline constexpr bool is_ref_v = /* true for Ref<N> */;
template <fixed_string Name, class T> struct Field { static constexpr auto name = Name; using type = T; };
template <class... Items> struct Struct {};                     // Items: Field<…> or Refine<…>
template <auto Len> struct Bytes {};                             // Bytes<16> or Bytes<Ref("len")>
template <class T, auto Count, std::size_t Cap = 4096> struct Array {};
template <auto V, class T> struct Case {};
template <auto TagRef, class... Cases> struct Choice {};         // Choice<Tag("type"), Case<1, A>, Case<2, B>>
template <class T, auto Pred> struct Optional {};
template <auto Pred, fixed_string Name> struct Refine {};
}
```
- [ ] **Step 1: Write failing tests** — `primitives_declare_width_and_endian` (`U16BE::width == 2`, `U32LE::endian == Endian::Little`, `std::same_as<U8::value_type, std::uint8_t>`), `ref_and_tag_carry_names` (`Ref("len").name.sv() == "len"`), `field_exposes_name_and_type` (`Field<"len", U16BE>::name == fixed_string("len")`), `bytes_accepts_int_and_ref` (`Bytes<16>` and `Bytes<Ref("len")>` are distinct complete types).
- [ ] **Step 2: Run** — Expected: build failure. **Step 3: Implement.** **Step 4: Run** — PASS. **Step 5: Commit** `schema: primitives, Ref/Tag, Field, Struct and combinator type shells`.

#### Task 1.2: `for_each_field` and `FieldInfo`

**Files:** Create `include/portcullis/visit/for_each_field.hpp`; Test: `tests/visit.test.cpp`

**Interfaces (frozen):**
```cpp
template <std::size_t Index, fixed_string Name, class T>
struct FieldInfo { static constexpr std::size_t index = Index; static constexpr auto name = Name; using type = T; };
template <class S, class F> constexpr void for_each_field(F&& f);     // S = Struct<…>; calls f(FieldInfo<…>{}) per Field, in order; Refine items are skipped
template <class S> inline constexpr std::size_t field_count_v;
template <class S, fixed_string Name> inline constexpr std::size_t field_index_v;   // compile error if absent
template <class S, fixed_string Name> using field_type_t = …;
```
- [ ] **Step 1: Tests** — `visits_fields_in_declaration_order` (collect names of `Struct<Field<"a",U8>, Refine<…,"r">, Field<"b",U16BE>>` → `{"a","b"}`), `field_index_and_type` (`field_index_v<S,"b"> == 1`, `field_type_t<S,"b">` is `U16BE`), `constexpr_usable` (`static_assert(field_count_v<S> == 2)`).
- [ ] **Step 2–5:** fail → implement (index_sequence over the Items pack) → pass → commit `visit: for_each_field, FieldInfo, field_index_v`.

#### Task 1.3: Layout analysis

**Files:** Create `include/portcullis/visit/layout.hpp`; Test: `tests/layout.test.cpp`

**Interfaces (frozen):**
```cpp
template <class S> inline constexpr std::size_t fixed_prefix_v;   // bytes before the first variable-length field (whole schema if none)
template <class S> inline constexpr bool is_fixed_v;              // no Bytes<Ref>, Array<…,Ref>, Choice, Optional anywhere
template <class S> inline constexpr std::size_t fixed_size_v;     // defined only if is_fixed_v<S>
```
Primitives and `Bytes<N>` are fixed; nested fixed `Struct`s add up; `Array<T, N>` with fixed `T` is fixed; anything with a `Ref`, `Choice` or `Optional` ends the prefix.
- [ ] **Step 1: Tests** — `all_fixed_header` (`Struct<Field<"a",U8>,Field<"b",U32BE>,Field<"c",Bytes<3>>>`: `is_fixed_v`, `fixed_size_v == 8`, `fixed_prefix_v == 8`), `prefix_stops_at_first_ref` (heartbeat: `fixed_prefix_v == 3`, `!is_fixed_v`), `nested_fixed_struct_counts` (inner fixed struct of 6 inside outer → prefix includes it), `fixed_array` (`Array<U16BE, 4>` → 8).
- [ ] **Step 2–5:** fail → implement → pass → commit `visit: fixed_prefix_v, is_fixed_v, fixed_size_v`.

#### Task 1.4: `parse` for primitives, `Bytes<N>`, flat `Struct`; `Parsed`; `value_of_t`

**Files:** Create `include/portcullis/parse/{value_of,parsed,path,parse}.hpp`; Test: `tests/parse_basic.test.cpp`

**Interfaces (frozen):**
```cpp
template <class T> using value_of_t = …;   // Primitive → value_type; Bytes → std::span<const std::byte>; Struct → Parsed<Struct>;
                                           // Array<T,…> → std::vector<value_of_t<T>>; Choice → std::variant<value_of_t<CaseTypes>...>; Optional<T> → std::optional<value_of_t<T>>
template <class S> class Parsed {          // S = Struct<…>
 public:
  template <fixed_string Name> const auto& get() const noexcept;   // value_of_t<field_type_t<S,Name>>
  std::span<const std::byte> bytes() const noexcept;               // the validated region
};
template <class S> Result<Parsed<S>> parse(const Untrusted& in);
```
- [ ] **Step 1: Tests** — `parses_fixed_header` (bytes `01 00 02 03 04 05 06 07` with the all-fixed schema from 1.3 → `get<"a">()==1`, `get<"b">()==0x02030405`, `get<"c">().size()==3`, `bytes().size()==8`), `empty_input_is_truncated_at_offset_zero` (Review Focus 2: `error().kind == Truncated`, `offset == 0`, `need == 8`, `have == 0`, `path == "a"`), `trailing_bytes_are_allowed` (9 bytes → ok, `bytes().size()==8`), `signed_primitive_round_trips` (`I16BE` of `FF FE` → -2).
- [ ] **Step 2–5:** fail → implement (per-schema `Parser<T>` specializations; `Struct` entry does the `fixed_prefix_v` early-out producing `Truncated` with the first field's path) → pass → commit `parse: primitives, Bytes<N>, flat Struct, Parsed, value_of_t`.

#### Task 1.5: `Ref` lengths — `Bytes<Ref("f")>` and the forward-reference rule

**Files:** Modify `parse/parse.hpp`, Create `include/portcullis/schema/partial.hpp`; Test: `tests/parse_ref.test.cpp`, `tests/compile_fail/{forward_ref,ref_to_bytes}.cpp`, `tests/schemas/heartbeat.hpp`

**Interfaces (frozen):**
```cpp
template <class S> class Partial {      // fields parsed so far; what predicates receive
 public:
  template <fixed_string Name> const auto& get() const noexcept;   // compile error if Name is not yet parsed at this point
  std::size_t remaining() const noexcept;
};
template <fixed_string Name, class V> decltype(auto) field(const V& v) noexcept;   // v.template get<Name>() without the keyword
```
`tests/schemas/heartbeat.hpp` is the spec's worked example (`type` U8, `len` U16BE, `payload` Bytes<Ref("len")>, `padding` Bytes<16>).
- [ ] **Step 1: Tests** — `heartbleed_input_is_truncated_at_payload` (bytes `01 FF FF 41` → `Truncated`, `path == "payload"`, `offset == 3`, `need == 65535`, `have == 1`), `honest_heartbeat_parses` (len 3, 3 bytes, 16 padding → `get<"payload">().size()==3`), `zero_length_ref_is_valid` (Review Focus 3: len 0 → ok, empty span), compile-fail `forward_ref.cpp` (`Bytes<Ref("len")>` declared before `Field<"len",…>`) and `ref_to_bytes.cpp` (`Ref` targeting a `Bytes<4>` field).
- [ ] **Step 2–5:** fail → implement (resolve `Ref` through `field_index_v` + `static_assert(index < current)` and `static_assert(Primitive<target> && unsigned)`) → pass, including `ctest -R cf` → commit `parse: Ref lengths, Partial, forward-reference and integer-target rules`.

#### Task 1.6: `Array`

**Files:** Modify `parse/parse.hpp`; Test: `tests/parse_array.test.cpp`
- [ ] **Step 1: Tests** — `fixed_count_array` (`Array<U16BE,3>` → vector of 3), `ref_count_array` (count field 2 → 2 structs), `cap_rejects_before_allocating` (Review Focus 1: `Array<U8, Ref("n"), 16>` with `n = 0xFF` → `TooManyElements`, `path == "items"`, and a counting allocator or `vector::capacity()==0` check that nothing was reserved), `element_error_path_has_index` (third element truncated → `path == "items[2]"`).
- [ ] **Step 2–5:** fail → implement → pass → commit `parse: Array with Ref counts and element cap`.

#### Task 1.7: `Choice`/`Case`

**Files:** Modify `parse/parse.hpp`; Test: `tests/parse_choice.test.cpp`
- [ ] **Step 1: Tests** — `selects_case_by_tag` (tag 2 → `std::holds_alternative<Parsed<B>>`), `unknown_tag_is_bad_tag` (`BadTag`, `tag == 9`, `path == "body"`), `tag_must_be_earlier_field` (compile-fail `tests/compile_fail/choice_forward_tag.cpp`).
- [ ] **Step 2–5:** fail → implement → pass → commit `parse: Choice/Case`.

#### Task 1.8: `Optional`

**Files:** Modify `parse/parse.hpp`; Test: `tests/parse_optional.test.cpp`
- [ ] **Step 1: Tests** — `present_when_predicate_true` (`Optional<U16BE, [](auto& v){ return field<"flags">(v) & 1; }>`), `absent_consumes_nothing` (flags 0 → `std::nullopt`, offset unchanged).
- [ ] **Step 2–5:** fail → implement → pass → commit `parse: Optional`.

#### Task 1.9: `Refine`

**Files:** Modify `parse/parse.hpp`; Test: `tests/parse_refine.test.cpp`
- [ ] **Step 1: Tests** — `refinement_failure_names_itself` (`Refine<[](auto& v){ return field<"len">(v) <= 64; }, "len_le_64">` with len 65 → `RefinementFailed`, `name == "len_le_64"`, `path == "len_le_64"`, `offset` = offset after the last parsed field), `refinement_sees_only_earlier_fields` (compile-fail `tests/compile_fail/refine_forward_field.cpp`), `refinement_runs_at_its_position` (a refine between fields a and b fails before b is read: b truncated input still reports the refinement).
- [ ] **Step 2–5:** fail → implement → pass → commit `parse: Refine`.

#### Task 1.10: Nested structs and error paths

**Files:** Modify `parse/path.hpp`, `parse/parse.hpp`; Test: `tests/parse_nested.test.cpp`
- [ ] **Step 1: Tests** — `nested_values_are_parsed_structs` (`get<"hdr">().get<"ver">()`), `nested_error_path_names_array_index` (Review Focus 4: `"frames[3].payload"`), `choice_path_uses_field_name` (`"body.len"`).
- [ ] **Step 2–5:** fail → implement (`PathBuilder` with push/pop of names and indices; no allocation until an error is produced) → pass → commit `parse: nested paths`.

#### Task 1.11: `Parsed` immutability and the public surface

**Files:** Test: `tests/compile_fail/parsed_is_immutable.cpp`, `tests/parse_surface.test.cpp`; update `portcullis.hpp` umbrella
- [ ] **Step 1: Tests** — compile-fail: `const_cast`-free attempts to assign through `get<>()` or to default-construct `Parsed<S>`; `umbrella_exposes_parse_and_schema` (one test using only `<portcullis/portcullis.hpp>`).
- [ ] **Step 2–5:** fail → implement → pass → commit `parse: immutability, umbrella header`.

#### Task 1.12: P1 report

- [ ] Write `docs/reports/P1-report.md` (every test name, compile-fail list, open questions: anything the frozen interfaces forced that reads wrong), commit.

### P4 — CVE research, vulnerable builds, PoCs (worktree `p4-cve-infra`)

#### Task 4.1: Select the six CVEs

**Files:** Create `cve/SELECTION.md`
Criteria (spec §8.2): bounds violation driven by a length or count field; public PoC with stated provenance; vulnerable version builds under ASan on Linux. Candidates to verify, two per family — TLS: CVE-2014-0160 (OpenSSL 1.0.1f heartbeat, declared length vs record), CVE-2014-0195 (DTLS fragment length vs message length); PNG: CVE-2015-8126 (palette entry count vs bit depth), CVE-2015-8540 (keyword length underflow); TIFF: CVE-2017-9147 (directory entry count/offset reads), CVE-2016-9535 (predictor row size). Replace any candidate that fails a criterion and say why.
- [ ] **Step 1:** For each candidate, record: advisory link, the exact field relationship violated, PoC source and license/redistribution status, the vulnerable version and fix commit.
- [ ] **Step 2:** Commit `cve: selection of six CVEs with criteria evidence`.

#### Task 4.2: Pinned vulnerable builds under ASan

**Files:** Create `cve/vendor/fetch.sh` (tarball URL + sha256 per project, `-fsanitize=address` build into `cve/vendor/build/<project>/`), `cve/vendor/README.md`
- [ ] **Step 1:** Script fetches and builds OpenSSL 1.0.1f, libpng (selected version), libtiff (selected version) with ASan; verification: each build produces its library and `fetch.sh --verify` checks sha256s.
- [ ] **Step 2:** Commit (scripts only; builds are gitignored).

#### Task 4.3: PoC inputs and original-parser drivers

**Files:** Create `cve/<id>/poc/<file>` + `cve/<id>/poc/PROVENANCE.md`, `cve/<id>/harness/original_driver.cpp`
- [ ] **Step 1:** For each CVE, commit the triggering input with provenance; where redistribution is unclear, commit a script that generates it and the sha256 of the result.
- [ ] **Step 2:** For each CVE, a libFuzzer-compatible driver (`LLVMFuzzerTestOneInput`) that feeds the input to the original parser at the vulnerable version (reuse the project's own fuzz driver when the version has one, otherwise the smallest call sequence that reaches the parsing code). Verification: running the driver on the PoC under ASan reports the overflow (`ERROR: AddressSanitizer`). If a CVE cannot be reproduced this way, record it in `cve/SELECTION.md` as "original not reproduced: <reason>" — the contract test in Wave 2 still stands on its own.
- [ ] **Step 3:** Commit `cve: PoCs with provenance; original-parser drivers reproduce under ASan`.

### P5 — handwritten bench parsers, bench runner, CI matrix (worktree `p5-bench`)

#### Task 5.1: Handwritten bounds-checked parsers

**Files:** Create `bench/handwritten/{tls_heartbeat,png_chunks,tiff_ifd}.hpp`; Test: `tests/bench_handwritten.test.cpp`
Plain C++ with explicit `if (remaining < n) return false;` before every read — the honest comparison point, not a strawman.
- [ ] **Step 1: Tests** — each parser accepts a valid sample and rejects the family's truncated input; `png_chunks_rejects_length_past_end`, `tiff_ifd_rejects_entry_count_past_end`.
- [ ] **Step 2–5:** fail → implement → pass → commit.

#### Task 5.2: Bench runner and report generator

**Files:** Create `bench/bench.cpp`, `bench/CMakeLists.txt`, `scripts/gen-benchmarks.py`
- [ ] **Step 1:** `bench.cpp` parses each sample N = 2000 times per parser, reports median MB/s to stdout as JSON `{format, parser, mbps}`; `gen-benchmarks.py` turns that plus `fixed_prefix_v`/field counts (emitted by a tiny `bench/layout_report.cpp`) and `-ftime-report`-free compile timing (`cmake --build` per schema target, wall clock) into `BENCHMARKS.md` with commit hash and `clang++ --version`. Portcullis rows are filled once P1 lands; until then the runner builds with the handwritten rows only.
- [ ] **Step 2:** Verify `python3 scripts/gen-benchmarks.py` writes a table with the three handwritten rows. Commit.

#### Task 5.3: Complete the CI matrix

- [ ] Add to `ci.yml`: the compile-fail tests in every job, `-Werror`, and a `bench-smoke` step that builds `bench/`; commit.

---

## Wave 2 (after P1 is merged; P7 also after P4)

### P6 — fuzz (worktree `p6-fuzz`)

#### Task 6.1: `Generator<S>::valid()`

**Files:** Create `include/portcullis/fuzz/generator.hpp`; Test: `tests/fuzz_generator.test.cpp`

**Interfaces (frozen):**
```cpp
template <class S> class Generator {
 public:
  explicit Generator(std::uint64_t seed) noexcept;
  std::vector<std::byte> valid();       // parses successfully with parse<S>
  std::vector<std::byte> violating();   // exactly one contract violated; parse<S> fails
};
```
- [ ] **Step 1: Tests** — `valid_outputs_parse` (1000 seeds × heartbeat/png_chunks/tiff_ifd schemas → `parse<S>` ok every time), `valid_hits_boundaries` (over 1000 seeds, lengths 0 and the cap, and every `Choice` case, each appear at least once), `deterministic_per_seed`.
- [ ] **Step 2–5:** fail → implement (walk with `for_each_field`; emit `Ref` targets consistent with what follows; boundary bias) → pass → commit.

#### Task 6.2: `Generator<S>::violating()`
- [ ] **Step 1: Tests** — `violating_outputs_fail` (1000 seeds → every output fails `parse<S>`), `violations_cover_each_kind` (over 1000 seeds, `Truncated`, `BadTag`, `TooManyElements` and `RefinementFailed` each appear when the schema has the construct).
- [ ] **Step 2–5:** fail → implement → pass → commit.

#### Task 6.3: `mutate<S>()`

**Files:** Create `include/portcullis/fuzz/mutator.hpp`; Test: `tests/fuzz_mutator.test.cpp` (built with the `clang-asan` preset in CI)

**Interfaces (frozen):**
```cpp
template <class S>
std::size_t mutate(std::span<std::byte> buf, std::size_t size, std::size_t max_size, std::uint32_t seed) noexcept;
// Returns the new size ≤ max_size. Ops: grow/shrink a length or count field, swap a Choice tag, truncate at a field boundary,
// duplicate an Array element, flip a byte (fallback when the input does not parse).
```
- [ ] **Step 1: Tests** — `mutate_respects_max_size` (Review Focus 5: 10,000 iterations over random inputs, never `> max_size`, ASan clean), `field_level_ops_are_reachable` (from a valid input, over 1000 seeds, at least one output has a changed length field, one a swapped tag, one a shorter size at a field boundary), `unparseable_input_still_mutates` (returns a size > 0, differs from input).
- [ ] **Step 2–5:** fail → implement → pass → commit.

#### Task 6.4: libFuzzer hook and fuzz targets

**Files:** Create `include/portcullis/fuzz/libfuzzer.hpp` (`PORTCULLIS_FUZZ_TARGET(S)` defines `LLVMFuzzerTestOneInput` calling `parse<S>` and `LLVMFuzzerCustomMutator` calling `mutate<S>`), `fuzz/targets/{heartbeat,png_chunks,tiff_ifd}.cpp`, `fuzz/CMakeLists.txt` (`-fsanitize=fuzzer,address,undefined`, Linux only), `.github/workflows/fuzz-smoke.yml` (each target `-max_total_time=60`)
- [ ] **Step 1:** Build the three targets with the `clang-asan` preset on Linux (WSL or CI); run each for 60 s. Expected: no crash, no sanitizer report.
- [ ] **Step 2:** Commit `fuzz: libFuzzer hook, three targets, fuzz-smoke workflow`; write `docs/reports/P6-report.md`.

### P7 — the six contracts (worktrees `p7-tls`, `p7-png`, `p7-tiff`; one agent each)

#### Task 7.1 (×6): `cve/<id>/contract.hpp`, `reject.test.cpp`, `README.md`

- [ ] **Step 1: Write `reject.test.cpp` first** — `parse<Contract>(Untrusted(poc))` fails with the kind and path named in `cve/SELECTION.md` (e.g. CVE-2014-0160: `Truncated`, `"payload"`); plus one `accepts_well_formed_record` test with a hand-built valid input.
- [ ] **Step 2: Run** — Expected: build failure (no contract).
- [ ] **Step 3: Write `contract.hpp`** — only the structures involved in the bug, with a `Refine` for any rule the combinators do not express (CRC, cross-field invariant), named after the rule.
- [ ] **Step 4: Run** — PASS. **Step 5:** `README.md`: the bug in two paragraphs, the violated field relationship, how the contract expresses it, links. **Step 6: Commit** `cve/<id>: contract and reject test`. After the family's two CVEs: `docs/reports/P7-<family>-report.md`.

### P8 — TaggedArena (worktree `p8-arena`)

#### Task 8.1: Portable arena and `materialize`

**Files:** Create `include/portcullis/arena/tagged_arena.hpp`; Test: `tests/arena.test.cpp`

**Interfaces (frozen):**
```cpp
class TaggedArena {
 public:
  explicit TaggedArena(std::size_t capacity);
  void* allocate(std::size_t bytes, std::size_t align) noexcept;   // nullptr when exhausted — never UB
  static bool mte_available() noexcept;                             // HWCAP2_MTE on aarch64 Linux, else false
  bool tagging_active() const noexcept;
  ~TaggedArena();
};
template <class S> Parsed<S>* materialize(const Parsed<S>& p, TaggedArena& a) noexcept;   // deep copy: integers and the bytes behind every span; nullptr when the arena is exhausted
```
- [ ] **Step 1: Tests** — `allocate_returns_aligned_memory`, `exhaustion_returns_nullptr` (not a crash), `materialize_copies_spans_out_of_the_input` (free/overwrite the input buffer after materialize; the materialized spans still read the original bytes), `tagging_inactive_without_mte` (on x86-64).
- [ ] **Step 2–5:** fail → implement (bump arena over `std::aligned_alloc`/`mmap`) → pass → commit.

#### Task 8.2: MTE path and the QEMU job

**Files:** Modify `tagged_arena.hpp` (`#if defined(__aarch64__) && defined(__ARM_FEATURE_MEMORY_TAGGING)`: `mmap` with `PROT_MTE`, `prctl(PR_SET_TAGGED_ADDR_CTRL, PR_TAGGED_ADDR_ENABLE | PR_MTE_TCF_SYNC | (0xfffe << PR_MTE_TAG_SHIFT))`, `__arm_mte_create_random_tag`/`__arm_mte_set_tag` per 16-byte granule); Test: `tests/arena_mte.test.cpp` (runs only when `mte_available()`); Create `.github/workflows/mte.yml`
- [ ] **Step 1: Test** — `overflow_past_allocation_faults`: fork; the child allocates 32 bytes, writes byte 48 (next granule), and must die with `SIGSEGV`; the parent asserts the signal. Skipped (reported as such) when `!mte_available()`.
- [ ] **Step 2:** `mte.yml`: cross-compile with `clang++ --target=aarch64-linux-gnu -march=armv8.5-a+memtag` (Ubuntu `g++-aarch64-linux-gnu` sysroot), run the arena tests under `qemu-aarch64 -cpu max`. Expected: `overflow_past_allocation_faults` PASS. If QEMU's MTE proves unreliable, mark the job `continue-on-error: true`, say so in the report and the README (spec §11).
- [ ] **Step 3:** Commit; `docs/reports/P8-report.md` with the documented limits (16-byte granule, heap only, detection by fault, Linux/Android only).

---

## Wave 3 — integration (integrator)

### Task 9.1: Merge, run everything live, fix what only integration finds
- [ ] Merge P6, P7, P8 into `main`; `ctest` on both presets locally; the full suite under `clang-asan` on Linux (WSL or a CI run read with `gh`); every compile-fail test; every fuzz target 60 s. Fix integration-only issues on `main` and record them in `docs/reports/wave2-integration-report.md`.

### Task 9.2: Wire the CVE harnesses and generate the reports
- [ ] For each CVE: a `harness/contract_driver.cpp` (`PORTCULLIS_FUZZ_TARGET(Contract)`) and a run script that drives the original driver (Task 4.3) and the contract driver with the schema mutator for a fixed budget (`-max_total_time=300` each); `scripts/gen-cves.py` writes `CVES.md`: per CVE — rejected (kind, path) yes/no, original time-to-first-crash (or "not reproduced: reason"), contract-rewrite crashes (expected 0), commit, toolchain. Run `scripts/gen-benchmarks.py` with the Portcullis rows filled. Both files are committed with the numbers as measured.

### Task 9.3: README, architecture doc, roadmap, final decision entries
- [ ] README: the narrow claim in the first screen, what is not protected, the CVES.md and BENCHMARKS.md tables, quick start (`cmake --preset`, the Heartbeat example), browser-free. `docs/architecture.md` (units, the one-walker rule, data flow). `docs/roadmap.md` updated from reports. Any decision made during integration recorded. Commit. The repository is pushed only when the owner says so.

---

## Self-review notes

- Spec coverage: §4 units → Tasks 0.2, 1.1–1.11, 6.1–6.4, 8.1–8.2; §5 API → 1.4–1.11 (+ Partial/field in 1.5); §5.3 layout → 1.3 and the early-out in 1.4; §6 fuzz → 6.x; §7 arena → 8.x; §8.1 layers → unit tests throughout, compile-fail in 0.2/1.5/1.7/1.9/1.11, fuzz in 6.4, CVE replay in 4.x/7.1/9.2; §8.3 benchmarks → 5.1–5.2 and 9.2; §8.4 CI → 0.3, 5.3, 6.4, 8.2; §9 done → 9.1–9.3; §10 decisions → Task 0.1 (D-001…D-010) plus those made in 4.1 and 9.3; §11 risks → carried into Tasks 4.2–4.3 (pinned builds, provenance), 8.2 (QEMU fallback), 6.4 (fuzz on Linux only).
- Interfaces referenced across tasks use the exact names defined in their frozen blocks (`Cursor::take/read`, `FieldInfo`, `field_index_v`, `fixed_prefix_v`, `Parsed::get`, `Partial`, `field<"n">(v)`, `Generator::valid/violating`, `mutate`, `TaggedArena::allocate`, `materialize`).
- Open to the executor by design: internal `Parser<T>` specialization layout, `PathBuilder` representation, the generator's boundary-bias constants, `fetch.sh`'s exact tarball mirrors.
