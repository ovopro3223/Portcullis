// libFuzzer target for the PNG-style chunked container: a Ref-counted array of
// length-prefixed, kind-tagged chunks. Exercises Array/Bytes<Ref>/Choice paths.
#include <portcullis/fuzz/libfuzzer.hpp>

#include "schemas/png_chunks.hpp"

PORTCULLIS_FUZZ_TARGET(::portcullis::schemas::PngChunks)
