// Task 1.8 — Optional<T, Pred>: present iff the predicate over earlier fields.
#include "third_party/doctest.h"

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

#include <portcullis/parse/parse.hpp>
#include <portcullis/schema/combinators.hpp>
#include <portcullis/schema/partial.hpp>
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

using Opt = Struct<Field<"flags", U8>,
                   Field<"opt", Optional<U16BE, [](auto& v) { return (field<"flags">(v) & 1) != 0; }>>>;
}  // namespace

TEST_CASE("present_when_predicate_true") {
  auto b = bytes({0x01, 0x12, 0x34});  // flags odd -> optional present
  auto r = parse<Opt>(view(b));
  REQUIRE(r);
  REQUIRE(r->get<"opt">().has_value());
  CHECK(*r->get<"opt">() == 0x1234);
  CHECK(r->bytes().size() == 3);
}

TEST_CASE("absent_consumes_nothing") {
  auto b = bytes({0x00, 0x12, 0x34});  // flags even -> optional absent
  auto r = parse<Opt>(view(b));
  REQUIRE(r);
  CHECK(!r->get<"opt">().has_value());
  CHECK(r->bytes().size() == 1);  // only the flags byte was consumed
}
