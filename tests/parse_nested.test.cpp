// Task 1.10 — nested structs and the error paths through them.
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

using Hdr = Struct<Field<"ver", U8>, Field<"flags", U8>>;
using Doc = Struct<Field<"hdr", Hdr>, Field<"tail", U8>>;

using Frame = Struct<Field<"len", U8>, Field<"payload", Bytes<Ref("len")>>>;
using Frames = Struct<Field<"n", U8>, Field<"frames", Array<Frame, Ref("n")>>>;

using Inner = Struct<Field<"len", U8>, Field<"payload", Bytes<Ref("len")>>>;
using Tagged = Struct<Field<"type", U8>, Field<"body", Choice<Tag("type"), Case<1, Inner>>>>;
}  // namespace

TEST_CASE("nested_values_are_parsed_structs") {
  auto b = bytes({0x03, 0x01, 0x09});
  auto r = parse<Doc>(view(b));
  REQUIRE(r);
  CHECK(r->get<"hdr">().get<"ver">() == 3);
  CHECK(r->get<"hdr">().get<"flags">() == 1);
  CHECK(r->get<"tail">() == 9);
}

TEST_CASE("nested_error_path_names_array_index") {
  // n=4; frames 0..2 are {len=1, 1 byte}; frame 3 declares len=255 with one byte.
  auto b = bytes({0x04, 0x01, 0xAA, 0x01, 0xBB, 0x01, 0xCC, 0xFF, 0xDD});
  auto r = parse<Frames>(view(b));
  REQUIRE(!r);
  CHECK(r.error().kind == ErrorKind::Truncated);
  CHECK(r.error().path == "frames[3].payload");
  CHECK(r.error().need == 255);
  CHECK(r.error().have == 1);
}

TEST_CASE("choice_path_uses_field_name") {
  // type=1 selects Inner, but no body bytes follow: Inner is truncated at "len".
  auto b = bytes({0x01});
  auto r = parse<Tagged>(view(b));
  REQUIRE(!r);
  CHECK(r.error().kind == ErrorKind::Truncated);
  CHECK(r.error().path == "body.len");
}
