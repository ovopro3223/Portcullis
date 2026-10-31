#ifndef PORTCULLIS_CORE_UNTRUSTED_HPP
#define PORTCULLIS_CORE_UNTRUSTED_HPP

#include <cstddef>
#include <span>

namespace portcullis {

class Cursor;

// The one door to raw input. It exposes its length and nothing else: no
// data(), no operator[], no iterators. The only thing that can read the bytes
// is Cursor (a friend), which is the library's single checked read path.
class Untrusted {
 public:
  explicit Untrusted(std::span<const std::byte> bytes) noexcept : bytes_(bytes) {}
  std::size_t size() const noexcept { return bytes_.size(); }

 private:
  std::span<const std::byte> bytes_;
  friend class Cursor;
};

}  // namespace portcullis

#endif  // PORTCULLIS_CORE_UNTRUSTED_HPP
