#ifndef PORTCULLIS_PARSE_PARSE_HPP
#define PORTCULLIS_PARSE_PARSE_HPP

#include <concepts>
#include <cstddef>
#include <span>
#include <tuple>
#include <utility>

#include "portcullis/core/cursor.hpp"
#include "portcullis/core/error.hpp"
#include "portcullis/core/result.hpp"
#include "portcullis/core/untrusted.hpp"
#include "portcullis/parse/parsed.hpp"
#include "portcullis/parse/path.hpp"
#include "portcullis/parse/value_of.hpp"
#include "portcullis/schema/combinators.hpp"
#include "portcullis/schema/primitives.hpp"
#include "portcullis/visit/for_each_field.hpp"
#include "portcullis/visit/layout.hpp"

namespace portcullis {
namespace detail {

template <class>
inline constexpr bool always_false = false;

template <class L>
struct first_field_name;
template <class F0, class... Fs>
struct first_field_name<type_list<F0, Fs...>> {
  static constexpr auto value = F0::name;
};
template <class S>
inline constexpr auto first_field_name_v = first_field_name<fields_of_t<S>>::value;

// Resolve a Ref/Tag that names an earlier field, as an unsigned count. Both
// halves of the reference rule (spec 5.2) are enforced here at compile time: the
// target must be declared earlier, and it must be an unsigned integer primitive.
// prefix holds the values of the FieldIdx fields parsed so far, so a resolved
// index is always within it.
template <class S, std::size_t FieldIdx, fixed_string RefName, class PrefixTuple>
std::size_t resolve_count(const PrefixTuple& prefix) {
  constexpr std::size_t ri = field_index_v<S, RefName>;
  static_assert(ri < FieldIdx, "reference names a field that is not declared earlier");
  using Target = field_type_t<S, RefName>;
  static_assert(Primitive<Target>, "reference target must be an unsigned integer primitive");
  static_assert(std::unsigned_integral<typename Target::value_type>,
                "reference target must be an unsigned integer primitive");
  return static_cast<std::size_t>(std::get<ri>(prefix));
}

// Mutually recursive: a field may itself be a Struct.
template <class S>
Result<Parsed<S>> parse_struct(Cursor& cur, const PathBuilder& here);

template <class S, std::size_t FieldIdx, class T, class PrefixTuple>
Result<value_of_t<T>> parse_value(Cursor& cur, const PathBuilder& here, const PrefixTuple& prefix);

// Walks the Items of a Struct in order, threading the tuple of field values
// parsed so far (Done). A Field appends a value and advances FieldIdx; a Refine
// runs its predicate at this position (Task 1.9) without advancing it.
template <class S, std::size_t FieldIdx, class Done, class ItemList>
struct parse_items_t;

template <class S, std::size_t FieldIdx, class Done>
struct parse_items_t<S, FieldIdx, Done, type_list<>> {
  static Result<field_tuple_t<S>> run(Cursor&, const PathBuilder&, Done done) {
    return Result<field_tuple_t<S>>::ok(std::move(done));
  }
};

template <class S, std::size_t FieldIdx, class Done, class Item, class... Rest>
struct parse_items_t<S, FieldIdx, Done, type_list<Item, Rest...>> {
  static Result<field_tuple_t<S>> run(Cursor& cur, const PathBuilder& here, Done done) {
    if constexpr (is_field_v<Item>) {
      using FT = typename Item::type;
      const PathBuilder fnode(&here, Item::name.sv());
      auto rv = parse_value<S, FieldIdx, FT>(cur, fnode, done);
      if (!rv) return Result<field_tuple_t<S>>::fail(rv.error());
      auto& moved = const_cast<value_of_t<FT>&>(*rv);  // rv is local; the value is non-const
      auto next = std::tuple_cat(std::move(done), std::tuple<value_of_t<FT>>(std::move(moved)));
      return parse_items_t<S, FieldIdx + 1, decltype(next), type_list<Rest...>>::run(
          cur, here, std::move(next));
    } else {
      static_assert(always_false<Item>, "Refine parsing is implemented in Task 1.9");
    }
  }
};

template <class S, std::size_t FieldIdx, class T, class PrefixTuple>
Result<value_of_t<T>> parse_value(Cursor& cur, const PathBuilder& here, const PrefixTuple& prefix) {
  using R = Result<value_of_t<T>>;
  (void)prefix;

  if constexpr (Primitive<T>) {
    typename T::value_type v{};
    const std::size_t at = cur.offset();
    if (!cur.read(T::endian, v))
      return R::fail(Error{ErrorKind::Truncated, here.str(), at, T::width, cur.remaining()});
    return R::ok(v);

  } else if constexpr (is_bytes_v<T>) {
    std::size_t n;
    if constexpr (bytes_len_is_ref_v<T>) {
      constexpr auto ref = bytes_len_v<T>;  // the Ref value
      n = resolve_count<S, FieldIdx, ref.name>(prefix);
    } else {
      n = static_cast<std::size_t>(bytes_len_v<T>);
    }
    std::span<const std::byte> sp;
    const std::size_t at = cur.offset();
    if (!cur.take(n, sp))
      return R::fail(Error{ErrorKind::Truncated, here.str(), at, n, cur.remaining()});
    return R::ok(sp);

  } else if constexpr (is_struct_v<T>) {
    return parse_struct<T>(cur, here);

  } else if constexpr (is_array_v<T>) {
    static_assert(always_false<T>, "Array is implemented in Task 1.6");
  } else if constexpr (is_choice_v<T>) {
    static_assert(always_false<T>, "Choice is implemented in Task 1.7");
  } else if constexpr (is_optional_v<T>) {
    static_assert(always_false<T>, "Optional is implemented in Task 1.8");
  } else {
    static_assert(always_false<T>, "parse_value: unsupported schema type");
  }
}

template <class S>
Result<Parsed<S>> parse_struct(Cursor& cur, const PathBuilder& here) {
  using RS = Result<Parsed<S>>;

  // Name the validated region without reading: take(0) yields a pointer at the
  // current position, and offset() gives the end once parsing completes.
  std::span<const std::byte> start_span;
  (void)cur.take(0, start_span);
  const std::byte* base = start_span.data();
  const std::size_t start = cur.offset();

  // One bounds check for the whole fixed prefix (D-010, spec 5.3).
  if constexpr (field_count_v<S> > 0) {
    constexpr std::size_t need = fixed_prefix_v<S>;
    if (need > cur.remaining()) {
      constexpr auto fname = first_field_name_v<S>;
      const PathBuilder fnode(&here, fname.sv());
      return RS::fail(Error{ErrorKind::Truncated, fnode.str(), cur.offset(), need, cur.remaining()});
    }
  }

  auto res = [&]<class... It>(Struct<It...>*) {
    return parse_items_t<S, 0, std::tuple<>, type_list<It...>>::run(cur, here, std::tuple<>{});
  }(static_cast<S*>(nullptr));
  if (!res) return RS::fail(res.error());

  const std::span<const std::byte> region(base, cur.offset() - start);
  auto& fields = const_cast<field_tuple_t<S>&>(*res);
  return RS::ok(Parsed<S>(std::move(fields), region));
}

}  // namespace detail

// The only door: parse untrusted bytes through the contract S (a Struct<…>).
template <class S>
Result<Parsed<S>> parse(const Untrusted& in) {
  Cursor cur(in);
  const PathBuilder root;
  return detail::parse_struct<S>(cur, root);
}

}  // namespace portcullis

#endif  // PORTCULLIS_PARSE_PARSE_HPP
