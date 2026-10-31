# CVE selection (unit P4)

Six historical, long-patched parser vulnerabilities that Portcullis contracts are written to reject. The purpose is **defensive**: each entry documents the length/count invariant a contract enforces and the error it reports, so Wave 2 (P7) can write the contract and a reject-test, and prove a small declared schema catches a famous memory-safety bug. These are the standard methodology of ground-truth fuzzing benchmarks (Magma, EPFL).

**Trigger inputs are synthesized, not redistributed.** For a length/count bug the triggering input is simply a structurally-malformed header — a field that declares more than the bytes present, or a count past a documented bound — constructed directly from the public format/protocol specification (RFC, PNG/TIFF spec). P7 builds these as small test vectors in the reject-tests. No exploit payloads are fetched or committed. The optional step of building the *original* vulnerable library under AddressSanitizer to show the crash is a separate, secondary data point deferred to Linux/CI (see `docs/reports/P4-report.md`).

Selection criterion (plan §8.2): the bounds violation is driven by a length or count field, and the invariant is expressible as a Portcullis contract.

## Locked (verified, clean length/count invariant)

### CVE-2014-0160 — OpenSSL 1.0.1 "Heartbleed"
- **Component / version:** OpenSSL 1.0.1 through 1.0.1f; fixed 1.0.1g.
- **Structure (RFC 6520):** heartbeat message = `{ type: u8, payload_length: u16, payload: bytes[payload_length], padding: bytes[>=16] }`.
- **Invariant violated:** the server trusted `payload_length` and copied that many bytes from the request back into the response without checking it against the bytes actually received. `payload_length` must be ≤ the bytes remaining in the record.
- **Contract:** `Bytes<Ref("payload_length")>` — the length-checked read refuses when the declared length exceeds what is available.
- **Expected reject:** `ErrorKind::Truncated`, path `"payload"`, `need = payload_length`, `have = actual`.
- **Trigger vector:** `type = 1`, `payload_length = 0xFFFF`, one byte of payload.
- **Refs:** CVE-2014-0160; RFC 6520.

### CVE-2014-0195 — OpenSSL DTLS invalid fragment
- **Component / version:** OpenSSL before 0.9.8za / 1.0.0m / 1.0.1h; `dtls1_reassemble_fragment` in `d1_both.c`.
- **Structure:** a DTLS handshake fragment header carries `msg_length`, `fragment_offset`, `fragment_length`. A non-initial fragment whose range exceeds the message overflowed the reassembly buffer.
- **Invariant violated:** `fragment_offset + fragment_length ≤ msg_length`.
- **Contract:** the fragment header as a `Struct`, plus `Refine<[](auto& v){ return field<"fragment_offset">(v) + field<"fragment_length">(v) <= field<"msg_length">(v); }, "fragment_within_message">`.
- **Expected reject:** `ErrorKind::RefinementFailed`, name `"fragment_within_message"`, path `"fragment_within_message"`.
- **Trigger vector:** `msg_length = 16`, `fragment_offset = 0`, `fragment_length = 0xFFFF`.
- **Refs:** CVE-2014-0195; Red Hat bug 1103598.

### CVE-2015-8126 — libpng PLTE/bit-depth palette overflow
- **Component / version:** libpng before 1.6.19 (and 1.5.24, 1.4.17, 1.2.54, 1.0.64).
- **Structure:** IHDR carries `bit_depth`; the PLTE chunk carries `3 * num_palette` bytes. For `bit_depth < 8` the palette may hold at most `2^bit_depth` entries, but libpng returned up to 256, overflowing a caller's `2^bit_depth`-sized buffer.
- **Invariant violated:** `num_palette ≤ 2^bit_depth` (and the absolute `≤ 256`).
- **Contract:** `Array<Rgb, Ref("num_palette"), 256>` (the cap enforces the absolute 256), plus `Refine<..., "palette_fits_bit_depth">` for the `≤ 2^bit_depth` cross-field rule. `num_palette` is derived from the PLTE chunk length.
- **Expected reject:** `ErrorKind::RefinementFailed`, name `"palette_fits_bit_depth"` (or `TooManyElements` when the count exceeds the 256 cap).
- **Trigger vector:** IHDR `bit_depth = 2` (max 4 entries), PLTE with 16 entries.
- **Refs:** CVE-2015-8126; Debian security tracker.

## Provisional (verified bug; contract shape to confirm during P7, swap if it is not cleanly a length/count contract)

### CVE-2015-8540 — libpng keyword-length underflow
- **Component / version:** libpng before 1.5.26 (and earlier branches); `png_check_keyword` in `pngwutil.c`.
- **Bug:** an integer underflow in keyword-length handling (a short/space keyword) leads to an out-of-bounds read.
- **Candidate contract:** model the tEXt keyword field as bounded within its chunk with `Refine<..., "keyword_length_valid">` ensuring the trimmed keyword length does not underflow (`end > start`, length ≥ 1).
- **Expected reject:** `ErrorKind::RefinementFailed`, name `"keyword_length_valid"` (or `OutOfRange`).
- **Confirm:** the exact underflow condition against the upstream fix for libpng bug 244 before finalizing the contract.

### CVE-2016-9535 — libtiff predictor row-size overflow
- **Component / version:** libtiff 4.0.6; `tif_predict.h`/`tif_predict.c` (MSVR 35105). Heap overflow with unusual tile layouts (e.g. YCbCr subsampling).
- **Bug:** the horizontal predictor's computed row size did not match the allocated strip/tile buffer.
- **Candidate contract:** cross-field `Refine` relating `ImageWidth`, `SamplesPerPixel` and `BitsPerSample` to the strip/tile size. This is a computed-size relationship rather than a single length field — keep only if it reads as a clean contract; otherwise replace with a libtiff CVE driven directly by a strip/tile **count** field.
- **Confirm:** against upstream commits 3ca657a / 6a984bf.

### CVE-2017-9147 — libtiff `_TIFFVGetField` out-of-bounds read
- **Component / version:** libtiff 4.0.7; `_TIFFVGetField` in `tif_dir.c` (bug 2693).
- **Bug:** out-of-bounds read fetching a directory field that was not properly set.
- **Candidate contract:** requires modeling the IFD directory-entry count and the presence of referenced fields. This is closer to a field-presence check than a length bound — a strong replacement candidate is a libtiff CVE where the **directory entry count** or **StripByteCounts** array drives a truncation/overflow (maps cleanly to `Array<IfdEntry, Ref("entry_count")>` → `Truncated`/`TooManyElements`).
- **Confirm or replace** during P7.

## Status

Locked and ready for P7: CVE-2014-0160, CVE-2014-0195, CVE-2015-8126. Provisional (confirm or swap): CVE-2015-8540, CVE-2016-9535, CVE-2017-9147. The TIFF pair in particular should move toward count-field-driven bugs if the current two do not express cleanly as bounds contracts. Vulnerable-library reproduction under ASan is deferred to Linux/CI; the reject-tests stand on the synthesized trigger vectors above.
