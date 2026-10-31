// Task 1.7 — Choice/Case tagged unions.
#include "third_party/doctest.h"

#include <cstddef>
#include <span>
#include <variant>
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

using A = Struct<Field<"a", U8>>;
using B = Struct<Field<"b", U16BE>>;
using Msg = Struct<Field<"type", U8>, Field<"body", Choice<Tag("type"), Case<1, A>, Case<2, B>>>>;
}  // namespace

TEST_CASE("selects_case_by_tag") {
  auto b = bytes({0x02, 0x00, 0x05});
  auto r = parse<Msg>(view(b));
  REQUIRE(r);
  const auto& body = r->get<"body">();
  REQUIRE(std::holds_alternative<Parsed<B>>(body));
  CHECK(std::get<Parsed<B>>(body).get<"b">() == 5);
}

TEST_CASE("selects_first_case_by_tag") {
  auto b = bytes({0x01, 0x2A});
  auto r = parse<Msg>(view(b));
  REQUIRE(r);
  const auto& body = r->get<"body">();
  REQUIRE(std::holds_alternative<Parsed<A>>(body));
  CHECK(std::get<Parsed<A>>(body).get<"a">() == 0x2A);
}

TEST_CASE("unknown_tag_is_bad_tag") {
  auto b = bytes({0x09});
  auto r = parse<Msg>(view(b));
  REQUIRE(!r);
  CHECK(r.error().kind == ErrorKind::BadTag);
  CHECK(r.error().tag == 9);
  CHECK(r.error().path == "body");
}
