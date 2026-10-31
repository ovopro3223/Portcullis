#ifndef PORTCULLIS_CORE_FIXED_STRING_HPP
#define PORTCULLIS_CORE_FIXED_STRING_HPP

#include <cstddef>
#include <string_view>

namespace portcullis {

// A compile-time string usable as a non-type template parameter, so field
// names are written as literals: Field<"len", U16BE>. Structural: all members
// public, which is what lets it be an NTTP.
template <std::size_t N>
struct fixed_string {
  char value[N]{};

  constexpr fixed_string(const char (&s)[N]) {
    for (std::size_t i = 0; i < N; ++i) value[i] = s[i];
  }

  // The view excludes the trailing '\0' the literal carries.
  constexpr std::string_view sv() const noexcept { return std::string_view(value, N - 1); }

  constexpr bool operator==(const fixed_string&) const = default;
};

template <std::size_t N>
fixed_string(const char (&)[N]) -> fixed_string<N>;

}  // namespace portcullis

#endif  // PORTCULLIS_CORE_FIXED_STRING_HPP
