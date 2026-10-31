#ifndef PORTCULLIS_VISIT_FOR_EACH_FIELD_HPP
#define PORTCULLIS_VISIT_FOR_EACH_FIELD_HPP

#include <cstddef>
#include <string_view>
#include <type_traits>
#include <utility>

#include "portcullis/core/fixed_string.hpp"
#include "portcullis/schema/combinators.hpp"

namespace portcullis {

// What a visitor receives for each field, in declaration order. The index is
// the field's position among fields only (Refine items do not advance it).
template <std::size_t Index, fixed_string Name, class T>
struct FieldInfo {
  static constexpr std::size_t index = Index;
  static constexpr auto name = Name;
  using type = T;
};

namespace detail {

// A compile-time list of the Field items of a Struct, in order.
template <class... Fs>
struct type_list {};

template <class Acc, class... Items>
struct filter_fields;
template <class... Fs>
struct filter_fields<type_list<Fs...>> {
  using type = type_list<Fs...>;
};
template <class... Fs, class Item, class... Rest>
struct filter_fields<type_list<Fs...>, Item, Rest...> {
  using kept = std::conditional_t<is_field_v<Item>, type_list<Fs..., Item>, type_list<Fs...>>;
  using type = typename filter_fields<kept, Rest...>::type;
};

template <class S>
struct fields_of;
template <class... Items>
struct fields_of<Struct<Items...>> {
  using type = typename filter_fields<type_list<>, Items...>::type;
};
template <class S>
using fields_of_t = typename fields_of<S>::type;

// Index of the field named `name`, or std::size_t(-1) if absent.
template <class... Fs>
consteval std::size_t index_of(std::string_view name) {
  if constexpr (sizeof...(Fs) == 0) {
    (void)name;
    return static_cast<std::size_t>(-1);
  } else {
    const std::string_view names[] = {Fs::name.sv()...};
    for (std::size_t i = 0; i < sizeof...(Fs); ++i)
      if (names[i] == name) return i;
    return static_cast<std::size_t>(-1);
  }
}

template <class List, fixed_string Name>
struct list_index;
template <class... Fs, fixed_string Name>
struct list_index<type_list<Fs...>, Name> {
  static constexpr std::size_t value = index_of<Fs...>(Name.sv());
};

template <std::size_t I, class F0, class... Fs>
struct nth {
  using type = typename nth<I - 1, Fs...>::type;
};
template <class F0, class... Fs>
struct nth<0, F0, Fs...> {
  using type = typename F0::type;
};

template <class List, std::size_t I>
struct list_at;
template <class... Fs, std::size_t I>
struct list_at<type_list<Fs...>, I> {
  using type = typename nth<I, Fs...>::type;
};

template <class List>
struct list_size;
template <class... Fs>
struct list_size<type_list<Fs...>> {
  static constexpr std::size_t value = sizeof...(Fs);
};

template <class... Fs, class F>
constexpr void for_each_field_impl(type_list<Fs...>, F&& f) {
  [&]<std::size_t... Is>(std::index_sequence<Is...>) {
    (f(FieldInfo<Is, Fs::name, typename Fs::type>{}), ...);
  }(std::make_index_sequence<sizeof...(Fs)>{});
}

}  // namespace detail

// Calls f(FieldInfo<Index, Name, Type>{}) for each Field of S in declaration
// order; Refine items are skipped. S must be a Struct<...>.
template <class S, class F>
constexpr void for_each_field(F&& f) {
  detail::for_each_field_impl(detail::fields_of_t<S>{}, std::forward<F>(f));
}

template <class S>
inline constexpr std::size_t field_count_v =
    detail::list_size<detail::fields_of_t<S>>::value;

// Index of the field named Name among the fields of S. Compile error if absent.
template <class S, fixed_string Name>
inline constexpr std::size_t field_index_v = [] {
  constexpr std::size_t v = detail::list_index<detail::fields_of_t<S>, Name>::value;
  static_assert(v != static_cast<std::size_t>(-1), "field name not declared in Struct");
  return v;
}();

template <class S, fixed_string Name>
using field_type_t = typename detail::list_at<detail::fields_of_t<S>, field_index_v<S, Name>>::type;

}  // namespace portcullis

#endif  // PORTCULLIS_VISIT_FOR_EACH_FIELD_HPP
