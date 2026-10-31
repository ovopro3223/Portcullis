// Tests for the handwritten bounds-checked parsers in bench/handwritten/.
//
// These parsers are the honest comparison point for the throughput numbers in
// BENCHMARKS.md, so they must be real parsers: they accept a valid sample and
// reject truncated or lying input. If they were wrong, the comparison would
// be too.
#include "third_party/doctest.h"

#include "../bench/handwritten/png_chunks.hpp"
#include "../bench/handwritten/tiff_ifd.hpp"
#include "../bench/handwritten/tls_heartbeat.hpp"
#include "../bench/samples.hpp"

#include <cstddef>
#include <cstdint>
#include <initializer_list>

namespace {

using samples::Bytes;

// Every strict prefix of `full` must be rejected by `parse`.
template <class Parse>
bool rejects_every_strict_prefix(const Bytes& full, Parse parse) {
  for (std::size_t n = 0; n < full.size(); ++n) {
    if (parse(full.data(), n)) return false;
  }
  return true;
}

}  // namespace

// ---------------------------------------------------------------- TLS ----

TEST_CASE("tls_heartbeat_accepts_valid_record") {
  const Bytes rec = samples::tls_heartbeat(5, /*type=*/1);
  handwritten::TlsHeartbeat hb{};
  REQUIRE(handwritten::parse_tls_heartbeat(rec.data(), rec.size(), hb));
  CHECK(hb.type == 1);
  CHECK(hb.payload_length == 5);
  CHECK(hb.payload == rec.data() + 3);
  CHECK(hb.padding == rec.data() + 3 + 5);
}

TEST_CASE("tls_heartbeat_accepts_empty_and_maximum_payload") {
  for (std::size_t len : {std::size_t{0}, std::size_t{65535}}) {
    const Bytes rec = samples::tls_heartbeat(len);
    handwritten::TlsHeartbeat hb{};
    REQUIRE(handwritten::parse_tls_heartbeat(rec.data(), rec.size(), hb));
    CHECK(hb.payload_length == len);
  }
}

TEST_CASE("tls_heartbeat_rejects_heartbleed_record") {
  // CVE-2014-0160 shape: declared payload length 65535, one payload byte
  // actually present, no room for the padding.
  const Bytes rec = {0x01, 0xFF, 0xFF, 0x41};
  handwritten::TlsHeartbeat hb{};
  CHECK_FALSE(handwritten::parse_tls_heartbeat(rec.data(), rec.size(), hb));
}

TEST_CASE("tls_heartbeat_rejects_payload_length_past_end") {
  // A record whose payload is 5 bytes followed by 16 bytes of padding. Raise
  // the declared length by d: d = 1 leaves the padding one byte short, and
  // d >= 17 pushes the payload itself past the end of the buffer.
  for (unsigned d : {1u, 2u, 16u, 17u, 18u, 1000u}) {
    Bytes rec = samples::tls_heartbeat(5);
    const unsigned declared = 5 + d;
    rec[1] = static_cast<std::uint8_t>(declared >> 8);
    rec[2] = static_cast<std::uint8_t>(declared & 0xFF);
    handwritten::TlsHeartbeat hb{};
    CHECK_FALSE(handwritten::parse_tls_heartbeat(rec.data(), rec.size(), hb));
  }
}

TEST_CASE("tls_heartbeat_rejects_every_truncation") {
  for (std::size_t len : {std::size_t{0}, std::size_t{1}, std::size_t{200}}) {
    const Bytes rec = samples::tls_heartbeat(len);
    CHECK(rejects_every_strict_prefix(rec, [](const std::uint8_t* p, std::size_t n) {
      handwritten::TlsHeartbeat hb{};
      return handwritten::parse_tls_heartbeat(p, n, hb);
    }));
  }
}

// ---------------------------------------------------------------- PNG ----

namespace {

bool png_ok(const Bytes& b) {
  handwritten::PngChunks out{};
  return handwritten::parse_png_chunks(b.data(), b.size(), out);
}

// signature followed by raw chunk bytes the caller wrote by hand
Bytes png_with(std::initializer_list<std::uint8_t> chunk_bytes) {
  Bytes b;
  samples::put_png_signature(b);
  b.insert(b.end(), chunk_bytes.begin(), chunk_bytes.end());
  return b;
}

}  // namespace

TEST_CASE("png_chunks_accepts_valid_file") {
  const Bytes png = samples::png(/*idat_chunks=*/3, /*idat_size=*/100);
  handwritten::PngChunks out{};
  REQUIRE(handwritten::parse_png_chunks(png.data(), png.size(), out));
  CHECK(out.chunk_count == 8);  // IHDR gAMA pHYs tEXt IDAT*3 IEND
  CHECK(out.end_offset == png.size());
  // total size = signature + sum(12 + data length), whatever the builder emits
  CHECK(out.data_bytes == png.size() - 8 - 12 * out.chunk_count);
}

