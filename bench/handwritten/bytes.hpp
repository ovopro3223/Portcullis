// Unchecked integer loads for the handwritten parsers.
//
// These do NOT check bounds. The contract is the same one every handwritten
// parser in this directory follows: the caller has already established, with
// an explicit `if (remaining < n) return false;`, that n bytes are readable at
// `p`. Nothing else in the handwritten parsers reads input bytes.
#pragma once

#include <cstdint>

namespace handwritten {

inline std::uint16_t load_be16(const std::uint8_t* p) {
  return static_cast<std::uint16_t>((std::uint16_t{p[0]} << 8) | p[1]);
}
inline std::uint32_t load_be32(const std::uint8_t* p) {
  return (std::uint32_t{p[0]} << 24) | (std::uint32_t{p[1]} << 16) |
         (std::uint32_t{p[2]} << 8) | std::uint32_t{p[3]};
}
inline std::uint16_t load_le16(const std::uint8_t* p) {
  return static_cast<std::uint16_t>((std::uint16_t{p[1]} << 8) | p[0]);
}
inline std::uint32_t load_le32(const std::uint8_t* p) {
  return (std::uint32_t{p[3]} << 24) | (std::uint32_t{p[2]} << 16) |
         (std::uint32_t{p[1]} << 8) | std::uint32_t{p[0]};
}

}  // namespace handwritten
