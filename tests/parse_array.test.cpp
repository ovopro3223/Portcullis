// Task 1.6 — Array with fixed and Ref counts, the element cap, element paths.
#include "third_party/doctest.h"

#include <cstddef>
#include <span>
#include <vector>

#include <portcullis/parse/parse.hpp>
#include <portcullis/schema/combinators.hpp>
#include <portcullis/schema/primitives.hpp>
#include <portcullis/schema/ref.hpp>

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

using Elem = Struct<Field<"x", U8>, Field<"y", U8>>;
using FixedArr = Struct<Field<"items", Array<U16BE, 3>>>;
using RefArr = Struct<Field<"n", U8>, Field<"items", Array<Elem, Ref("n")>>>;
using Capped = Struct<Field<"n", U8>, Field<"items", Array<U8, Ref("n"), 16>>>;
using ElemErr = Struct<Field<"n", U8>, Field<"items", Array<U16BE, Ref("n")>>>;
}  // namespace

TEST_CASE("fixed_count_array") {
  auto b = bytes({0x00, 0x01, 0x00, 0x02, 0x00, 0x03});
  auto r = parse<FixedArr>(view(b));
  REQUIRE(r);
  REQUIRE(r->get<"items">().size() == 3);
  CHECK(r->get<"items">()[0] == 1);
  CHECK(r->get<"items">()[2] == 3);
}

TEST_CASE("ref_count_array") {
  auto b = bytes({0x02, 0x10, 0x11, 0x20, 0x21});
  auto r = parse<RefArr>(view(b));
  REQUIRE(r);
  REQUIRE(r->get<"items">().size() == 2);
  CHECK(r->get<"items">()[0].get<"x">() == 0x10);
  CHECK(r->get<"items">()[1].get<"y">() == 0x21);
}

TEST_CASE("cap_rejects_before_allocating") {
  auto b = bytes({0xFF});  // n = 255 > cap 16
  auto r = parse<Capped>(view(b));
  REQUIRE(!r);
  CHECK(r.error().kind == ErrorKind::TooManyElements);
  CHECK(r.error().path == "items");
}

TEST_CASE("element_error_path_has_index") {
  // n = 3 but only two full U16BE elements plus one stray byte are present.
  auto b = bytes({0x03, 0x00, 0x01, 0x00, 0x02, 0x00});
  auto r = parse<ElemErr>(view(b));
  REQUIRE(!r);
  CHECK(r.error().kind == ErrorKind::Truncated);
  CHECK(r.error().path == "items[2]");
}
