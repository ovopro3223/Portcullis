# P8 report — TaggedArena (ARM MTE), optional/experimental

Unit P8 (worktree `p8-arena`): `portcullis::arena::TaggedArena`, a bump arena
that holds validated parse output, plus `materialize<S>` which deep-copies a
`Parsed<S>` into it. On aarch64 Linux with the Memory Tagging Extension (MTE)
every allocation is given a random tag and its granules are painted, so any
out-of-bounds access to arena memory — even from code that never went through
the library — faults synchronously. Everywhere else the same API is a plain
portable bump arena. Built on the frozen P1 `Parsed<S>` / `value_of_t` and the
`for_each_field` field list (all unchanged).

Toolchains (WSL Ubuntu "resolute"):
- x86-64: clang 21.1.8 and g++ 15.2.0, CMake 4.2.3, Ninja 1.13.2, presets
  `clang-release` / `gcc-release`, `-Wall -Wextra -Werror`.
- aarch64 cross: `clang++ --target=aarch64-linux-gnu` 21.1.8 and
  `aarch64-linux-gnu-g++` 15.2.0, both `-march=armv8.5-a+memtag`, run under
  `qemu-aarch64 -cpu max` 10.2.1 (emulates FEAT_MTE).

## What was built

- `include/portcullis/arena/tagged_arena.hpp` (new, the only header):
  - `TaggedArena(std::size_t capacity)` — mmaps a `PROT_MTE` region and enables
    the tagged-address ABI + synchronous tag checking (`prctl`) on the MTE path;
    otherwise a page-aligned buffer from aligned `operator new`.
  - `void* allocate(bytes, align) noexcept` — aligns and bumps; returns
    `nullptr` on exhaustion (never UB, never a partial allocation). On the MTE
    path it rounds to the 16-byte granule, assigns one random tag per allocation
    via `__arm_mte_create_random_tag`, paints every granule with
    `__arm_mte_set_tag`, and returns the tagged pointer.
  - `static bool mte_available()` — `HWCAP2_MTE` via `getauxval` on aarch64
    Linux, else `false`.
  - `bool tagging_active()` — true only when MTE is compiled in, available at
    runtime, and the mmap + prctl setup succeeded.
  - `template <class S> Parsed<S>* materialize(const Parsed<S>&, TaggedArena&)`
    — recursively relocates: integers by value, and the bytes behind every
    `std::span<const std::byte>` into arena storage, descending through nested
    `Parsed` (struct), `std::vector` (array), `std::optional` and `std::variant`
    (choice). The returned object and all its spans are independent of the input
    buffer. Returns `nullptr` if the arena exhausts at any point.

The MTE path is `#if defined(__aarch64__) && defined(__ARM_FEATURE_MEMORY_TAGGING)
&& defined(__linux__)`; all POSIX-only members are compiled only inside that
guard so the header stays warning-clean on non-MTE targets (incl. MinGW).

## Tests that run

`tests/arena.test.cpp` (portable; runs on every target including x86-64 and, via
cross-build, aarch64):

- `allocate_returns_aligned_memory` — alignments 1..64 all satisfied, region
  writable for its full length.
- `exhaustion_returns_nullptr` — an over-capacity request and a >arena request
  both return `nullptr`; an earlier allocation stays valid.
- `materialize_copies_spans_out_of_the_input` — parse a `Struct<U8, U32BE,
  Bytes<3>>`, materialize, then overwrite **and free** the input vector; the
  materialized span and `bytes()` still read the original bytes and live outside
  the input range; integers copied by value.
- `tagging_inactive_without_mte` — off MTE hardware, `mte_available()` and
  `tagging_active()` are both `false` and the arena still allocates.

`tests/arena_mte.test.cpp`:

- `overflow_past_allocation_faults` — forks; the child restores the default
  disposition for SIGSEGV/SIGBUS/SIGABRT (so doctest's inherited crash handler
  does not intercept the fault), allocates 32 bytes and writes one granule past
  it (offset 48); the parent asserts the child died with `SIGSEGV`
  (`WIFSIGNALED` + `WTERMSIG == SIGSEGV`). **Skipped and reported as skipped when
  `!mte_available()`** (e.g. on x86-64), and compiled to a skip on non-POSIX
  hosts.

### Measured results

