// Tests for the handwritten bounds-checked parsers in bench/handwritten/.
//
// These parsers are the honest comparison point for the throughput numbers in
// BENCHMARKS.md, so they must be real parsers: they accept a valid sample and
// reject truncated or lying input. If they were wrong, the comparison would
// be too.
#include "third_party/doctest.h"

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
