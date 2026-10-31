// Must NOT compile: the refine predicate references "b", which is declared
// AFTER the refine. A predicate may only read fields parsed before it.
#include <cstddef>
#include <span>

#include <portcullis/parse/parse.hpp>
#include <portcullis/schema/combinators.hpp>
#include <portcullis/schema/partial.hpp>
#include <portcullis/schema/primitives.hpp>

using namespace portcullis;

using Bad = Struct<Field<"a", U8>,
                   Refine<[](auto& v) { return field<"b">(v) == 0; }, "r">,
                   Field<"b", U8>>;

int main() {
  std::byte storage[2]{};
  Untrusted u(std::span<const std::byte>(storage, 2));
  auto r = parse<Bad>(u);
  return r ? 0 : 1;
}
