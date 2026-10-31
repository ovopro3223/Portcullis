// Task 1.1 — primitives, Ref/Tag, Field, Struct and the combinator type shells.
#include "third_party/doctest.h"

#include <cstdint>
#include <type_traits>

#include <portcullis/schema/combinators.hpp>
#include <portcullis/schema/primitives.hpp>
#include <portcullis/schema/ref.hpp>

using namespace portcullis;

TEST_CASE("primitives_declare_width_and_endian") {
  static_assert(U16BE::width == 2);
  static_assert(U32LE::endian == Endian::Little);
  static_assert(U16BE::endian == Endian::Big);
  static_assert(std::same_as<U8::value_type, std::uint8_t>);
  static_assert(std::same_as<I16BE::value_type, std::int16_t>);
  static_assert(U64LE::width == 8);
  static_assert(Primitive<U8>);
  static_assert(Primitive<I32BE>);
  static_assert(!Primitive<Bytes<4>>);
  CHECK(true);
}

TEST_CASE("ref_and_tag_carry_names") {
  static_assert(Ref("len").name.sv() == "len");
  static_assert(Tag("type").name.sv() == "type");
  static_assert(is_ref_v<decltype(Ref("len"))>);
  static_assert(!is_ref_v<int>);
  static_assert(is_tag_v<decltype(Tag("type"))>);
  CHECK(std::string(Ref("len").name.sv()) == "len");
}

TEST_CASE("field_exposes_name_and_type") {
  using F = Field<"len", U16BE>;
  static_assert(F::name == fixed_string("len"));
  static_assert(std::same_as<F::type, U16BE>);
  CHECK(true);
}

TEST_CASE("bytes_accepts_int_and_ref") {
  using A = Bytes<16>;
  using B = Bytes<Ref("len")>;
  static_assert(!std::same_as<A, B>);
  static_assert(sizeof(A) >= 1);  // complete type
  static_assert(sizeof(B) >= 1);  // complete type
  // Struct is a complete type holding Field and Refine items.
  using S = Struct<Field<"a", U8>, Field<"b", B>>;
  static_assert(sizeof(S) >= 1);
  CHECK(true);
}
