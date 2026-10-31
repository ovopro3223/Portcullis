// Handwritten, bounds-checked parser for the TLS heartbeat record of the
// design's worked example (spec 5.2):
//
//   type:U8  len:U16BE  payload:Bytes<len>  padding:Bytes<16>
//
// This is the comparison point for Portcullis in BENCHMARKS.md, so it is a
// correct parser written the way a careful C++ programmer writes one, not a
// strawman: every read is preceded by an explicit `remaining` check.
//
// Validates exactly what the Portcullis `Heartbeat` schema validates:
//   * the fixed header is present,
//   * the declared payload length fits in what is left (the Heartbleed check),
//   * 16 bytes of padding follow the payload.
// It does not constrain the `type` byte (the schema has no Refine on it) and
// ignores bytes after the padding. RFC 6520 lets the padding run to the end of
// the record (>= 16 bytes); the design fixes it at 16, and this follows the
// design so both sides do the same work.
#pragma once

#include <cstddef>
#include <cstdint>

#include "bytes.hpp"

namespace handwritten {

struct TlsHeartbeat {
  std::uint8_t type;
  std::uint16_t payload_length;
  const std::uint8_t* payload;  // payload_length bytes, inside the input
  const std::uint8_t* padding;  // 16 bytes, inside the input
};

// Returns true and fills `out` iff `data[0..size)` is a well-formed record.
// On false, `out` is unspecified.
inline bool parse_tls_heartbeat(const std::uint8_t* data, std::size_t size,
                                TlsHeartbeat& out) {
  std::size_t pos = 0;
  std::size_t remaining = size;

  if (remaining < 1) return false;
  out.type = data[pos];
  pos += 1;
  remaining = size - pos;

  if (remaining < 2) return false;
  out.payload_length = load_be16(data + pos);
  pos += 2;
  remaining = size - pos;

  if (remaining < out.payload_length) return false;
  out.payload = data + pos;
  pos += out.payload_length;
  remaining = size - pos;

  if (remaining < 16) return false;
  out.padding = data + pos;
  pos += 16;

  return true;
}

}  // namespace handwritten
