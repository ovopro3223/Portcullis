#ifndef PORTCULLIS_CORE_ERROR_HPP
#define PORTCULLIS_CORE_ERROR_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace portcullis {

enum class ErrorKind { Truncated, OutOfRange, RefinementFailed, BadTag, TooManyElements };

// First failure only: after it, the remaining bytes have no meaning. The path
// names the field ("frames[3].payload"); need/have apply to Truncated, name to
// RefinementFailed, tag to BadTag.
struct Error {
  ErrorKind kind;
  std::string path;
  std::size_t offset;
  std::size_t need = 0;
  std::size_t have = 0;
  std::string_view name = {};
  std::uint64_t tag = 0;
};

}  // namespace portcullis

#endif  // PORTCULLIS_CORE_ERROR_HPP
