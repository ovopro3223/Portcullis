// Must NOT compile: Choice's Tag("type") names a field declared AFTER the
// choice. A tag from the future is rejected at compile time.
#include <cstddef>
#include <span>

#include <portcullis/parse/parse.hpp>
#include <portcullis/schema/combinators.hpp>
#include <portcullis/schema/primitives.hpp>
#include <portcullis/schema/ref.hpp>

using namespace portcullis;

using Bad = Struct<Field<"body", Choice<Tag("type"), Case<1, U8>>>, Field<"type", U8>>;

int main() {
  std::byte storage[2]{};
  Untrusted u(std::span<const std::byte>(storage, 2));
  auto r = parse<Bad>(u);
  return r ? 0 : 1;
}
