#ifndef PORTCULLIS_SCHEMA_PRIMITIVES_HPP
#define PORTCULLIS_SCHEMA_PRIMITIVES_HPP

#include <cstddef>
#include <cstdint>

#include "portcullis/core/endian.hpp"

namespace portcullis {

// Fixed-width integer primitives with explicit endianness. Each carries the
// three facts the parser and the fuzzer need: the C++ value it produces, how
// many bytes it reads, and in which byte order. A single-byte type names Big
// so the shape is uniform; endianness is a no-op at width 1.
template <class T>
concept Primitive = requires {
  typename T::value_type;
  { T::width } -> std::convertible_to<std::size_t>;
  { T::endian } -> std::convertible_to<Endian>;
};

#define PORTCULLIS_PRIMITIVE(Name, ValueType, Width, End)            \
  struct Name {                                                      \
    using value_type = ValueType;                                    \
    static constexpr std::size_t width = Width;                      \
    static constexpr Endian endian = End;                            \
  }

PORTCULLIS_PRIMITIVE(U8,    std::uint8_t,  1, Endian::Big);
PORTCULLIS_PRIMITIVE(U16BE, std::uint16_t, 2, Endian::Big);
PORTCULLIS_PRIMITIVE(U16LE, std::uint16_t, 2, Endian::Little);
PORTCULLIS_PRIMITIVE(U32BE, std::uint32_t, 4, Endian::Big);
PORTCULLIS_PRIMITIVE(U32LE, std::uint32_t, 4, Endian::Little);
PORTCULLIS_PRIMITIVE(U64BE, std::uint64_t, 8, Endian::Big);
PORTCULLIS_PRIMITIVE(U64LE, std::uint64_t, 8, Endian::Little);
PORTCULLIS_PRIMITIVE(I8,    std::int8_t,   1, Endian::Big);
PORTCULLIS_PRIMITIVE(I16BE, std::int16_t,  2, Endian::Big);
PORTCULLIS_PRIMITIVE(I16LE, std::int16_t,  2, Endian::Little);
PORTCULLIS_PRIMITIVE(I32BE, std::int32_t,  4, Endian::Big);
PORTCULLIS_PRIMITIVE(I32LE, std::int32_t,  4, Endian::Little);
PORTCULLIS_PRIMITIVE(I64BE, std::int64_t,  8, Endian::Big);
PORTCULLIS_PRIMITIVE(I64LE, std::int64_t,  8, Endian::Little);

#undef PORTCULLIS_PRIMITIVE

}  // namespace portcullis

#endif  // PORTCULLIS_SCHEMA_PRIMITIVES_HPP
