// Task 6.3 — mutate<S>(). Built under the clang-asan preset in CI: the size
// discipline (Review Focus 5) and the in-place edits are what ASan verifies.
//
//   mutate_respects_max_size   10k iterations over random and generated inputs,
//                              across all three schemas: the return is never
//                              above max_size and nothing reads/writes OOB.
//   field_level_ops_are_reachable  from a valid input, the length-change, tag-
//                              swap and truncate-at-boundary ops all occur.
//   unparseable_input_still_mutates  a non-parsing input is still changed.
#include "third_party/doctest.h"

#include <cstddef>
#include <cstdint>
#include <set>
#include <span>
#include <vector>

#include <portcullis/fuzz/generator.hpp>
#include <portcullis/fuzz/mutator.hpp>
#include <portcullis/parse/parse.hpp>

#include "schemas/heartbeat.hpp"
#include "schemas/png_chunks.hpp"
#include "schemas/tiff_ifd.hpp"

using namespace portcullis;
using portcullis::fuzz::Generator;
using portcullis::fuzz::last_op;
using portcullis::fuzz::MutOp;
using portcullis::fuzz::mutate;
using portcullis::schemas::Heartbeat;
using portcullis::schemas::PngChunks;
using portcullis::schemas::TiffIfd;

namespace {

Untrusted view(const std::byte* d, std::size_t n) {
  return Untrusted(std::span<const std::byte>(d, n));
}

// One bounded mutation of a random-or-generated buffer, asserting the result
// stays within max_size. The buffer has exactly max_size bytes of capacity, so
// any out-of-bounds write (e.g. a mis-sized element duplication) trips ASan.
template <class S>
void one_iter(std::uint64_t i) {
  fuzz::Rng rng{fuzz::mix(i, 0x5151u)};
  std::size_t max_size = 1 + static_cast<std::size_t>(rng.below(256));
  std::vector<std::byte> buf(max_size);

  std::size_t size;
  if (i % 2 == 0) {
    size = static_cast<std::size_t>(rng.below(max_size + 1));
    for (std::size_t k = 0; k < size; ++k) buf[k] = static_cast<std::byte>(rng.next() & 0xFF);
  } else {
    Generator<S> g(i);
    auto v = g.valid();
    if (v.size() <= max_size) {
      for (std::size_t k = 0; k < v.size(); ++k) buf[k] = v[k];
      size = v.size();
    } else {
      size = static_cast<std::size_t>(rng.below(max_size + 1));
      for (std::size_t k = 0; k < size; ++k) buf[k] = static_cast<std::byte>(rng.next() & 0xFF);
    }
  }

  std::size_t out = mutate<S>(std::span<std::byte>(buf.data(), buf.size()), size, max_size,
                              static_cast<std::uint32_t>(i));
  REQUIRE(out <= max_size);
}

}  // namespace

TEST_CASE("mutate_respects_max_size") {
  for (std::uint64_t i = 0; i < 10000; ++i) {
    switch (i % 3) {
      case 0: one_iter<Heartbeat>(i); break;
      case 1: one_iter<PngChunks>(i); break;
      default: one_iter<TiffIfd>(i); break;
    }
  }
}

TEST_CASE("field_level_ops_are_reachable") {
  // A valid PNG container with at least one chunk, so a length field, a Choice
  // tag and interior field boundaries all exist to be mutated.
  std::vector<std::byte> valid;
  for (std::uint64_t seed = 0; seed < 2000; ++seed) {
    Generator<PngChunks> g(seed);
    auto v = g.valid();
    auto r = parse<PngChunks>(view(v.data(), v.size()));
    if (r && !r->get<"chunks">().empty()) {
      valid = std::move(v);
      break;
    }
  }
  REQUIRE_FALSE(valid.empty());

  const std::size_t max_size = valid.size() + 64;
  std::set<MutOp> seen;
  for (std::uint64_t seed = 0; seed < 1000; ++seed) {
    std::vector<std::byte> buf(max_size);
    for (std::size_t k = 0; k < valid.size(); ++k) buf[k] = valid[k];  // restore each time
    std::size_t out = mutate<PngChunks>(std::span<std::byte>(buf.data(), buf.size()), valid.size(),
                                        max_size, static_cast<std::uint32_t>(seed));
    REQUIRE(out <= max_size);
    seen.insert(last_op);
    if (last_op == MutOp::Truncate) CHECK(out < valid.size());
  }

  CHECK(seen.count(MutOp::LenChange) == 1);
  CHECK(seen.count(MutOp::TagSwap) == 1);
  CHECK(seen.count(MutOp::Truncate) == 1);
}

TEST_CASE("unparseable_input_still_mutates") {
  // Declares a 0xFFFF payload with one byte present: it never parses and never
  // scans cleanly, so the byte-flip fallback must still change it.
  const std::vector<std::byte> seed_bytes = {std::byte{0x01}, std::byte{0xFF}, std::byte{0xFF},
                                             std::byte{0x41}};
  REQUIRE_FALSE(static_cast<bool>(parse<Heartbeat>(view(seed_bytes.data(), seed_bytes.size()))));

  bool any_changed = false;
  for (std::uint64_t seed = 0; seed < 200; ++seed) {
    std::vector<std::byte> buf = seed_bytes;
    std::size_t out = mutate<Heartbeat>(std::span<std::byte>(buf.data(), buf.size()),
                                        buf.size(), buf.size(), static_cast<std::uint32_t>(seed));
    CHECK(out > 0);
    REQUIRE(out <= seed_bytes.size());
    if (buf != seed_bytes) any_changed = true;
  }
  CHECK(any_changed);
}
