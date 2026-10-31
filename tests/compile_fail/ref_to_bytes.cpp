// Must NOT compile: Ref("hdr") targets a Bytes<4> field, not an unsigned
// integer primitive, so it cannot be used as a length.
#include <cstddef>
#include <span>

#include <portcullis/parse/parse.hpp>
#include <portcullis/schema/combinators.hpp>
#include <portcullis/schema/primitives.hpp>
#include <portcullis/schema/ref.hpp>

using namespace portcullis;

using Bad = Struct<Field<"hdr", Bytes<4>>, Field<"payload", Bytes<Ref("hdr")>>>;

int main() {
  std::byte storage[8]{};
  Untrusted u(std::span<const std::byte>(storage, 8));
  auto r = parse<Bad>(u);
  return r ? 0 : 1;
}
