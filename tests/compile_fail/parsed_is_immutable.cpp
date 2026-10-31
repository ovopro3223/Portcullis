// Must NOT compile: a Parsed<S> cannot be fabricated from nothing, and a
// validated value cannot be assigned through get<>() (it yields a const ref).
#include <cstddef>
#include <span>

#include <portcullis/portcullis.hpp>

using namespace portcullis;

using S = Struct<Field<"a", U8>>;

int main() {
  Parsed<S> fresh;  // no public default constructor

  std::byte buf[1]{};
  Untrusted u(std::span<const std::byte>(buf, 1));
  auto r = parse<S>(u);
  (*r).get<"a">() = 7;  // get<>() returns a const reference: not assignable

  (void)fresh;
  return 0;
}
