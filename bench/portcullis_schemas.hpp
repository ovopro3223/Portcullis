// Portcullis schemas for the three benchmark formats, and a parse callable per
// format with the `bool(const uint8_t*, size_t)` shape the runner expects. Each
// is written to validate the SAME structure as its handwritten counterpart in
// `handwritten/`, so BENCHMARKS.md compares like for like:
//
//   * tls_heartbeat — a full, exact match (the design's worked example).
//   * png_chunks    — PNG's chunk stream is terminated by IEND, not counted, so
//                     a consumer parses one chunk schema at a time in a loop
//                     (as any real Portcullis consumer would); the per-chunk
//                     length check is the Heartbleed-class check.
//   * tiff_ifd      — the IFD table only (header, count, entries, next-IFD), the
//                     little-endian case with the IFD at offset 8. Portcullis v1
//                     is linear and does not follow out-of-line value offsets, so
//                     the honest handwritten baseline is parse_tiff_ifd<false>.
//                     (Offset/seek support and runtime endianness are roadmap.)
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

#include <portcullis/portcullis.hpp>

namespace portcullis_bench {

namespace pc = portcullis;

inline pc::Untrusted as_untrusted(const std::uint8_t* p, std::size_t len) {
  return pc::Untrusted(std::span<const std::byte>(reinterpret_cast<const std::byte*>(p), len));
}

// ---- tls_heartbeat -------------------------------------------------- exact
using Heartbeat = pc::Struct<
    pc::Field<"type", pc::U8>,
    pc::Field<"len", pc::U16BE>,
    pc::Field<"payload", pc::Bytes<pc::Ref("len")>>,
    pc::Field<"padding", pc::Bytes<16>>>;

inline bool parse_tls_heartbeat(const std::uint8_t* p, std::size_t len) {
  return static_cast<bool>(pc::parse<Heartbeat>(as_untrusted(p, len)));
}

// ---- png_chunks ----------------------------------- one chunk, parsed in a loop
using PngChunk = pc::Struct<
    pc::Field<"length", pc::U32BE>,
    pc::Field<"ctype", pc::Bytes<4>>,
    pc::Field<"data", pc::Bytes<pc::Ref("length")>>,
    pc::Field<"crc", pc::U32BE>>;

inline bool parse_png_chunks(const std::uint8_t* p, std::size_t len) {
  static const std::uint8_t kSig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  if (len < 8 || std::memcmp(p, kSig, 8) != 0) return false;
  std::size_t off = 8;
  for (;;) {
    auto r = pc::parse<PngChunk>(as_untrusted(p + off, len - off));
    if (!r) return false;
    const auto type = r->get<"ctype">();  // 4-byte span
    off += r->bytes().size();             // 12 + length
    if (std::memcmp(type.data(), "IEND", 4) == 0) return true;
  }
}

// ---- tiff_ifd ------------------------------------- IFD table, little-endian
using IfdEntry = pc::Struct<
    pc::Field<"tag", pc::U16LE>,
    pc::Field<"ftype", pc::U16LE>,
    pc::Field<"n", pc::U32LE>,
    pc::Field<"value", pc::U32LE>>;

// Header is "II", magic 42, IFD offset 8 (the IFD immediately follows), then
// count, entries, next-IFD. Matches the little-endian sample; see the file note.
using TiffIfd = pc::Struct<
    pc::Field<"bom", pc::Bytes<2>>,
    pc::Field<"magic", pc::U16LE>,
    pc::Field<"ifd_offset", pc::U32LE>,
    pc::Field<"count", pc::U16LE>,
    pc::Field<"entries", pc::Array<IfdEntry, pc::Ref("count"), 65535>>,
    pc::Field<"next_ifd", pc::U32LE>>;

inline bool parse_tiff_ifd(const std::uint8_t* p, std::size_t len) {
  return static_cast<bool>(pc::parse<TiffIfd>(as_untrusted(p, len)));
}

}  // namespace portcullis_bench
