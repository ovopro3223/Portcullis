#ifndef PORTCULLIS_CORE_CURSOR_HPP
#define PORTCULLIS_CORE_CURSOR_HPP

#include <concepts>
#include <cstddef>
#include <span>
#include <type_traits>

#include "portcullis/core/endian.hpp"
#include "portcullis/core/untrusted.hpp"

namespace portcullis {

// The single read path. Every read checks remaining() before touching a byte,
// and leaves the cursor untouched on failure.
class Cursor {
 public:
  explicit Cursor(const Untrusted& in) noexcept : bytes_(in.bytes_) {}

  std::size_t remaining() const noexcept { return bytes_.size() - pos_; }
  std::size_t offset() const noexcept { return pos_; }

  [[nodiscard]] bool take(std::size_t n, std::span<const std::byte>& out) noexcept {
    if (n > remaining()) return false;
    out = bytes_.subspan(pos_, n);
    pos_ += n;
    return true;
  }

  template <std::unsigned_integral U>
  [[nodiscard]] bool read(Endian e, U& out) noexcept {
    std::span<const std::byte> s;
    if (!take(sizeof(U), s)) return false;
    U v = 0;
    if (e == Endian::Big) {
      for (std::size_t i = 0; i < sizeof(U); ++i)
        v = static_cast<U>((v << 8) | static_cast<U>(std::to_integer<unsigned>(s[i])));
    } else {
      for (std::size_t i = 0; i < sizeof(U); ++i)
        v = static_cast<U>(v | (static_cast<U>(std::to_integer<unsigned>(s[i])) << (8 * i)));
    }
    out = v;
    return true;
  }

  template <std::signed_integral I>
  [[nodiscard]] bool read(Endian e, I& out) noexcept {
    std::make_unsigned_t<I> u;
    if (!read(e, u)) return false;
    out = static_cast<I>(u);  // two's complement, well-defined in C++20
    return true;
  }

 private:
  std::span<const std::byte> bytes_;
  std::size_t pos_ = 0;
};

}  // namespace portcullis

#endif  // PORTCULLIS_CORE_CURSOR_HPP
