#ifndef PORTCULLIS_PARSE_PARSED_HPP
#define PORTCULLIS_PARSE_PARSED_HPP

#include <cstddef>
#include <span>
#include <tuple>
#include <utility>

#include "portcullis/core/fixed_string.hpp"
#include "portcullis/parse/value_of.hpp"
#include "portcullis/schema/combinators.hpp"
#include "portcullis/visit/for_each_field.hpp"

namespace portcullis {

namespace detail {
template <class L>
struct field_tuple;
template <class... Fs>
struct field_tuple<type_list<Fs...>> {
  using type = std::tuple<value_of_t<typename Fs::type>...>;
};
template <class S>
using field_tuple_t = typename field_tuple<fields_of_t<S>>::type;
}  // namespace detail

// The result of a successful parse: immutable, built only once every field and
// refinement has passed. Integer fields are copied; byte fields are spans into
// the original buffer whose length the contract has already checked. There is
// no public default constructor and get() yields a const reference, so a Parsed
// cannot be fabricated or mutated after the fact.
template <class S>
class Parsed;

template <class... Items>
class Parsed<Struct<Items...>> {
 public:
  using Schema = Struct<Items...>;
  using Fields = detail::field_tuple_t<Schema>;

  Parsed(Fields fields, std::span<const std::byte> bytes)
      : fields_(std::move(fields)), bytes_(bytes) {}

  template <fixed_string Name>
  const auto& get() const noexcept {
    return std::get<field_index_v<Schema, Name>>(fields_);
  }

  std::span<const std::byte> bytes() const noexcept { return bytes_; }

 private:
  Fields fields_;
  std::span<const std::byte> bytes_;
};

}  // namespace portcullis

#endif  // PORTCULLIS_PARSE_PARSED_HPP
