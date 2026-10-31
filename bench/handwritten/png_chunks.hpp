// Handwritten, bounds-checked parser for the PNG container framing: the
// 8-byte signature, then chunks
//
//   length:U32BE  type:4 bytes  data:Bytes<length>  crc:U32BE
//
// until the IEND chunk. This is the comparison point for Portcullis in
// BENCHMARKS.md, so it is a correct parser, not a strawman: every read is
// preceded by an explicit `remaining` check, and `remaining` is compared
// against the 32-bit length without ever forming `pos + length` (which wraps).
//
// What it validates (the Portcullis schema must validate the same, or the
// comparison is not like for like):
//   * the signature,
//   * for every chunk: length + type + data + crc fit in what is left, and the
//     length is within the PNG limit of 2^31 - 1,
//   * the chunk sequence ends with an IEND chunk; a buffer that stops earlier
//     is truncated and is rejected.
// What it deliberately does not validate (the design leaves these to Refine):
//   * the CRC value (it is skipped, not checked),
//   * chunk ordering rules (IHDR first, one IHDR, IDAT contiguity),
//   * the chunk type's letter-case bits, and any chunk's contents,
//   * IEND having length 0.
// Bytes after IEND are ignored, as libpng ignores them; `end_offset` says where
// the PNG ended.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "bytes.hpp"

namespace handwritten {

struct PngChunks {
  std::size_t chunk_count;     // chunks parsed, including IEND
  std::uint64_t data_bytes;    // sum of the chunk data lengths
  std::size_t end_offset;      // offset just past the IEND chunk's CRC
};

// Returns true and fills `out` iff `data[0..size)` starts with a PNG whose
// chunk framing is intact up to and including IEND. On false, `out` is
// unspecified.
inline bool parse_png_chunks(const std::uint8_t* data, std::size_t size,
                             PngChunks& out) {
  static const std::uint8_t kSignature[8] = {0x89, 'P', 'N', 'G',
                                             0x0D, 0x0A, 0x1A, 0x0A};
  static const std::uint8_t kIend[4] = {'I', 'E', 'N', 'D'};

  std::size_t pos = 0;
  std::size_t remaining = size;

  if (remaining < 8) return false;
  if (std::memcmp(data + pos, kSignature, 8) != 0) return false;
  pos += 8;

  out.chunk_count = 0;
  out.data_bytes = 0;

  for (;;) {
    remaining = size - pos;
    if (remaining < 4) return false;
    const std::uint32_t length = load_be32(data + pos);
    pos += 4;
    if (length > 0x7FFFFFFFu) return false;  // PNG caps a chunk at 2^31 - 1

    remaining = size - pos;
    if (remaining < 4) return false;
    const std::uint8_t* type = data + pos;
    pos += 4;

    remaining = size - pos;
    if (remaining < length) return false;  // the lie that every PNG CVE tells
    pos += length;

    remaining = size - pos;
    if (remaining < 4) return false;
    pos += 4;  // crc: skipped, not verified

    out.chunk_count += 1;
    out.data_bytes += length;

    if (std::memcmp(type, kIend, 4) == 0) {
      out.end_offset = pos;
      return true;
    }
  }
}

}  // namespace handwritten
