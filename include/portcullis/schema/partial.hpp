#ifndef PORTCULLIS_SCHEMA_PARTIAL_HPP
#define PORTCULLIS_SCHEMA_PARTIAL_HPP

#include <cstddef>
#include <tuple>

#include "portcullis/core/fixed_string.hpp"
#include "portcullis/parse/parsed.hpp"
#include "portcullis/visit/for_each_field.hpp"

namespace portcullis {

// The view a predicate receives: the fields parsed so far, plus how many bytes
// remain. S is the prefix Struct of exactly those earlier fields, so naming a
// field that has not been parsed yet (a forward reference) is a compile error —
// field_index_v<S, Name> does not resolve.
template <class S>
class Partial {
 public:
  using Schema = S;
  using Fields = detail::field_tuple_t<S>;

  Partial(const Fields& fields, std::size_t remaining) noexcept
      : fields_(fields), remaining_(remaining) {}

  template <fixed_string Name>
  const auto& get() const noexcept {
    return std::get<field_index_v<S, Name>>(fields_);
  }

  std::size_t remaining() const noexcept { return remaining_; }

 private:
  const Fields& fields_;
  std::size_t remaining_;
};

// field<"n">(v) == v.get<"n">() without the .template keyword, so a predicate
// reads an earlier field as field<"flags">(v).
template <fixed_string Name, class V>
decltype(auto) field(const V& v) noexcept {
  return v.template get<Name>();
}

}  // namespace portcullis

#endif  // PORTCULLIS_SCHEMA_PARTIAL_HPP
