#ifndef PORTCULLIS_TESTS_SCHEMAS_PNG_CHUNKS_HPP
#define PORTCULLIS_TESTS_SCHEMAS_PNG_CHUNKS_HPP

// A PNG-style chunked container, modelled on the real format's shape: a count
// of chunks followed by that many chunks, each a length-prefixed data region
// with a trailing CRC and a kind-tagged auxiliary field. It exercises the three
// structural constructs the fuzzer must reason about that heartbeat does not:
//
//   * Array<Chunk, Ref("count"), 16> — a Ref-counted array with an element cap,
//     so a count past the cap is TooManyElements before anything is allocated.
//   * Bytes<Ref("length")>          — a Ref length, so a declared length past
//     the remaining bytes is Truncated (the Heartbleed relationship, per chunk).
//   * Choice<Tag("kind"), ...>      — a tagged union, so an unknown kind is a
//     BadTag.
//
// Shared by the P6 generator/mutator tests and the png_chunks fuzz target.
#include <portcullis/schema/combinators.hpp>
#include <portcullis/schema/primitives.hpp>
#include <portcullis/schema/ref.hpp>

namespace portcullis::schemas {

// One chunk: kind (the discriminant), a length-prefixed data region, a kind-
// selected auxiliary value, and a CRC. `kind` is declared before `aux`, and
// `length` before `data`, so both references name earlier fields (the rule the
// library enforces at compile time).
using PngChunk = Struct<Field<"kind", U8>, Field<"length", U16BE>,
                        Field<"data", Bytes<Ref("length")>>,
                        Field<"aux", Choice<Tag("kind"), Case<0, U8>, Case<1, U16BE>, Case<2, U32BE>>>,
                        Field<"crc", U32BE>>;

using PngChunks = Struct<Field<"count", U8>, Field<"chunks", Array<PngChunk, Ref("count"), 16>>>;

}  // namespace portcullis::schemas

#endif  // PORTCULLIS_TESTS_SCHEMAS_PNG_CHUNKS_HPP
