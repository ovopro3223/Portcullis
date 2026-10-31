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

// ------------------------------------------------------------------ PNG ----

inline std::uint32_t crc32(const std::uint8_t* p, std::size_t n) {
  static const auto table = [] {
    struct T { std::uint32_t v[256]; } t{};
    for (std::uint32_t i = 0; i < 256; ++i) {
      std::uint32_t c = i;
      for (int k = 0; k < 8; ++k) c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      t.v[i] = c;
    }
    return t;
  }();
  std::uint32_t c = 0xFFFFFFFFu;
  for (std::size_t i = 0; i < n; ++i) c = table.v[(c ^ p[i]) & 0xFFu] ^ (c >> 8);
  return c ^ 0xFFFFFFFFu;
}

inline void put_png_signature(Bytes& b) {
  static const std::uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  b.insert(b.end(), sig, sig + 8);
}

// One chunk: length:U32BE, type:4 bytes, data, crc:U32BE (over type + data).
inline void put_png_chunk(Bytes& b, const char (&type)[5], const Bytes& data) {
  put_be32(b, static_cast<std::uint32_t>(data.size()));
  const std::size_t crc_from = b.size();
  b.insert(b.end(), type, type + 4);
  b.insert(b.end(), data.begin(), data.end());
  put_be32(b, crc32(b.data() + crc_from, b.size() - crc_from));
}

// signature, then IHDR, gAMA, pHYs, tEXt, `idat_chunks` x IDAT of `idat_size`
// bytes, IEND: 5 + idat_chunks chunks. Chunk framing and CRCs are valid; the
// IDAT bytes are filler, not a deflate stream (nothing here decodes pixels).
inline Bytes png(std::size_t idat_chunks, std::size_t idat_size) {
  Bytes b;
  b.reserve(8 + 25 + 16 + 21 + 43 + idat_chunks * (12 + idat_size) + 12);
  put_png_signature(b);

  Bytes ihdr;  // 640x480, 8-bit RGB
  put_be32(ihdr, 640);
  put_be32(ihdr, 480);
  put_u8(ihdr, 8);   // bit depth
  put_u8(ihdr, 2);   // colour type: truecolour
  put_u8(ihdr, 0);   // compression
  put_u8(ihdr, 0);   // filter
  put_u8(ihdr, 0);   // interlace
  put_png_chunk(b, "IHDR", ihdr);

  Bytes gama;
  put_be32(gama, 45455);
  put_png_chunk(b, "gAMA", gama);

  Bytes phys;
  put_be32(phys, 2835);
  put_be32(phys, 2835);
  put_u8(phys, 1);  // metres
  put_png_chunk(b, "pHYs", phys);

  Bytes text;
  const char kw[] = "Comment";
  text.insert(text.end(), kw, kw + 8);  // keyword + NUL
  const char val[] = "portcullis bench sample";
  text.insert(text.end(), val, val + sizeof(val) - 1);
  put_png_chunk(b, "tEXt", text);

  for (std::size_t i = 0; i < idat_chunks; ++i) {
    Bytes idat;
    idat.reserve(idat_size);
    put_filler(idat, idat_size, static_cast<std::uint8_t>(i));
    put_png_chunk(b, "IDAT", idat);
  }

  put_png_chunk(b, "IEND", Bytes{});
  return b;
}

// ----------------------------------------------------------------- TIFF ----

// Byte size of one value of a TIFF field type (TIFF 6.0 types 1-12); 0 for a
// type this builder does not know.
inline std::uint32_t tiff_type_size(std::uint16_t type) {
  static const std::uint8_t sizes[13] = {0, 1, 1, 2, 4, 8, 1, 1, 2, 4, 8, 4, 8};
  return type < 13 ? sizes[type] : 0u;
}

inline void put_tiff_u16(Bytes& b, bool le, std::uint16_t v) {
  if (le) put_le16(b, v); else put_be16(b, v);
}
inline void put_tiff_u32(Bytes& b, bool le, std::uint32_t v) {
  if (le) put_le32(b, v); else put_be32(b, v);
}

// 8-byte header: byte order mark, magic 42, offset of the first IFD.
inline void put_tiff_header(Bytes& b, bool le, std::uint32_t ifd_offset) {
  put_u8(b, le ? 'I' : 'M');
  put_u8(b, le ? 'I' : 'M');
  put_tiff_u16(b, le, 42);
  put_tiff_u32(b, le, ifd_offset);
}

// One 12-byte IFD entry. `value` is written as a 32-bit word in file byte
// order: an out-of-line offset, or the raw inline bytes (a left-justified
// inline SHORT is `v` in little-endian files and `v << 16` in big-endian).
inline void put_tiff_entry(Bytes& b, bool le, std::uint16_t tag, std::uint16_t type,
                           std::uint32_t count, std::uint32_t value) {
  put_tiff_u16(b, le, tag);
  put_tiff_u16(b, le, type);
  put_tiff_u32(b, le, count);
  put_tiff_u32(b, le, value);
}

// A TIFF: header, one IFD at offset 8 with `entry_count` entries (ascending
// tags starting at 256, next-IFD offset 0), then the out-of-line value area.
// Entries cycle through ten shapes (inline and out-of-line, BYTE to DOUBLE).
// The last entry is always out-of-line, so the file ends exactly where its
// last referenced value ends: every strict prefix has an entry whose value
// lies past the end.
inline Bytes tiff(std::size_t entry_count, bool little_endian = true) {
  struct Shape { std::uint16_t type; std::uint32_t count; };
  static const Shape shapes[] = {
      {3, 1},   // SHORT x1      inline
      {4, 1},   // LONG x1       inline
      {3, 3},   // SHORT x3      6 bytes out of line
      {3, 1},   // SHORT x1      inline
      {1, 4},   // BYTE x4       inline (exactly 4 bytes)
      {2, 24},  // ASCII x24     out of line
      {5, 1},   // RATIONAL x1   8 bytes out of line
      {4, 4},   // LONG x4       16 bytes out of line
      {3, 2},   // SHORT x2      inline (exactly 4 bytes)
      {12, 1},  // DOUBLE x1     8 bytes out of line
  };
  const bool le = little_endian;
  const std::uint32_t table_end = static_cast<std::uint32_t>(8 + 2 + 12 * entry_count + 4);

  Bytes b;
  put_tiff_header(b, le, 8);
  put_tiff_u16(b, le, static_cast<std::uint16_t>(entry_count));

  Bytes values;
  for (std::size_t i = 0; i < entry_count; ++i) {
    Shape s = shapes[i % (sizeof(shapes) / sizeof(shapes[0]))];
    if (i + 1 == entry_count) s = {2, 16};  // last entry: ASCII x16, out of line
    const std::uint32_t bytes = tiff_type_size(s.type) * s.count;
    std::uint32_t value;
    if (bytes > 4) {
      if (values.size() & 1u) values.push_back(0);  // values start on even offsets
      value = table_end + static_cast<std::uint32_t>(values.size());
      put_filler(values, bytes, static_cast<std::uint8_t>(i));
    } else if (s.type == 3 && s.count == 1) {
      value = le ? 640u : (640u << 16);
    } else {
      value = 0x01020304u;
    }
    put_tiff_entry(b, le, static_cast<std::uint16_t>(256 + i), s.type, s.count, value);
  }
  put_tiff_u32(b, le, 0);  // next IFD: none
  b.insert(b.end(), values.begin(), values.end());
  return b;
}

}  // namespace samples
