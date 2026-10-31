// Tasks 6.1 & 6.2 — Generator<S>::valid() and ::violating().
//
// valid() must always parse; its boundary bias must, over many seeds, reach
// length 0, an array count of 0 and the cap, and every Choice case. violating()
// must always fail to parse, and over many seeds must exhibit every ErrorKind
// the schema can produce. All generation is deterministic per seed.
#include "third_party/doctest.h"

#include <cstddef>
#include <cstdint>
#include <set>
#include <span>
#include <variant>
#include <vector>

#include <portcullis/fuzz/generator.hpp>
#include <portcullis/parse/parse.hpp>

#include "schemas/heartbeat.hpp"
#include "schemas/png_chunks.hpp"
#include "schemas/tiff_ifd.hpp"

using namespace portcullis;
using portcullis::fuzz::Generator;
using portcullis::schemas::Heartbeat;
using portcullis::schemas::PngChunks;
using portcullis::schemas::TiffIfd;

namespace {
Untrusted view(const std::vector<std::byte>& b) {
  return Untrusted(std::span<const std::byte>(b.data(), b.size()));
}
}  // namespace

TEST_CASE("valid_outputs_parse") {
  for (std::uint64_t seed = 0; seed < 1000; ++seed) {
    {
      Generator<Heartbeat> g(seed);
      auto out = g.valid();
      INFO("heartbeat seed ", seed, " size ", out.size());
      CHECK(static_cast<bool>(parse<Heartbeat>(view(out))));
    }
    {
      Generator<PngChunks> g(seed);
      auto out = g.valid();
      INFO("png seed ", seed, " size ", out.size());
      CHECK(static_cast<bool>(parse<PngChunks>(view(out))));
    }
    {
      Generator<TiffIfd> g(seed);
      auto out = g.valid();
      INFO("tiff seed ", seed, " size ", out.size());
      CHECK(static_cast<bool>(parse<TiffIfd>(view(out))));
    }
  }
}

TEST_CASE("deterministic_per_seed") {
  for (std::uint64_t seed : {1u, 7u, 42u, 1000u}) {
    Generator<PngChunks> a(seed), b(seed);
    CHECK(a.valid() == b.valid());
    Generator<PngChunks> c(seed), d(seed);
    CHECK(c.violating() == d.violating());
    // A fresh generator at the same seed reproduces an earlier call's output.
    Generator<TiffIfd> e(seed);
    auto first = e.valid();
    Generator<TiffIfd> f(seed);
    CHECK(f.valid() == first);
  }
}

TEST_CASE("valid_hits_boundaries") {
  bool heartbeat_len_zero = false;
  bool png_count_zero = false, png_count_cap = false;
  std::set<std::size_t> png_cases;  // which Choice alternatives were produced

  for (std::uint64_t seed = 0; seed < 1000; ++seed) {
    {
      Generator<Heartbeat> g(seed);
      auto r = parse<Heartbeat>(view(g.valid()));
      REQUIRE(r);
      if (r->get<"len">() == 0) heartbeat_len_zero = true;
    }
    {
      Generator<PngChunks> g(seed);
      auto r = parse<PngChunks>(view(g.valid()));
      REQUIRE(r);
      const auto& chunks = r->get<"chunks">();
      if (chunks.empty()) png_count_zero = true;
      if (chunks.size() == 16) png_count_cap = true;  // the array's element cap
      for (const auto& ch : chunks) png_cases.insert(ch.template get<"aux">().index());
    }
  }

  CHECK(heartbeat_len_zero);
  CHECK(png_count_zero);
  CHECK(png_count_cap);
  // Every Choice case (U8, U16BE, U32BE bodies) appears at least once.
  CHECK(png_cases.count(0) == 1);
  CHECK(png_cases.count(1) == 1);
  CHECK(png_cases.count(2) == 1);
}

TEST_CASE("violating_outputs_fail") {
  for (std::uint64_t seed = 0; seed < 1000; ++seed) {
    {
      Generator<Heartbeat> g(seed);
      auto out = g.violating();
      INFO("heartbeat seed ", seed, " size ", out.size());
      CHECK_FALSE(static_cast<bool>(parse<Heartbeat>(view(out))));
    }
    {
      Generator<PngChunks> g(seed);
      auto out = g.violating();
      INFO("png seed ", seed, " size ", out.size());
      CHECK_FALSE(static_cast<bool>(parse<PngChunks>(view(out))));
    }
    {
      Generator<TiffIfd> g(seed);
      auto out = g.violating();
      INFO("tiff seed ", seed, " size ", out.size());
      CHECK_FALSE(static_cast<bool>(parse<TiffIfd>(view(out))));
    }
  }
}

TEST_CASE("violations_cover_each_kind") {
  std::set<ErrorKind> heartbeat_kinds, png_kinds, tiff_kinds;
  for (std::uint64_t seed = 0; seed < 1000; ++seed) {
    {
      Generator<Heartbeat> g(seed);
      auto r = parse<Heartbeat>(view(g.violating()));
      REQUIRE(!r);
      heartbeat_kinds.insert(r.error().kind);
    }
    {
      Generator<PngChunks> g(seed);
      auto r = parse<PngChunks>(view(g.violating()));
      REQUIRE(!r);
      png_kinds.insert(r.error().kind);
    }
    {
      Generator<TiffIfd> g(seed);
      auto r = parse<TiffIfd>(view(g.violating()));
      REQUIRE(!r);
      tiff_kinds.insert(r.error().kind);
    }
  }

  // heartbeat has only a Ref length -> Truncated is the only kind it can show.
  CHECK(heartbeat_kinds.count(ErrorKind::Truncated) == 1);
  // png: a Ref length (Truncated), a Choice (BadTag), a Ref-counted array (TooMany).
  CHECK(png_kinds.count(ErrorKind::Truncated) == 1);
  CHECK(png_kinds.count(ErrorKind::BadTag) == 1);
  CHECK(png_kinds.count(ErrorKind::TooManyElements) == 1);
  // tiff: a Ref length (Truncated), a Ref-counted array (TooMany), a Refine.
  CHECK(tiff_kinds.count(ErrorKind::Truncated) == 1);
  CHECK(tiff_kinds.count(ErrorKind::TooManyElements) == 1);
  CHECK(tiff_kinds.count(ErrorKind::RefinementFailed) == 1);
}
