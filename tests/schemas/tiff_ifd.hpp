#ifndef PORTCULLIS_TESTS_SCHEMAS_TIFF_IFD_HPP
#define PORTCULLIS_TESTS_SCHEMAS_TIFF_IFD_HPP

// A TIFF Image File Directory, modelled on the real format's shape: a 16-bit
// entry count followed by that many directory entries, each a tag/type/length
// triple with a length-prefixed value. It pairs a Ref-counted array (with its
// element cap) with a Refine, so the fuzzer must produce:
//
//   * TooManyElements  — a `count` past the 32-element cap, rejected before any
//     entry is parsed.
//   * Truncated        — an entry whose `len` runs past the remaining bytes.
//   * RefinementFailed — a `magic` value outside the accepted range. The refine
//     is a pure value-range rule the combinators cannot express, so the fuzzer
//     cannot invert it; it generates candidates and lets the contract reject
//     them (design 6, "refinements are not inverted").
//
// Shared by the P6 generator/mutator tests and the tiff_ifd fuzz target.
#include <portcullis/schema/combinators.hpp>
#include <portcullis/schema/partial.hpp>
#include <portcullis/schema/primitives.hpp>
#include <portcullis/schema/ref.hpp>

namespace portcullis::schemas {

using TiffEntry = Struct<Field<"tag", U16LE>, Field<"etype", U16LE>, Field<"len", U16LE>,
                         Field<"value", Bytes<Ref("len")>>>;

using TiffIfd =
    Struct<Field<"magic", U16LE>, Field<"count", U16LE>,
           Field<"entries", Array<TiffEntry, Ref("count"), 32>>,
           Refine<[](const auto& v) { return field<"magic">(v) <= 0x7FFF; }, "magic_in_range">>;

}  // namespace portcullis::schemas

#endif  // PORTCULLIS_TESTS_SCHEMAS_TIFF_IFD_HPP
