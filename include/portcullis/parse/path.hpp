#ifndef PORTCULLIS_PARSE_PATH_HPP
#define PORTCULLIS_PARSE_PATH_HPP

#include <cstddef>
#include <string>
#include <string_view>

namespace portcullis {

// A field path built on the call stack: each parse frame links a segment to its
// parent, so no heap allocation happens on the happy path — a std::string is
// materialized only when an Error is produced. Field segments join with '.',
// array indices render as "[i]" with no separator, giving "frames[3].payload".
class PathBuilder {
 public:
  PathBuilder() = default;  // the root: contributes nothing
  PathBuilder(const PathBuilder* parent, std::string_view name) noexcept
      : parent_(parent), name_(name), is_index_(false) {}
  PathBuilder(const PathBuilder* parent, std::size_t index) noexcept
      : parent_(parent), index_(index), is_index_(true) {}

  std::string str() const {
    std::string out;
    append(out);
    return out;
  }

 private:
  void append(std::string& out) const {
    if (parent_) parent_->append(out);
    if (is_index_) {
      out += '[';
      out += std::to_string(index_);
      out += ']';
    } else if (!name_.empty()) {
      if (!out.empty()) out += '.';
      out += name_;
    }
  }

  const PathBuilder* parent_ = nullptr;
  std::string_view name_{};
  std::size_t index_ = 0;
  bool is_index_ = false;
};

}  // namespace portcullis

#endif  // PORTCULLIS_PARSE_PATH_HPP
