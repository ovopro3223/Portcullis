// Task 1.9 — Refine: a predicate over earlier fields, run at its position.
#include "third_party/doctest.h"

#include <cstddef>
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

using LenLe64 =
    Struct<Field<"len", U8>, Refine<[](auto& v) { return field<"len">(v) <= 64; }, "len_le_64">>;

// A refine sits between a and b; it fails when a > 4.
using Between = Struct<Field<"a", U8>, Refine<[](auto& v) { return field<"a">(v) <= 4; }, "a_le_4">,
                       Field<"b", U8>>;
}  // namespace

TEST_CASE("refinement_failure_names_itself") {
  auto b = bytes({65});  // len = 65 > 64
  auto r = parse<LenLe64>(view(b));
  REQUIRE(!r);
  CHECK(r.error().kind == ErrorKind::RefinementFailed);
  CHECK(r.error().name == "len_le_64");
  CHECK(r.error().path == "len_le_64");
  CHECK(r.error().offset == 1);  // after the last parsed field
}

TEST_CASE("refinement_passes_when_true") {
  auto b = bytes({64});
  auto r = parse<LenLe64>(view(b));
  REQUIRE(r);
  CHECK(r->get<"len">() == 64);
}

TEST_CASE("refinement_runs_at_its_position") {
  // a = 5 fails the refine; b is absent (truncated), but the refinement is what
  // is reported because it runs before b is read.
  auto b = bytes({0x05});
  auto r = parse<Between>(view(b));
  REQUIRE(!r);
  CHECK(r.error().kind == ErrorKind::RefinementFailed);
  CHECK(r.error().name == "a_le_4");
}
