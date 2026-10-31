#ifndef PORTCULLIS_PARSE_PARSE_HPP
#define PORTCULLIS_PARSE_PARSE_HPP

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "portcullis/core/cursor.hpp"
#include "portcullis/core/error.hpp"
#include "portcullis/core/result.hpp"
#include "portcullis/core/untrusted.hpp"
#include "portcullis/parse/parsed.hpp"
#include "portcullis/parse/path.hpp"
#include "portcullis/parse/value_of.hpp"
#include "portcullis/schema/combinators.hpp"
#include "portcullis/schema/partial.hpp"
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

// The I-th element of a pack (the element type itself, e.g. a Field<…>).
template <std::size_t I, class F0, class... Fs>
struct pack_at : pack_at<I - 1, Fs...> {};
template <class F0, class... Fs>
struct pack_at<0, F0, Fs...> {
  using type = F0;
};

// Struct of the first K fields of S — the prefix a predicate at position K sees.
template <class L, class Seq>
struct prefix_struct_impl;
template <class... Fs, std::size_t... Is>
struct prefix_struct_impl<type_list<Fs...>, std::index_sequence<Is...>> {
  using type = Struct<typename pack_at<Is, Fs...>::type...>;
};
template <class S, std::size_t K>
using prefix_struct_t =
    typename prefix_struct_impl<fields_of_t<S>, std::make_index_sequence<K>>::type;

template <class R>
struct refine_info;
template <auto Pred, fixed_string Name>
struct refine_info<Refine<Pred, Name>> {
  static constexpr auto pred = Pred;
  static constexpr auto name = Name;
};

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

// Tries each Case in order: the first whose value equals the tag is parsed and
// wrapped into the variant at that case's index. No match is a BadTag.
template <class S, std::size_t FieldIdx, std::size_t CaseIdx, class Variant, class PrefixTuple,
          class C0, class... Cs>
Result<Variant> choose(std::uint64_t tag, Cursor& cur, const PathBuilder& here,
                       const PrefixTuple& prefix) {
  using CaseT = typename case_value_type<C0>::type;
  constexpr auto Val = case_value_type<C0>::value;
  if (tag == static_cast<std::uint64_t>(Val)) {
    auto rv = parse_value<S, FieldIdx, CaseT>(cur, here, prefix);
    if (!rv) return Result<Variant>::fail(rv.error());
    return Result<Variant>::ok(
        Variant(std::in_place_index<CaseIdx>, std::move(const_cast<value_of_t<CaseT>&>(*rv))));
  }
  if constexpr (sizeof...(Cs) == 0) {
    return Result<Variant>::fail(
        Error{ErrorKind::BadTag, here.str(), cur.offset(), 0, 0, std::string_view{}, tag});
  } else {
    return choose<S, FieldIdx, CaseIdx + 1, Variant, PrefixTuple, Cs...>(tag, cur, here, prefix);
  }
}

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
    } else {  // is_refine_v<Item>
      constexpr auto Pred = refine_info<Item>::pred;
      constexpr auto Name = refine_info<Item>::name;
      // The predicate sees only the fields parsed so far (prefix_struct_t). A
      // reference to a not-yet-parsed field does not compile.
      const Partial<prefix_struct_t<S, FieldIdx>> partial(done, cur.remaining());
      if (!static_cast<bool>(Pred(partial))) {
        const PathBuilder rnode(&here, Name.sv());
        return Result<field_tuple_t<S>>::fail(Error{ErrorKind::RefinementFailed, rnode.str(),
                                                    cur.offset(), 0, 0, Name.sv(), 0});
      }
      return parse_items_t<S, FieldIdx, Done, type_list<Rest...>>::run(cur, here, std::move(done));
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
    using E = array_elem_t<T>;
    std::size_t count;
    if constexpr (array_count_is_ref_v<T>) {
      constexpr auto ref = array_count_v<T>;
      count = resolve_count<S, FieldIdx, ref.name>(prefix);
      // A count field must not make the parser allocate unboundedly: reject
      // before reserving or parsing a single element (D-008).
      constexpr std::size_t cap = array_cap_v<T>;
      if (count > cap)
        return R::fail(Error{ErrorKind::TooManyElements, here.str(), cur.offset(), count, cap});
    } else {
      count = static_cast<std::size_t>(array_count_v<T>);
    }
    std::vector<value_of_t<E>> out;
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
      const PathBuilder el(&here, i);
      auto rv = parse_value<S, FieldIdx, E>(cur, el, prefix);
      if (!rv) return R::fail(rv.error());
      out.push_back(std::move(const_cast<value_of_t<E>&>(*rv)));
    }
    return R::ok(std::move(out));

  } else if constexpr (is_choice_v<T>) {
    return [&]<auto TagRef, class... Cases>(Choice<TagRef, Cases...>*) -> R {
      static_assert(is_tag_v<std::remove_cvref_t<decltype(TagRef)>>,
                    "Choice discriminant must be Tag(\"field\")");
      const std::uint64_t tag =
          static_cast<std::uint64_t>(resolve_count<S, FieldIdx, TagRef.name>(prefix));
      return choose<S, FieldIdx, 0, value_of_t<T>, PrefixTuple, Cases...>(tag, cur, here, prefix);
    }(static_cast<T*>(nullptr));

  } else if constexpr (is_optional_v<T>) {
    return [&]<class ElemT, auto Pred>(Optional<ElemT, Pred>*) -> R {
      const Partial<prefix_struct_t<S, FieldIdx>> partial(prefix, cur.remaining());
      if (Pred(partial)) {
        auto rv = parse_value<S, FieldIdx, ElemT>(cur, here, prefix);
        if (!rv) return R::fail(rv.error());
        return R::ok(value_of_t<T>(std::move(const_cast<value_of_t<ElemT>&>(*rv))));
      }
      return R::ok(value_of_t<T>(std::nullopt));
    }(static_cast<T*>(nullptr));

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