- **x86-64, `clang-release`:** arena cases 4 passed / MTE case skipped; full
  `ctest` suite green, no regressions. `gcc-release`: identical.
- **aarch64 under `qemu-aarch64 -cpu max`:** MTE is active (0 skipped) — all 5
  arena cases pass, 35 assertions, clean output, for **both** the clang and the
  gcc cross-build. `overflow_past_allocation_faults` genuinely trips a
  synchronous MTE tag-check fault; `materialize_*` also passes on the MTE path
  (its spans land in tagged granules).
- Warnings-as-errors clean on all four builds; the only diagnostic is the
  vendored doctest `<ciso646>` `#warning`, already downgraded by `tests/
  CMakeLists.txt` / the `-Wno-error` flags, exactly as the rest of the suite.

## CI — `.github/workflows/mte.yml`

Installs `clang`, `g++-aarch64-linux-gnu`, `qemu-user`, `libc6-dev-arm64-cross`;
cross-compiles the three arena sources with
`clang++ --target=aarch64-linux-gnu -march=armv8.5-a+memtag` (and again with the
gcc cross-compiler) and runs each binary under `qemu-aarch64 -cpu max`.

The job is marked **`continue-on-error: true`** (experimental), per spec §11.
Reason: it passes reliably in local verification under QEMU 10.2.1 `-cpu max`,
but the reliability of MTE emulation on **GitHub's hosted QEMU** could not be
exercised from the dev environment (the repo is not pushed yet), and the spec
explicitly allows this job to stay green-optional rather than gate merges on an
experimental feature the core does not depend on. If it proves solid on the
hosted runner, the integrator should drop that one line to make it a hard gate.

Note on the toolchain invocation: clang cross-links cleanly with **plain
`--target=aarch64-linux-gnu`** (auto-detecting the `g++-aarch64-linux-gnu`
sysroot). Passing an explicit `--sysroot=/usr/aarch64-linux-gnu` makes `ld.bfd`
fail to find `libm.so.6`; the workflow deliberately omits it.

## Documented limits (spec §7, §11)

- **16-byte granule.** Tags apply to aligned 16-byte chunks, so an overflow that
  stays inside the last (partly used) granule of an allocation is **not** caught.
- **Heap only.** Protection covers arena allocations; stacks, globals and
  ordinary heap are untagged. Vector/optional control buffers inside a
  materialized `Parsed` are ordinary heap (they are self-owned and survive the
  input being freed, but are not tag-protected) — only the `Bytes` payloads and
  the `Parsed` object itself are arena-backed.
- **Detection, not prevention.** MTE reports the violation by faulting after it
  happens (synchronous check); it does not stop the store from being attempted.
- **Linux/Android + aarch64 only**, and only on CPUs advertising `HWCAP2_MTE`.
  Everywhere else the arena is a plain portable bump allocator with no tagging.
- **Experimental.** Time-boxed wave; the core library does not depend on it.

## Interfaces / open items for the integrator

- All Appendix-A frozen signatures are matched exactly
  (`TaggedArena::allocate/mte_available/tagging_active`,
  `materialize(const Parsed<S>&, TaggedArena&)`). **No interface changed**, so
  there is no `docs/change-requests/` entry. The types live in
  `portcullis::arena` (spec §7 / brief), not bare `portcullis`.
- **README caveat not added here.** Task 8.2 asks for the experimental/limits
  note to also appear in the README, but the worktree is restricted to
  `include/portcullis/arena/`, `tests/arena*.test.cpp` and
  `.github/workflows/mte.yml`, and the README is the integrator's Wave-3
  deliverable. Please fold the "Documented limits" block above and the
  `continue-on-error` reason into the README's "what the library does not
  protect" section.
- **Umbrella header.** `include/portcullis/portcullis.hpp` does **not** include
  the arena, which is correct for an optional, platform-specific, experimental
  unit — tests include `<portcullis/arena/tagged_arena.hpp>` directly. Flagged
  only so the omission is a decision, not an oversight.
- **`materialize` is `noexcept` (frozen).** The deep copy of array fields uses
  `std::vector`, whose growth could in principle throw `bad_alloc`; under the
  frozen `noexcept` that would terminate. No test hits it (small inputs, and the
  arena — not the vector — is the exhaustion path under test). Noted in case a
  future change wants the signature relaxed.
