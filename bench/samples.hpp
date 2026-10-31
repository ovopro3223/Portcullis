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

}  // namespace samples
