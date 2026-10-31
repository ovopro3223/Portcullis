#ifndef PORTCULLIS_SCHEMA_COMBINATORS_HPP
#define PORTCULLIS_SCHEMA_COMBINATORS_HPP

#include <cstddef>

#include "portcullis/core/fixed_string.hpp"
#include "portcullis/schema/ref.hpp"

namespace portcullis {

// A schema is a type. These combinators carry no data and do nothing on their
// own; they are a description that `visit` walks and `parse`/`fuzz` interpret.

// A named field inside a Struct. The name is a string literal NTTP.
template <fixed_string Name, class T>
struct Field {
  static constexpr auto name = Name;
  using type = T;
};

// A sequence of Items, each a Field<...> or a Refine<...>, parsed in order and
// nestable (a Field's type may itself be a Struct).
template <class... Items>
struct Struct {};

// Exactly Len bytes. Len is either a compile-time count (Bytes<16>) or a
// Ref to an earlier unsigned-integer field (Bytes<Ref("len")>).
template <auto Len>
struct Bytes {};

// Count elements of T, parsed in order. Count is a compile-time count
// (Array<U16BE, 4>) or a Ref to an earlier field (Array<T, Ref("n")>). For a
// Ref count, more than Cap elements is rejected with TooManyElements before any
// element is parsed (D-008, default cap 4096).
template <class T, auto Count, std::size_t Cap = 4096>
struct Array {};

// One arm of a Choice: tag value V selects type T.
template <auto V, class T>
struct Case {};

// A tagged union discriminated by an earlier field named by TagRef.
template <auto TagRef, class... Cases>
struct Choice {};

// Present iff Pred(fields-parsed-so-far) is true; otherwise consumes nothing.
template <class T, auto Pred>
struct Optional {};

// A predicate over the fields parsed so far, run at its declaration position.
// Failure is reported as RefinementFailed carrying Name.
template <auto Pred, fixed_string Name>
struct Refine {};

// Item classification used by visit and parse.
template <class T>
inline constexpr bool is_field_v = false;
template <fixed_string Name, class T>
inline constexpr bool is_field_v<Field<Name, T>> = true;

template <class T>
inline constexpr bool is_refine_v = false;
template <auto Pred, fixed_string Name>
inline constexpr bool is_refine_v<Refine<Pred, Name>> = true;

template <class T>
inline constexpr bool is_struct_v = false;
template <class... Items>
inline constexpr bool is_struct_v<Struct<Items...>> = true;

template <class T>
inline constexpr bool is_bytes_v = false;
template <auto Len>
inline constexpr bool is_bytes_v<Bytes<Len>> = true;

template <class T>
inline constexpr bool is_array_v = false;
template <class E, auto Count, std::size_t Cap>
inline constexpr bool is_array_v<Array<E, Count, Cap>> = true;

template <class T>
inline constexpr bool is_choice_v = false;
template <auto TagRef, class... Cases>
inline constexpr bool is_choice_v<Choice<TagRef, Cases...>> = true;

template <class T>
inline constexpr bool is_optional_v = false;
template <class T, auto Pred>
inline constexpr bool is_optional_v<Optional<T, Pred>> = true;

}  // namespace portcullis

#endif  // PORTCULLIS_SCHEMA_COMBINATORS_HPP