TEST_CASE("png_chunks_accepts_empty_data_chunks_and_ignores_bytes_after_iend") {
  Bytes png = samples::png(0, 0);
  const std::size_t end = png.size();
  CHECK(png_ok(png));
  png.push_back(0xDE);  // trailing garbage after IEND is not part of the PNG
  png.push_back(0xAD);
  handwritten::PngChunks out{};
  REQUIRE(handwritten::parse_png_chunks(png.data(), png.size(), out));
  CHECK(out.end_offset == end);
}

TEST_CASE("png_chunks_rejects_bad_signature") {
  const Bytes good = samples::png(1, 16);
  for (std::size_t i = 0; i < 8; ++i) {
    Bytes bad = good;
    bad[i] ^= 0x01;
    CHECK_FALSE(png_ok(bad));
  }
}

TEST_CASE("png_chunks_rejects_length_past_end") {
  // Declared lengths larger than what remains, at the boundary and far past it.
  // sig + length + "IDAT" + 8 bytes of body: after the type, 8 bytes remain.
  const auto with_len = [](std::uint32_t len) {
    Bytes b;
    samples::put_png_signature(b);
    samples::put_be32(b, len);
    b.insert(b.end(), {'I', 'D', 'A', 'T'});
    samples::put_filler(b, 8, 0x10);
    return b;
  };
  CHECK_FALSE(png_ok(with_len(9)));           // one byte of data past the end
  CHECK_FALSE(png_ok(with_len(8)));           // data fits, its CRC does not
  CHECK_FALSE(png_ok(with_len(5)));           // CRC one byte short
  CHECK_FALSE(png_ok(with_len(1000)));
  CHECK_FALSE(png_ok(with_len(0x7FFFFFFFu))); // largest length the spec allows
  CHECK_FALSE(png_ok(with_len(0x80000000u))); // above the spec limit
  CHECK_FALSE(png_ok(with_len(0xFFFFFFFFu))); // would wrap a 32-bit pos + len
  CHECK_FALSE(png_ok(with_len(0xFFFFFFF8u)));
  // (The parser's 2^31 - 1 cap is a spec rule, not a safety rule: any buffer
  // below 2 GiB already fails the fit check, so no small test can isolate it.)

  // The same lie inside an otherwise valid file: first chunk claims everything.
  Bytes png = samples::png(2, 64);
  const std::uint32_t claim = static_cast<std::uint32_t>(png.size() - 8 - 8);  // no room for the CRC
  png[8] = static_cast<std::uint8_t>(claim >> 24);
  png[9] = static_cast<std::uint8_t>(claim >> 16);
  png[10] = static_cast<std::uint8_t>(claim >> 8);
  png[11] = static_cast<std::uint8_t>(claim);
  CHECK_FALSE(png_ok(png));
}

TEST_CASE("png_chunks_rejects_missing_iend") {
  CHECK_FALSE(png_ok(png_with({})));  // signature only
  Bytes b;
  samples::put_png_signature(b);
  samples::put_png_chunk(b, "IHDR", Bytes(13, 0));
  CHECK_FALSE(png_ok(b));  // well-formed chunk, but the stream never ends
  samples::put_png_chunk(b, "IEND", Bytes{});
  CHECK(png_ok(b));
}

TEST_CASE("png_chunks_rejects_every_truncation") {
  for (std::size_t idat : {std::size_t{0}, std::size_t{3}}) {
    const Bytes png = samples::png(idat, 100);
    CHECK(rejects_every_strict_prefix(png, [](const std::uint8_t* p, std::size_t n) {
      handwritten::PngChunks out{};
      return handwritten::parse_png_chunks(p, n, out);
    }));
  }
}

// --------------------------------------------------------------- TIFF ----

namespace {

using handwritten::TiffIfd;

template <bool kCheckOutOfLine = true>
bool tiff_ok(const Bytes& b) {
  TiffIfd out{};
  return handwritten::parse_tiff_ifd<kCheckOutOfLine>(b.data(), b.size(), out);
}

// header + an IFD at offset 8 holding exactly the given entries, no value area
struct TinyTiff {
  bool le;
  Bytes b;
  explicit TinyTiff(bool little_endian, std::uint16_t declared_count = 0xFFFF)
      : le(little_endian) {
    samples::put_tiff_header(b, le, 8);
    samples::put_tiff_u16(b, le, declared_count);
  }
  TinyTiff& entry(std::uint16_t type, std::uint32_t count, std::uint32_t value) {
    samples::put_tiff_entry(b, le, 300, type, count, value);
    return *this;
  }
  Bytes finish() {
    samples::put_tiff_u32(b, le, 0);
    return b;
  }
};

}  // namespace

