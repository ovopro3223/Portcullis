// Task 1.2 — for_each_field, FieldInfo, field_index_v, field_type_t.
#include "third_party/doctest.h"

#include <string>
#include <type_traits>
#include <vector>

#include <portcullis/schema/combinators.hpp>
#include <portcullis/schema/primitives.hpp>
#include <portcullis/visit/for_each_field.hpp>

using namespace portcullis;

namespace {
// A refine predicate value (content irrelevant here; it must be skipped).
constexpr auto always_true = [](auto&) { return true; };
using S = Struct<Field<"a", U8>, Refine<always_true, "r">, Field<"b", U16BE>>;
}  // namespace

TEST_CASE("visits_fields_in_declaration_order") {
  std::vector<std::string> names;
  for_each_field<S>([&](auto info) { names.emplace_back(info.name.sv()); });
  REQUIRE(names.size() == 2);
  CHECK(names[0] == "a");
  CHECK(names[1] == "b");
}

TEST_CASE("field_index_and_type") {
  static_assert(field_index_v<S, "a"> == 0);
  static_assert(field_index_v<S, "b"> == 1);
  static_assert(std::same_as<field_type_t<S, "a">, U8>);
  static_assert(std::same_as<field_type_t<S, "b">, U16BE>);
  CHECK(true);
}

TEST_CASE("constexpr_usable") {
  static_assert(field_count_v<S> == 2);
  // FieldInfo carries a compile-time index.
  bool indices_ok = true;
  std::size_t expected = 0;
  for_each_field<S>([&](auto info) {
    if (info.index != expected) indices_ok = false;
    ++expected;
  });
  CHECK(indices_ok);
}
