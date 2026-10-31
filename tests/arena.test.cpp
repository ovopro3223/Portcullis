// Task 8.1 — portable bump arena + materialize (runs everywhere, incl. x86-64).
#include "third_party/doctest.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include <portcullis/arena/tagged_arena.hpp>
#include <portcullis/parse/parse.hpp>
#include <portcullis/schema/combinators.hpp>
#include <portcullis/schema/primitives.hpp>

using namespace portcullis;
using portcullis::arena::TaggedArena;
using portcullis::arena::materialize;

namespace {
std::vector<std::byte> bytes(std::initializer_list<int> vs) {
  std::vector<std::byte> out;
  out.reserve(vs.size());
  for (int v : vs) out.push_back(static_cast<std::byte>(v));
  return out;
}
Untrusted view(const std::vector<std::byte>& b) {
  return Untrusted(std::span<const std::byte>(b.data(), b.size()));
}

bool is_aligned(const void* p, std::size_t a) {
  return (reinterpret_cast<std::uintptr_t>(p) & (a - 1)) == 0;
}

using Header = Struct<Field<"a", U8>, Field<"b", U32BE>, Field<"c", Bytes<3>>>;
}  // namespace

TEST_CASE("allocate_returns_aligned_memory") {
  TaggedArena a(64 * 1024);
  for (std::size_t align : {std::size_t{1}, std::size_t{2}, std::size_t{4}, std::size_t{8},
                            std::size_t{16}, std::size_t{32}, std::size_t{64}}) {
    void* p = a.allocate(40, align);
    REQUIRE(p != nullptr);
    CHECK(is_aligned(p, align));
    // The returned region is writable for its whole requested length.
    std::memset(p, 0xAB, 40);
  }
}

TEST_CASE("exhaustion_returns_nullptr") {
  TaggedArena a(64);
  void* first = a.allocate(32, 16);
  REQUIRE(first != nullptr);
  // No room left for another 64 bytes: must return nullptr, never crash or UB.
  void* second = a.allocate(64, 16);
  CHECK(second == nullptr);
  // A single request larger than the whole arena also fails cleanly.
  void* huge = a.allocate(1u << 20, 16);
  CHECK(huge == nullptr);
  // The first allocation is still valid and usable after failed requests.
  std::memset(first, 0x5A, 32);
}

TEST_CASE("materialize_copies_spans_out_of_the_input") {
  auto input = bytes({0x11, 0x22, 0x33, 0x44, 0x55, 0xAA, 0xBB, 0xCC});
  const std::byte* input_begin = input.data();
  const std::byte* input_end = input.data() + input.size();

  auto r = parse<Header>(view(input));
  REQUIRE(r);

  TaggedArena arena(64 * 1024);
  Parsed<Header>* m = materialize(*r, arena);
  REQUIRE(m != nullptr);

  auto in_input = [&](const void* q) {
    auto x = reinterpret_cast<std::uintptr_t>(q);
    return x >= reinterpret_cast<std::uintptr_t>(input_begin) &&
           x < reinterpret_cast<std::uintptr_t>(input_end);
  };

  // Everything the materialized copy exposes must live outside the input buffer.
  auto c = m->get<"c">();
  CHECK(!in_input(c.data()));
  CHECK(!in_input(m->bytes().data()));

  // Destroy the original bytes: overwrite, then free the storage entirely.
  std::fill(input.begin(), input.end(), std::byte{0x00});
  input = std::vector<std::byte>{};

  // Integers were copied by value.
  CHECK(m->get<"a">() == 0x11);
  CHECK(m->get<"b">() == 0x22334455u);
  // The span still reads the ORIGINAL bytes, not the overwritten/freed input.
  REQUIRE(c.size() == 3);
  CHECK(c[0] == std::byte{0xAA});
  CHECK(c[1] == std::byte{0xBB});
  CHECK(c[2] == std::byte{0xCC});
  // The top-level byte view survived too.
  REQUIRE(m->bytes().size() == 8);
  CHECK(m->bytes()[0] == std::byte{0x11});
  CHECK(m->bytes()[7] == std::byte{0xCC});
}

TEST_CASE("tagging_inactive_without_mte") {
  TaggedArena a(1024);
  if (!TaggedArena::mte_available()) {
    // On x86-64 (and any host without MTE) tagging is never active and the
    // arena still works as a plain bump allocator.
    CHECK(TaggedArena::mte_available() == false);
    CHECK(a.tagging_active() == false);
    void* p = a.allocate(16, 16);
    CHECK(p != nullptr);
  } else {
    // On MTE hardware the portable guarantees still hold.
    CHECK(a.tagging_active() == true);
  }
}
