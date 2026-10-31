// Deterministic sample inputs shared by the handwritten-parser tests and the
// benchmark runner. The same bytes feed every parser that is measured, so a
// Portcullis row and a handwritten row always parse identical input.
//
// Everything here is plain std::vector<uint8_t> construction. No parsing.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace samples {

using Bytes = std::vector<std::uint8_t>;

inline void put_u8(Bytes& b, std::uint8_t v) { b.push_back(v); }

inline void put_be16(Bytes& b, std::uint16_t v) {
  b.push_back(static_cast<std::uint8_t>(v >> 8));
  b.push_back(static_cast<std::uint8_t>(v));
}
inline void put_be32(Bytes& b, std::uint32_t v) {
  b.push_back(static_cast<std::uint8_t>(v >> 24));
  b.push_back(static_cast<std::uint8_t>(v >> 16));
  b.push_back(static_cast<std::uint8_t>(v >> 8));
  b.push_back(static_cast<std::uint8_t>(v));
}
inline void put_le16(Bytes& b, std::uint16_t v) {
  b.push_back(static_cast<std::uint8_t>(v));
  b.push_back(static_cast<std::uint8_t>(v >> 8));
}
inline void put_le32(Bytes& b, std::uint32_t v) {
  b.push_back(static_cast<std::uint8_t>(v));
  b.push_back(static_cast<std::uint8_t>(v >> 8));
  b.push_back(static_cast<std::uint8_t>(v >> 16));
  b.push_back(static_cast<std::uint8_t>(v >> 24));
}

// Non-constant filler so no byte run is trivially compressible or zero.
inline void put_filler(Bytes& b, std::size_t n, std::uint8_t seed) {
  for (std::size_t i = 0; i < n; ++i) {
    b.push_back(static_cast<std::uint8_t>(seed + i * 31u + (i >> 8) * 7u));
  }
}

// ------------------------------------------------------------------ TLS ----
//
// Heartbeat record as in the design (spec 5.2, "the Heartbleed record"):
//   type:U8, len:U16BE, payload:Bytes<len>, padding:Bytes<16>
// payload_len is clamped by the caller to 0..65535.
inline Bytes tls_heartbeat(std::size_t payload_len, std::uint8_t type = 1) {
  Bytes b;
  b.reserve(3 + payload_len + 16);
  put_u8(b, type);
  put_be16(b, static_cast<std::uint16_t>(payload_len));
  put_filler(b, payload_len, 0x41);
  put_filler(b, 16, 0xA0);
  return b;
}

}  // namespace samples
