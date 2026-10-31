// Untrusted must offer no way to read its bytes except through Cursor.
#include <portcullis/portcullis.hpp>

#include <array>
#include <cstddef>
#include <span>

int main() {
  std::array<unsigned char, 3> d{1, 2, 3};
  portcullis::Untrusted u(
      std::span<const std::byte>(reinterpret_cast<const std::byte*>(d.data()), 3));
  auto b = u[0];     // must not compile: no operator[]
  (void)b;
  (void)u.data();    // must not compile: no data()
  return 0;
}
