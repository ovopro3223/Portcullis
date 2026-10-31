// Must NOT compile: Bytes<Ref("len")> names "len", which is declared AFTER the
// payload field. A length from the future is rejected at compile time.
#include <cstddef>
#include <span>

#include <portcullis/parse/parse.hpp>
#include <portcullis/schema/combinators.hpp>
#include <portcullis/schema/primitives.hpp>
#include <portcullis/schema/ref.hpp>

using namespace portcullis;

using Bad = Struct<Field<"payload", Bytes<Ref("len")>>, Field<"len", U16BE>>;

int main() {
  std::byte storage[4]{};
  Untrusted u(std::span<const std::byte>(storage, 4));
  auto r = parse<Bad>(u);
  return r ? 0 : 1;
}
