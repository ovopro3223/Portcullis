# CVEs rejected by Portcullis contracts

Six historical, long-patched parser vulnerabilities, each a memory-safety bug driven by a length or count field. For each, a small declarative Portcullis contract — the schema a careful author would write anyway — **rejects the triggering input with a specific, named error**, where the original code read or wrote out of bounds.

This is defensive: the purpose is to show that enforcing the length/count invariant at the parse boundary catches a whole class of bug, the methodology of ground-truth fuzzing benchmarks (Magma). The triggering inputs are **synthesized from each format's public specification** (a header field that declares more than the bytes present), not redistributed exploit artifacts. Every row is proven by a committed test (`cve/<id>/reject.test.cpp`) that runs in CI on clang and gcc; the whole suite is **6/6 rejected with the expected error, and well-formed inputs accepted**.

| CVE | Component | The invariant the original missed | Portcullis contract | Rejects with |
| :--- | :--- | :--- | :--- | :--- |
| [CVE-2014-0160](cve/CVE-2014-0160/) (Heartbleed) | OpenSSL 1.0.1 | heartbeat `payload_length` ≤ bytes in the record | `Bytes<Ref("payload_length")>` | `Truncated` @ `payload` |
| [CVE-2014-0195](cve/CVE-2014-0195/) | OpenSSL DTLS | `fragment_offset + fragment_length` ≤ `msg_length` | `Refine<…, "fragment_within_message">` | `RefinementFailed` @ `fragment_within_message` |
| [CVE-2015-8126](cve/CVE-2015-8126/) | libpng | PLTE entries ≤ 2^`bit_depth` (and ≤ 256) | `Refine` + `Array<…, cap 256>` | `RefinementFailed` @ `palette_fits_bit_depth` |
| [CVE-2015-8540](cve/CVE-2015-8540/) | libpng | tEXt keyword length ≥ 1 and within the chunk | `Refine<…, "keyword_length_valid">` | `RefinementFailed` @ `keyword_length_valid` |
| [CVE-2004-1307](cve/CVE-2004-1307/) | libtiff | strip count/offsets fit the file | `Array<U32BE, Ref, 2^20>` | `Truncated` / `TooManyElements` @ `strip_offsets` |
| [CVE-2010-4665](cve/CVE-2010-4665/) | libtiff | IFD entry count fits the file | `Array<IfdEntry, Ref, 4096>` | `TooManyElements` / `Truncated` @ `entries` |

## Why these, and how the contract catches each

- **Heartbleed** is the canonical example: the server trusted a 16-bit length and copied that many bytes from a request that did not contain them. In Portcullis the payload is `Bytes<Ref("payload_length")>`; the single length-checked read refuses the moment the declared length exceeds what is present, with `Truncated` at `payload` and the needed/available byte counts. A one-line field declaration is the whole fix.
- **DTLS fragment (2014-0195)**, **PNG palette (2015-8126)** and **PNG keyword (2015-8540)** are cross-field or derived-count invariants the primitive combinators do not express, so each contract names a `Refine` predicate after the rule — and the error carries that name, so a rejection says *which* invariant failed.
- **The two libtiff bugs (2004-1307, 2010-4665)** are count fields — a strip count and an IFD entry count — that promise more data than the file holds. `Array<T, Ref("count"), Cap>` both caps the count (so a hostile count cannot make the parser allocate or loop unboundedly) and refuses when the elements run past the end.

## Scope and honesty

- **Selection.** Three were locked from the start (Heartbleed, DTLS, PNG palette). The two libtiff candidates first considered — CVE-2016-9535 (a predictor row-size computation) and CVE-2017-9147 (a field-presence read) — are not clean length/count-field bounds, so they were **swapped** for count-field bugs that are (strip count, IFD entry count), per the project's own selection criterion. The rationale is recorded in each `cve/<id>/README.md` and `cve/SELECTION.md`.
- **What this does and does not claim.** It claims that a parser written *through* Portcullis rejects these inputs before any out-of-bounds access. It does **not** claim Portcullis makes an arbitrary program memory-safe — code that does not go through the library is not protected by it. The optional step of building each original vulnerable library under AddressSanitizer to show the crash is a secondary data point, deferred to Linux CI (`cve/vendor/`); the contract rejections above stand on their own.
