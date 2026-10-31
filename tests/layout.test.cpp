// Task 1.3 — fixed_prefix_v, is_fixed_v, fixed_size_v.
#include "third_party/doctest.h"

#include <portcullis/schema/combinators.hpp>
#include <portcullis/schema/primitives.hpp>
#include <portcullis/schema/ref.hpp>
#include <portcullis/visit/layout.hpp>

using namespace portcullis;

namespace {
using Header = Struct<Field<"a", U8>, Field<"b", U32BE>, Field<"c", Bytes<3>>>;
using Heartbeat = Struct<Field<"type", U8>, Field<"len", U16BE>,
                         Field<"payload", Bytes<Ref("len")>>, Field<"padding", Bytes<16>>>;
using Inner = Struct<Field<"x", U16BE>, Field<"y", U32BE>>;  // 6 bytes, fixed
using Outer = Struct<Field<"a", U8>, Field<"inner", Inner>, Field<"tail", Bytes<Ref("a")>>>;
using ArrHdr = Struct<Field<"arr", Array<U16BE, 4>>>;
}  // namespace

TEST_CASE("all_fixed_header") {
  static_assert(is_fixed_v<Header>);
  static_assert(fixed_size_v<Header> == 8);
  static_assert(fixed_prefix_v<Header> == 8);
  CHECK(true);
}

TEST_CASE("prefix_stops_at_first_ref") {
  static_assert(!is_fixed_v<Heartbeat>);
  static_assert(fixed_prefix_v<Heartbeat> == 3);
  CHECK(true);
}

TEST_CASE("nested_fixed_struct_counts") {
  static_assert(is_fixed_v<Inner>);
  static_assert(fixed_size_v<Inner> == 6);
  static_assert(!is_fixed_v<Outer>);
  static_assert(fixed_prefix_v<Outer> == 7);  // a(1) + inner(6), then tail is a Ref
  CHECK(true);
}

TEST_CASE("fixed_array") {
  static_assert(is_fixed_v<ArrHdr>);
  static_assert(fixed_size_v<ArrHdr> == 8);  // 4 * U16BE
  static_assert(fixed_prefix_v<ArrHdr> == 8);
  CHECK(true);
}
