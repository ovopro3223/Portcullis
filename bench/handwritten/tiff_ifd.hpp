// Handwritten, bounds-checked parser for a TIFF header and its first IFD
// (image file directory):
//
//   header:  byte order "II"|"MM", magic 42, offset of the first IFD:U32
//   IFD:     count:U16, count x { tag:U16 type:U16 n:U32 value-or-offset:U32 },
//            next-IFD offset:U32
//
// This is the comparison point for Portcullis in BENCHMARKS.md, so it is a
// correct parser, not a strawman: every read is preceded by an explicit
// `remaining` check, and sizes are computed in 64 bits so a hostile `n` cannot
// wrap (the shape of the libtiff bugs).
//
// What it validates:
//   * the header: a byte order mark, magic 42, an IFD offset of at least 8
//     that leaves room for the entry count,
//   * the entry table: each 12-byte entry, and the 4-byte next-IFD word after
//     the last one, fit in what is left of the file (a count that promises
//     more entries than the file holds is rejected),
//   * with kCheckOutOfLine (the default): for every entry of a known type
//     (TIFF 6.0 types 1-12), if n * sizeof(type) > 4 the value lives out of
//     line and [offset, offset + n * sizeof(type)) must lie inside the file.
//     A value of 4 bytes or fewer is inline; its word is data, not a pointer,
//     and is not range-checked.
// What it does not do:
//   * follow the next-IFD chain (the offset is reported, not followed),
//   * look at tags, or require ascending tag order,
//   * reject an entry of an unknown type: TIFF readers ignore those, and the
//     size of such a value is unknowable, so it cannot be range-checked,
//   * reject count == 0, or check alignment of offsets.
//
// parse_tiff_ifd<false> skips the out-of-line range check. It is for
// benchmarking against a schema that reads the table but does not follow
// offsets; the default (true) is the one that is safe to hand entries to a
// consumer, and the one BENCHMARKS.md should use unless it says otherwise.
#pragma once

#include <cstddef>
#include <cstdint>

#include "bytes.hpp"

namespace handwritten {

struct TiffIfd {
  bool little_endian;
  std::uint16_t entry_count;
  std::size_t entries_offset;    // offset of the first 12-byte entry
  std::uint32_t next_ifd_offset; // 0 if this is the last IFD (not followed)
  std::uint64_t value_bytes;     // sum of n * sizeof(type) over known types
};

// Returns true and fills `out` iff `data[0..size)` has a well-formed TIFF
// header and first IFD. On false, `out` is unspecified.
template <bool kCheckOutOfLine = true>
inline bool parse_tiff_ifd(const std::uint8_t* data, std::size_t size,
                           TiffIfd& out) {
  // Bytes per value for TIFF field types 1..12; index 0 and unknown types are 0.
  static const std::uint8_t kTypeSize[13] = {0, 1, 1, 2, 4, 8, 1, 1, 2, 4, 8, 4, 8};

  std::size_t pos = 0;
  std::size_t remaining = size;

  // Header.
  if (remaining < 8) return false;
  if (data[0] == 'I' && data[1] == 'I') {
    out.little_endian = true;
  } else if (data[0] == 'M' && data[1] == 'M') {
    out.little_endian = false;
  } else {
    return false;
  }
  const bool le = out.little_endian;
  const auto rd16 = [le](const std::uint8_t* p) { return le ? load_le16(p) : load_be16(p); };
  const auto rd32 = [le](const std::uint8_t* p) { return le ? load_le32(p) : load_be32(p); };

  if (rd16(data + 2) != 42) return false;
  const std::uint32_t ifd_offset = rd32(data + 4);
  pos = 8;

  // Seek to the IFD. It cannot overlap the header, and must start inside the file.
  if (ifd_offset < 8) return false;
  if (ifd_offset > size) return false;
  pos = ifd_offset;

  remaining = size - pos;
  if (remaining < 2) return false;
  const std::uint16_t count = rd16(data + pos);
  pos += 2;
  out.entry_count = count;
  out.entries_offset = pos;

  // Each entry is checked before it is read, so a count that promises more
  // entries than the file holds is rejected at the first entry that is not
  // there. The next-IFD word is checked the same way after the last entry.
  out.value_bytes = 0;
  for (std::size_t i = 0; i < count; ++i) {
    remaining = size - pos;
    if (remaining < 12) return false;
    const std::uint8_t* entry = data + pos;
    pos += 12;

    const std::uint16_t type = rd16(entry + 2);
    if (type < 1 || type > 12) continue;  // unknown type: ignored, as readers do

    const std::uint32_t n = rd32(entry + 4);
    const std::uint64_t bytes = static_cast<std::uint64_t>(kTypeSize[type]) * n;
    out.value_bytes += bytes;

    if (kCheckOutOfLine && bytes > 4) {
      const std::uint32_t offset = rd32(entry + 8);
      if (offset > size) return false;
      remaining = size - offset;
      if (remaining < bytes) return false;
    }
  }

  remaining = size - pos;
  if (remaining < 4) return false;
  out.next_ifd_offset = rd32(data + pos);
  pos += 4;

  return true;
}

}  // namespace handwritten