TEST_CASE("tiff_ifd_accepts_valid_file_in_both_byte_orders") {
  for (bool le : {true, false}) {
    const Bytes tiff = samples::tiff(12, le);
    TiffIfd out{};
    REQUIRE(handwritten::parse_tiff_ifd(tiff.data(), tiff.size(), out));
    CHECK(out.little_endian == le);
    CHECK(out.entry_count == 12);
    CHECK(out.entries_offset == 10);
    CHECK(out.next_ifd_offset == 0);
  }
}

TEST_CASE("tiff_ifd_sums_value_bytes") {
  // SHORT x1 (2, inline) + ASCII x10 (10, out of line) + DOUBLE x2 (16, out of
  // line); the two out-of-line values live in a 26-byte area after the IFD.
  for (bool le : {true, false}) {
    TinyTiff t(le, 3);
    const std::uint32_t area = 8 + 2 + 36 + 4;
    t.entry(3, 1, le ? 7u : 7u << 16).entry(2, 10, area).entry(12, 2, area + 10);
    Bytes b = t.finish();
    samples::put_filler(b, 26, 0x55);
    TiffIfd out{};
    REQUIRE(handwritten::parse_tiff_ifd(b.data(), b.size(), out));
    CHECK(out.value_bytes == 2 + 10 + 16);
  }
}

TEST_CASE("tiff_ifd_rejects_entry_count_past_end") {
  // Two real entries but a count of 3: the table needs 12*3 + 4 = 40 bytes
  // after the count and only 28 are there.
  for (bool le : {true, false}) {
    TinyTiff t(le, 3);
    t.entry(3, 1, 0).entry(4, 1, 0);
    CHECK_FALSE(tiff_ok(t.finish()));
  }
  // One byte short of the final next-IFD pointer.
  {
    TinyTiff t(true, 2);
    t.entry(3, 1, 0).entry(4, 1, 0);
    Bytes b = t.finish();
    b.pop_back();
    CHECK_FALSE(tiff_ok(b));
  }
  // The largest count a 16-bit field can say, on a tiny file and on a real one.
  {
    TinyTiff t(true, 0xFFFF);
    t.entry(3, 1, 0);
    CHECK_FALSE(tiff_ok(t.finish()));
  }
  {
    Bytes b = samples::tiff(20);
    b[8] = 0xFF;
    b[9] = 0xFF;
    CHECK_FALSE(tiff_ok(b));
  }
  // The exact fit is accepted (the boundary is the right one).
  {
    TinyTiff t(true, 2);
    t.entry(3, 1, 0).entry(4, 1, 0);
    CHECK(tiff_ok(t.finish()));
  }
}

TEST_CASE("tiff_ifd_rejects_bad_header") {
  const Bytes good = samples::tiff(4);
  CHECK(tiff_ok(good));
  { Bytes b = good; b[0] = 'M'; CHECK_FALSE(tiff_ok(b)); }  // "MI"
  { Bytes b = good; b[1] = 'M'; CHECK_FALSE(tiff_ok(b)); }  // "IM"
  { Bytes b = good; b[0] = 'X'; b[1] = 'X'; CHECK_FALSE(tiff_ok(b)); }
  { Bytes b = good; b[2] = 43; CHECK_FALSE(tiff_ok(b)); }   // BigTIFF magic
  { Bytes b = good; b[3] = 1; CHECK_FALSE(tiff_ok(b)); }    // 42 + 256
}

TEST_CASE("tiff_ifd_rejects_ifd_offset_outside_file") {
  const Bytes good = samples::tiff(4);
  const auto with_offset = [&](std::uint32_t off) {
    Bytes b = good;
    b[4] = static_cast<std::uint8_t>(off);
    b[5] = static_cast<std::uint8_t>(off >> 8);
    b[6] = static_cast<std::uint8_t>(off >> 16);
    b[7] = static_cast<std::uint8_t>(off >> 24);
    return b;
  };
  const std::uint32_t n = static_cast<std::uint32_t>(good.size());
  CHECK_FALSE(tiff_ok(with_offset(0)));           // inside the header
  CHECK_FALSE(tiff_ok(with_offset(4)));
  CHECK_FALSE(tiff_ok(with_offset(7)));
  CHECK_FALSE(tiff_ok(with_offset(n)));           // one past the end
  CHECK_FALSE(tiff_ok(with_offset(n - 1)));       // no room for the count
  CHECK_FALSE(tiff_ok(with_offset(0xFFFFFFFFu)));
  CHECK_FALSE(tiff_ok(with_offset(n + 1)));

  // An IFD at offset 4 would overlap the header: its count is the low half of
  // the offset word itself (4), and the zeros after the header decode as
  // entries of unknown type, which are skipped. Only the offset >= 8 rule
  // rejects this file; the control below is the same shape with a legal offset.
  Bytes overlap;
  samples::put_tiff_header(overlap, true, 4);
  overlap.resize(4 + 2 + 48 + 4, 0);  // IFD at 4: count(2) + 4 entries + next(4)
  CHECK_FALSE(tiff_ok(overlap));

  Bytes control;
  samples::put_tiff_header(control, true, 8);
  samples::put_tiff_u16(control, true, 4);
  control.resize(8 + 2 + 48 + 4, 0);
  CHECK(tiff_ok(control));
}

