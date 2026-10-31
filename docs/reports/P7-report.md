# P7 report — the six CVE contracts

Unit P7 (worktree `p7-contracts`, branch `p7-contracts`): for each of the six
replay CVEs, a declarative Portcullis contract that enforces the length/count
invariant the original parser skipped, and a reject-test proving the contract
refuses a structurally-malformed input built directly from the public format
structure. Defensive work only: no exploit code is fetched, written, or run; all
trigger vectors are synthesized in the reject-tests from the RFC / PNG / TIFF
structure (a header field declaring more than the bytes present).

Built against the frozen P1 core and schema/parse headers (unchanged).
**Toolchain:** WSL Ubuntu — clang 21.1.8 and g++ 15.2.0, CMake 4.2.3, Ninja,
`-std=c++20 -Wall -Wextra -Werror` (presets `clang-release`, `gcc-release`).
All reject tests compile into the main `portcullis_tests` via a `cve/*/reject.test.cpp`
glob added to `tests/CMakeLists.txt`.

## Result: 6/6 rejected, all well-formed records accepted

| CVE | family | contract (invariant) | reject `kind` | reject `path` |
| --- | --- | --- | --- | --- |
| CVE-2014-0160 Heartbleed | TLS | `Bytes<Ref("payload_length")>` (`len ≤ remaining`) | `Truncated` | `payload` |
| CVE-2014-0195 DTLS fragment | TLS | `Refine` `fragment_offset + fragment_length ≤ msg_length` | `RefinementFailed` | `fragment_within_message` |
| CVE-2015-8126 PNG palette | PNG | `Refine` `num_palette ≤ 2^bit_depth` + `Array` cap 256 | `RefinementFailed` | `palette_fits_bit_depth` |
| CVE-2015-8540 PNG keyword | PNG | `Refine` `1 ≤ keyword_length ≤ chunk_length` | `RefinementFailed` | `keyword_length_valid` |
| CVE-2004-1307 TIFF strips | TIFF | `Array<U32BE, Ref("strip_count"), 2^20>` | `Truncated` (also `TooManyElements`) | `strip_offsets[1]` |
| CVE-2010-4665 TIFF IFD | TIFF | `Array<IfdEntry, Ref("entry_count"), 4096>` | `TooManyElements` (also `Truncated`) | `entries` |

Each entry also asserts an `accepts_well_formed_record` case with a hand-built
valid vector. Totals: 14 CVE reject-test cases, 60 assertions, green on both
clang and gcc; the full suite is 79 cases / 295 assertions, warning-clean. Every
`ErrorKind` relevant to length/count bugs is exercised: `Truncated`,
`RefinementFailed`, and `TooManyElements`.

## Locked CVEs (all three kept as specified in SELECTION.md)

- **CVE-2014-0160** — one line, `Bytes<Ref("payload_length")>`; the length-checked
  read refuses the declared 65535 against one present byte before any span exists.
- **CVE-2014-0195** — cross-field `Refine`, run before the fragment body is read.
  DTLS encodes the three length fields as uint24; Portcullis v1 has no U24
  primitive, so they are modeled as `U32BE` and the vectors laid out to match
  (documented in the CVE's README). The invariant — the subject of the CVE — is
  expressed exactly, with the sum widened to 64 bits so it cannot wrap.
- **CVE-2015-8126** — the `≤ 2^bit_depth` rule is a `Refine`; the absolute `≤ 256`
  is the `Array` element cap. `num_palette` modeled as `U16BE` so the palette can
  reach the format maximum of 256 while the cap stays meaningful.

## Provisional CVEs: one confirmed, two swapped

- **CVE-2015-8540 (PNG) — CONFIRMED (kept).** It reads as a clean length refine:
  `keyword_length ≥ 1` is exactly the condition whose absence let the
  trailing-space trim underflow an unsigned length, and `≤ chunk_length` keeps
  the keyword in its chunk. Expressed as `Refine "keyword_length_valid"`.

- **CVE-2016-9535 (TIFF) — SWAPPED → CVE-2004-1307.** Confirmed (via the upstream
  fixes `3ca657a` / `6a984bf`, which add `cc % stride` / `occ0 % rowsize` tests)
  that its root cause is a *computed* predictor row/stride size —
  `ImageWidth × SamplesPerPixel × BitsPerSample`, perturbed by YCbCr subsampling —
  not a single declared length/count field, so it does not read as a clean bounds
  contract. Swapped for **CVE-2004-1307**, the textbook strip-count bug in the
  same library and the same source file (`tif_dirread.c`): the attacker-controlled
  strip count drove an allocation and copy with no sanity check.
  `Array<U32BE, Ref("strip_count"), Cap>` is that missing check — `TooManyElements`
  before allocation for an absurd count, `Truncated` for a count past the data.
  Caveat noted in its README: CVE-2004-1307 pre-dates the 2014–2017 window of the
  other five; it was chosen for a clean count-field contract in the same family.

- **CVE-2017-9147 (TIFF) — SWAPPED → CVE-2010-4665.** Confirmed it is an
  out-of-bounds *read* of an improperly set extension-tag field in
  `_TIFFVGetField` (the read-side twin of CVE-2015-7554) — a field-presence /
  field-typing defect, not a length or count bound. Swapped for the exact shape
  SELECTION.md proposed: an IFD whose declared entry count drives the over-read.
  **CVE-2010-4665** is the documented precedent ("a crafted TIFF file containing a
  directory data structure with a large number of directory entries").
  `Array<IfdEntry, Ref("entry_count"), 4096>` rejects an absurd count with
  `TooManyElements` before `entry_count × 12` is ever computed (closing the
  integer-overflow path), and a count past the data with `Truncated`. Caveat in
  its README: the precedent is 2010 and lives in the reader path rather than being
  pinned to a single in-window library commit; the malformation is a TIFF6
  structural invariant (`2 + entry_count × 12 + 4 ≤ bytes available`), synthesized
  from the spec.

## Layout notes and scope

- The reject tests live beside their contracts in `cve/<id>/reject.test.cpp`;
  `tests/CMakeLists.txt` globs `cve/*/reject.test.cpp` into `portcullis_tests`.
  No library header or other unit's files were touched. No `poc/` files are
  committed — trigger vectors are synthesized inline per SELECTION.md.
- Not done here (by design): building the original vulnerable libraries under
  AddressSanitizer for the time-to-first-crash data point is deferred to Linux/CI
  (§8.2, P4-report); these contracts and reject-tests stand on the synthesized
  trigger vectors.
