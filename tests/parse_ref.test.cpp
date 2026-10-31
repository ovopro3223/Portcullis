// Task 1.5 — Bytes<Ref("f")> lengths, Partial, forward-reference rule.
#include "third_party/doctest.h"

#include <cstddef>
#include <span>
#include <vector>

#include <portcullis/parse/parse.hpp>
#include <portcullis/schema/partial.hpp>

#include "schemas/heartbeat.hpp"

using namespace portcullis;
using portcullis::schemas::Heartbeat;

namespace {
std::vector<std::byte> bytes(std::initializer_list<int> vs) {
  std::vector<std::byte> out;
  out.reserve(vs.size());
  for (int v : vs) out.push_back(static_cast<std::byte>(v));
  return out;
}
std::vector<std::byte> with_padding(std::vector<std::byte> head) {
  for (int i = 0; i < 16; ++i) head.push_back(std::byte{0xAA});
  return head;
}
Untrusted view(const std::vector<std::byte>& b) {
  return Untrusted(std::span<const std::byte>(b.data(), b.size()));
}
}  // namespace

TEST_CASE("heartbleed_input_is_truncated_at_payload") {
  // type=1, len=0xFFFF (65535), one payload byte present, no padding.
  auto b = bytes({0x01, 0xFF, 0xFF, 0x41});
  auto r = parse<Heartbeat>(view(b));
  REQUIRE(!r);
  CHECK(r.error().kind == ErrorKind::Truncated);
  CHECK(r.error().path == "payload");
  CHECK(r.error().offset == 3);
  CHECK(r.error().need == 65535);
  CHECK(r.error().have == 1);
}

TEST_CASE("honest_heartbeat_parses") {
  // type=1, len=3, 3 payload bytes, 16 padding bytes.
  auto b = with_padding(bytes({0x01, 0x00, 0x03, 0x61, 0x62, 0x63}));
  auto r = parse<Heartbeat>(view(b));
  REQUIRE(r);
  CHECK(r->get<"type">() == 1);
  CHECK(r->get<"len">() == 3);
  CHECK(r->get<"payload">().size() == 3);
  CHECK(r->get<"padding">().size() == 16);
}

TEST_CASE("zero_length_ref_is_valid") {
  // len=0 yields an empty payload; this must not be mistaken for truncation.
  auto b = with_padding(bytes({0x01, 0x00, 0x00}));
  auto r = parse<Heartbeat>(view(b));
  REQUIRE(r);
  CHECK(r->get<"payload">().size() == 0);
  CHECK(r->get<"payload">().empty());
}

TEST_CASE("partial_exposes_earlier_fields") {
  // Partial<prefix> is what predicates receive; field<>() reads an earlier value.
  using Prefix = Struct<Field<"type", U8>, Field<"len", U16BE>>;
  std::tuple<std::uint8_t, std::uint16_t> vals{1, 7};
  Partial<Prefix> p(vals, 42);
  CHECK(field<"len">(p) == 7);
  CHECK(p.get<"type">() == 1);
  CHECK(p.remaining() == 42);
}