TEST_CASE("tiff_ifd_rejects_out_of_line_value_past_end") {
  const std::uint32_t area = 8 + 2 + 12 + 4;  // value area of a one-entry file
  const auto one_entry = [&](std::uint16_t type, std::uint32_t count, std::uint32_t off,
                             std::size_t area_bytes) {
    TinyTiff t(true, 1);
    t.entry(type, count, off);
    Bytes b = t.finish();
    samples::put_filler(b, area_bytes, 0x33);
    return b;
  };
  // DOUBLE x2 = 16 bytes. Exactly fitting is fine, one byte short is not.
  CHECK(tiff_ok(one_entry(12, 2, area, 16)));
  CHECK_FALSE(tiff_ok(one_entry(12, 2, area, 15)));
  CHECK_FALSE(tiff_ok(one_entry(12, 2, area + 1, 16)));
  // Offset past the end, and offsets that wrap 32-bit arithmetic.
  CHECK_FALSE(tiff_ok(one_entry(12, 2, 0xFFFFFFF8u, 16)));
  CHECK_FALSE(tiff_ok(one_entry(12, 2, 0xFFFFFFFFu, 16)));
  // count * size overflowing 32 bits (8 * 0xFFFFFFFF), and BYTE x 4 GiB.
  CHECK_FALSE(tiff_ok(one_entry(12, 0xFFFFFFFFu, area, 16)));
  CHECK_FALSE(tiff_ok(one_entry(1, 0xFFFFFFFFu, area, 16)));
  CHECK_FALSE(tiff_ok(one_entry(5, 0x20000001u, area, 16)));  // 8 * count wraps to 8
}

TEST_CASE("tiff_ifd_treats_values_up_to_four_bytes_as_inline") {
  // The 'offset' word of an inline value is data, not a pointer: it must not be
  // range-checked. Boundary: 4 bytes is inline, 5 bytes is an offset.
  { TinyTiff t(true, 1); t.entry(3, 2, 0xFFFFFFFFu); CHECK(tiff_ok(t.finish())); }
  { TinyTiff t(true, 1); t.entry(4, 1, 0xFFFFFFFFu); CHECK(tiff_ok(t.finish())); }
  { TinyTiff t(true, 1); t.entry(1, 4, 0xFFFFFFFFu); CHECK(tiff_ok(t.finish())); }
  { TinyTiff t(true, 1); t.entry(1, 5, 0xFFFFFFFFu); CHECK_FALSE(tiff_ok(t.finish())); }
  { TinyTiff t(true, 1); t.entry(3, 0, 0xFFFFFFFFu); CHECK(tiff_ok(t.finish())); }  // empty
}

TEST_CASE("tiff_ifd_skips_unknown_field_types") {
  // TIFF readers ignore types they do not know; the size is unknowable, so the
  // value cannot be range-checked and is not.
  TinyTiff t(true, 2);
  t.entry(99, 0xFFFFFFFFu, 0xFFFFFFFFu).entry(0, 1, 0);
  CHECK(tiff_ok(t.finish()));
}

TEST_CASE("tiff_ifd_rejects_every_truncation") {
  for (std::size_t n : {std::size_t{1}, std::size_t{3}, std::size_t{12}, std::size_t{40}}) {
    for (bool le : {true, false}) {
      const Bytes tiff = samples::tiff(n, le);
      CHECK(rejects_every_strict_prefix(tiff, [](const std::uint8_t* p, std::size_t len) {
        TiffIfd out{};
        return handwritten::parse_tiff_ifd(p, len, out);
      }));
    }
  }
}

TEST_CASE("tiff_ifd_table_only_variant_skips_value_range_checks") {
  // parse_tiff_ifd<false> checks the header and the entry table but not that
  // out-of-line values lie inside the file. It exists so the benchmark can
  // match a schema that does not follow offsets; it is never the default.
  const Bytes tiff = samples::tiff(12);
  const Bytes cut(tiff.begin(), tiff.end() - 1);  // last value now past the end
  CHECK_FALSE(tiff_ok<true>(cut));
  CHECK(tiff_ok<false>(cut));
  const Bytes table_cut(tiff.begin(), tiff.begin() + 8 + 2 + 12 * 12);  // no next-IFD word
  CHECK_FALSE(tiff_ok<false>(table_cut));
}
