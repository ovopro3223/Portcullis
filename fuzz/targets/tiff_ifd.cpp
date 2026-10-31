// libFuzzer target for the TIFF Image File Directory: a Ref-counted array of
// entries plus a value-range Refine. Exercises Array/Bytes<Ref>/Refine paths.
#include <portcullis/fuzz/libfuzzer.hpp>

#include "schemas/tiff_ifd.hpp"

PORTCULLIS_FUZZ_TARGET(::portcullis::schemas::TiffIfd)
