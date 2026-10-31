#include "third_party/doctest.h"
#include <portcullis/portcullis.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

using namespace portcullis;

namespace {
template <std::size_t N>
std::span<const std::byte> bs(const std::array<unsigned char, N>& a) {
  return std::span<const std::byte>(reinterpret_cast<const std::byte*>(a.data()), N);
}
}  // namespace

TEST_CASE("fixed_string_sv_drops_terminator") {
  CHECK(fixed_string("len").sv() == "len");
  CHECK(fixed_string("len") == fixed_string("len"));
}

TEST_CASE("untrusted_reports_size_only") {
  std::array<unsigned char, 5> d{1, 2, 3, 4, 5};
  CHECK(Untrusted(bs(d)).size() == 5);
}

TEST_CASE("cursor_take_refuses_past_end") {
  std::array<unsigned char, 3> d{1, 2, 3};
  Cursor c(Untrusted(bs(d)));
  std::span<const std::byte> out;
  CHECK_FALSE(c.take(4, out));
  CHECK(c.offset() == 0);
  CHECK(c.take(3, out));
  CHECK(out.size() == 3);
  CHECK(c.remaining() == 0);
}

TEST_CASE("cursor_read_big_and_little") {
  std::array<unsigned char, 2> d{0x12, 0x34};
  {
    Cursor c(Untrusted(bs(d)));
    std::uint16_t v = 0;
    CHECK(c.read(Endian::Big, v));
    CHECK(v == 0x1234);
    CHECK(c.offset() == 2);
  }
  {
    Cursor c(Untrusted(bs(d)));
    std::uint16_t v = 0;
    CHECK(c.read(Endian::Little, v));
    CHECK(v == 0x3412);
  }
}

TEST_CASE("cursor_read_truncated_leaves_offset") {
  std::array<unsigned char, 1> d{0xAB};
  Cursor c(Untrusted(bs(d)));
  std::uint16_t v = 0;
  CHECK_FALSE(c.read(Endian::Big, v));
  CHECK(c.offset() == 0);
}

TEST_CASE("cursor_reads_signed_twos_complement") {
  std::array<unsigned char, 2> d{0xFF, 0xFE};
  Cursor c(Untrusted(bs(d)));
  std::int16_t v = 0;
  CHECK(c.read(Endian::Big, v));
  CHECK(v == -2);
}

TEST_CASE("result_ok_and_fail") {
  auto a = Result<int>::ok(7);
  CHECK(a.has_value());
  CHECK(static_cast<bool>(a));
  CHECK(*a == 7);

  auto b = Result<int>::fail(Error{ErrorKind::Truncated, "x", 3, 4, 1});
  CHECK_FALSE(b.has_value());
  CHECK(b.error().kind == ErrorKind::Truncated);
  CHECK(b.error().need == 4);
  CHECK(b.error().have == 1);
}
