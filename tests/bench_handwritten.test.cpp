// Tests for the handwritten bounds-checked parsers in bench/handwritten/.
//
// These parsers are the honest comparison point for the throughput numbers in
// BENCHMARKS.md, so they must be real parsers: they accept a valid sample and
// reject truncated or lying input. If they were wrong, the comparison would
// be too.
#include "third_party/doctest.h"

#include "../bench/handwritten/png_chunks.hpp"
#include "../bench/handwritten/tls_heartbeat.hpp"
#include "../bench/samples.hpp"

#include <cstddef>
#include <cstdint>

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
