#ifndef PORTCULLIS_SCHEMA_REF_HPP
#define PORTCULLIS_SCHEMA_REF_HPP

#include <cstddef>

#include "portcullis/core/fixed_string.hpp"

namespace portcullis {

// A reference to an earlier field by name, used as a length or count:
// Bytes<Ref("len")>, Array<T, Ref("count")>. It is a structural type (its only
// member is a fixed_string), so it can be a non-type template parameter, which
// is what lets Bytes<16> and Bytes<Ref("len")> come from one template<auto>.
template <std::size_t N>
struct Ref {
  fixed_string<N> name;
  constexpr Ref(const char (&s)[N]) : name(s) {}
  constexpr bool operator==(const Ref&) const = default;
};

template <std::size_t N>
Ref(const char (&)[N]) -> Ref<N>;

// A reference to an earlier field used as a tag discriminator for a Choice:
// Choice<Tag("type"), Case<...>...>. Same structural shape as Ref; a distinct
// type so a tag reference cannot be confused with a length reference.
template <std::size_t N>
struct Tag {
  fixed_string<N> name;
  constexpr Tag(const char (&s)[N]) : name(s) {}
  constexpr bool operator==(const Tag&) const = default;
};

template <std::size_t N>
Tag(const char (&)[N]) -> Tag<N>;

template <class T>
inline constexpr bool is_ref_v = false;
template <std::size_t N>
inline constexpr bool is_ref_v<Ref<N>> = true;

template <class T>
inline constexpr bool is_tag_v = false;
template <std::size_t N>
inline constexpr bool is_tag_v<Tag<N>> = true;

}  // namespace portcullis

#endif  // PORTCULLIS_SCHEMA_REF_HPP
