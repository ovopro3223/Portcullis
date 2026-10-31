#ifndef PORTCULLIS_VISIT_LAYOUT_HPP
#define PORTCULLIS_VISIT_LAYOUT_HPP

#include <cstddef>

#include "portcullis/schema/combinators.hpp"
#include "portcullis/schema/primitives.hpp"
#include "portcullis/visit/for_each_field.hpp"

namespace portcullis {

// Compile-time layout: how much of a schema has offsets fixed at compile time.
// The parser checks the whole fixed prefix with one bounds check instead of one
// per field, so a fully fixed header costs a single check.

namespace detail {

template <class T>
consteval bool compute_is_fixed();
template <class T>
consteval std::size_t compute_fixed_size();  // precondition: compute_is_fixed<T>()

template <class L>
struct struct_layout;
template <class... Fs>
struct struct_layout<type_list<Fs...>> {
  static consteval bool all_fixed() { return (compute_is_fixed<typename Fs::type>() && ... && true); }

  template <std::size_t I>
  static consteval std::size_t size_from() {
    if constexpr (I >= sizeof...(Fs)) {
      return 0;
    } else {
      return compute_fixed_size<typename nth<I, Fs...>::type>() + size_from<I + 1>();
    }
  }
};

// The fixed prefix is computed over the RAW items: it is the run of leading
// fixed fields, ending at the first variable field OR the first Refine (a
// refine must run at its position, so bytes beyond it cannot be pre-checked).
template <class ItemList>
struct items_prefix;
template <>
struct items_prefix<type_list<>> {
  static consteval std::size_t value() { return 0; }
};
template <class I0, class... Is>
struct items_prefix<type_list<I0, Is...>> {
  static consteval std::size_t value() {
    if constexpr (is_refine_v<I0>) {
      return 0;
    } else if constexpr (is_field_v<I0>) {
      using T = typename I0::type;
      if constexpr (compute_is_fixed<T>()) {
        return compute_fixed_size<T>() + items_prefix<type_list<Is...>>::value();
      } else {
        return 0;
      }
    } else {
      return 0;
    }
  }
};

// An empty struct (or a struct with only Refine items) has no fields.
template <>
struct struct_layout<type_list<>> {
  static consteval bool all_fixed() { return true; }
  template <std::size_t>
  static consteval std::size_t size_from() { return 0; }
};

template <class T>
consteval bool compute_is_fixed() {
  if constexpr (Primitive<T>) {
    return true;
  } else if constexpr (is_bytes_v<T>) {
    return !bytes_len_is_ref_v<T>;
  } else if constexpr (is_array_v<T>) {
    if constexpr (array_count_is_ref_v<T>) {
      return false;
    } else {
      return compute_is_fixed<array_elem_t<T>>();
    }
  } else if constexpr (is_choice_v<T>) {
    return false;
  } else if constexpr (is_optional_v<T>) {
    return false;
  } else if constexpr (is_struct_v<T>) {
    return struct_layout<fields_of_t<T>>::all_fixed();
  } else {
    return false;
  }
}

template <class T>
consteval std::size_t compute_fixed_size() {
  if constexpr (Primitive<T>) {
    return T::width;
  } else if constexpr (is_bytes_v<T>) {
    return static_cast<std::size_t>(bytes_len_v<T>);
  } else if constexpr (is_array_v<T>) {
    return static_cast<std::size_t>(array_count_v<T>) * compute_fixed_size<array_elem_t<T>>();
  } else if constexpr (is_struct_v<T>) {
    return struct_layout<fields_of_t<T>>::template size_from<0>();
  } else {
    return 0;  // unreachable: only called on fixed types
  }
}

}  // namespace detail

// True when the schema has no Bytes<Ref>, Array<…,Ref>, Choice or Optional
// anywhere, so its every offset is a compile-time constant.
template <class S>
inline constexpr bool is_fixed_v = detail::compute_is_fixed<S>();

// Bytes before the first variable-length field or Refine (the whole schema if
// none). One bounds check covers this prefix.
template <class S>
inline constexpr std::size_t fixed_prefix_v =
    detail::items_prefix<detail::items_of_t<S>>::value();

// Total size; defined only when is_fixed_v<S>.
template <class S>
inline constexpr std::size_t fixed_size_v = [] {
  static_assert(is_fixed_v<S>, "fixed_size_v requires a fully fixed schema");
  return detail::struct_layout<detail::fields_of_t<S>>::template size_from<0>();
}();

}  // namespace portcullis

#endif  // PORTCULLIS_VISIT_LAYOUT_HPP
