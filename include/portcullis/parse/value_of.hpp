#ifndef PORTCULLIS_PARSE_VALUE_OF_HPP
#define PORTCULLIS_PARSE_VALUE_OF_HPP

#include <cstddef>
#include <optional>
#include <span>
#include <variant>
#include <vector>

#include "portcullis/schema/combinators.hpp"
#include "portcullis/schema/primitives.hpp"

namespace portcullis {

template <class S>
class Parsed;  // defined in parse/parsed.hpp

// value_of_t<T> — the C++ value a schema type produces once parsed. Integers are
// copied out; Bytes is a view into the validated input; a nested Struct is a
// Parsed of it; Array a vector; Choice a variant; Optional an optional.
template <class T>
struct value_of_impl {
  static_assert(Primitive<T>, "value_of_t: unsupported schema type");
  using type = typename T::value_type;
};

template <class T>
using value_of_t = typename value_of_impl<T>::type;

template <auto Len>
struct value_of_impl<Bytes<Len>> {
  using type = std::span<const std::byte>;
};

template <class... Items>
struct value_of_impl<Struct<Items...>> {
  using type = Parsed<Struct<Items...>>;
};

template <class E, auto Count, std::size_t Cap>
struct value_of_impl<Array<E, Count, Cap>> {
  using type = std::vector<value_of_t<E>>;
};

template <class T, auto Pred>
struct value_of_impl<Optional<T, Pred>> {
  using type = std::optional<value_of_t<T>>;
};

namespace detail {
template <class C>
struct case_value_type;
template <auto V, class T>
struct case_value_type<Case<V, T>> {
  using type = T;
  static constexpr auto value = V;
};
}  // namespace detail

template <auto TagRef, class... Cases>
struct value_of_impl<Choice<TagRef, Cases...>> {
  using type = std::variant<value_of_t<typename detail::case_value_type<Cases>::type>...>;
};

}  // namespace portcullis

#endif  // PORTCULLIS_PARSE_VALUE_OF_HPP
