// Task 1.4 — parse of primitives, Bytes<N>, flat Struct; Parsed; value_of_t.
#include "third_party/doctest.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <vector>

#include <portcullis/parse/parse.hpp>
#include <portcullis/schema/combinators.hpp>
#include <portcullis/schema/primitives.hpp>

using namespace portcullis;

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

using Header = Struct<Field<"a", U8>, Field<"b", U32BE>, Field<"c", Bytes<3>>>;
}  // namespace

TEST_CASE("value_of_t_maps_types") {
  static_assert(std::same_as<value_of_t<U8>, std::uint8_t>);
  static_assert(std::same_as<value_of_t<U32BE>, std::uint32_t>);
  static_assert(std::same_as<value_of_t<Bytes<3>>, std::span<const std::byte>>);
  static_assert(std::same_as<value_of_t<Header>, Parsed<Header>>);
  CHECK(true);
}

TEST_CASE("parses_fixed_header") {
  // Plan's prose input "01 00 02 03 04 05 06 07" is inconsistent with its own
  // asserted b == 0x02030405 (U8 then U32BE over that input gives 0x00020304).
  // We honor the asserted value and use input consistent with it.
  auto b = bytes({0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08});
  auto r = parse<Header>(view(b));
  REQUIRE(r);
  CHECK(r->get<"a">() == 1);
  CHECK(r->get<"b">() == 0x02030405u);
  CHECK(r->get<"c">().size() == 3);
  CHECK(r->bytes().size() == 8);
}

TEST_CASE("empty_input_is_truncated_at_offset_zero") {
  auto b = bytes({});
  auto r = parse<Header>(view(b));
  REQUIRE(!r);
  CHECK(r.error().kind == ErrorKind::Truncated);
  CHECK(r.error().offset == 0);
  CHECK(r.error().need == 8);
  CHECK(r.error().have == 0);
  CHECK(r.error().path == "a");
}

TEST_CASE("trailing_bytes_are_allowed") {
  auto b = bytes({0x01, 0x00, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0xFF});
  auto r = parse<Header>(view(b));
  REQUIRE(r);
  CHECK(r->bytes().size() == 8);
}

TEST_CASE("signed_primitive_round_trips") {
  using S = Struct<Field<"n", I16BE>>;
  auto b = bytes({0xFF, 0xFE});
  auto r = parse<S>(view(b));
  REQUIRE(r);
  CHECK(r->get<"n">() == -2);
  static_assert(std::same_as<value_of_t<I16BE>, std::int16_t>);
}
