#ifndef PORTCULLIS_CORE_RESULT_HPP
#define PORTCULLIS_CORE_RESULT_HPP

#include <memory>
#include <utility>
#include <variant>

#include "portcullis/core/error.hpp"

namespace portcullis {

// Errors are values, never exceptions. Index 0 is the value, index 1 the error
// (index-based so T == Error would still be unambiguous).
template <class T>
class Result {
 public:
  static Result ok(T value) {
    return Result(std::variant<T, Error>(std::in_place_index<0>, std::move(value)));
  }
  static Result fail(Error e) {
    return Result(std::variant<T, Error>(std::in_place_index<1>, std::move(e)));
  }

  bool has_value() const noexcept { return v_.index() == 0; }
  explicit operator bool() const noexcept { return has_value(); }

  const T& operator*() const& { return std::get<0>(v_); }
  const T* operator->() const { return std::addressof(std::get<0>(v_)); }
  const Error& error() const { return std::get<1>(v_); }  // precondition: !has_value()

 private:
  explicit Result(std::variant<T, Error> v) : v_(std::move(v)) {}
  std::variant<T, Error> v_;
};

}  // namespace portcullis

#endif  // PORTCULLIS_CORE_RESULT_HPP
