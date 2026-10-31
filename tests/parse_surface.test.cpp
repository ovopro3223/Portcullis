// Task 1.11 — the public surface is reachable through the umbrella header alone.
#include "third_party/doctest.h"

#include <cstddef>
#include <span>
#include <type_traits>
#include <vector>

#include <portcullis/portcullis.hpp>

using namespace portcullis;

TEST_CASE("umbrella_exposes_parse_and_schema") {
  using S = Struct<Field<"a", U8>, Field<"b", U16BE>>;
  static_assert(field_count_v<S> == 2);
  static_assert(is_fixed_v<S>);
  static_assert(fixed_size_v<S> == 3);

  std::vector<std::byte> b{std::byte{0x01}, std::byte{0x00}, std::byte{0x02}};
  Untrusted u(std::span<const std::byte>(b.data(), b.size()));
  auto r = parse<S>(u);
  REQUIRE(r);
  CHECK(r->get<"a">() == 1);
  CHECK(r->get<"b">() == 2);
  static_assert(std::same_as<value_of_t<U8>, std::uint8_t>);
}
