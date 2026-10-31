#ifndef PORTCULLIS_FUZZ_MUTATOR_HPP
#define PORTCULLIS_FUZZ_MUTATOR_HPP

// Structure-aware mutation. mutate<S> scans the current input against the
// schema S (the same walk the parser performs, following each Ref length/count
// and Choice tag through the bytes), locates the integer fields, field
// boundaries and array elements, and applies ONE field-level edit: grow or
// shrink a length/count, swap a Choice tag, truncate at a field boundary, or
// duplicate an array element. An input that does not scan cleanly falls back to
// a byte flip. Understanding the format lets the mutator reach deep parser
// states far faster than byte-level mutation (the libprotobuf-mutator idea).
//
// Discipline (Review Focus 5): the function never returns a size greater than
// max_size and never reads past `size` or writes past max_size. `buf` must have
// at least max_size bytes of capacity; the libFuzzer hook passes a span of
// max_size.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <span>
#include <string>
#include <vector>

#include "portcullis/core/endian.hpp"
#include "portcullis/fuzz/generator.hpp"  // Rng, mix, schema traits
#include "portcullis/schema/combinators.hpp"
#include "portcullis/schema/primitives.hpp"
#include "portcullis/visit/for_each_field.hpp"

namespace portcullis::fuzz {

// The edit mutate() last performed. Not part of the frozen interface — an
// observability hook so a test can confirm each field-level op is reachable.
// GenSeed means the input did not scan and was replaced with a freshly
// generated structure to bootstrap the fuzzer out of a shallow state.
enum class MutOp { ByteFlip, LenChange, TagSwap, Truncate, DupElem, GenSeed };
inline thread_local MutOp last_op = MutOp::ByteFlip;

namespace detail {

inline std::uint64_t width_max(std::size_t w) {
  return w >= 8 ? ~0ull : ((std::uint64_t(1) << (8 * w)) - 1);
}

inline std::uint64_t read_uint(const std::byte* d, std::size_t off, std::size_t width, Endian e) {
  std::uint64_t v = 0;
  if (e == Endian::Big) {
    for (std::size_t i = 0; i < width; ++i)
      v = (v << 8) | std::to_integer<unsigned>(d[off + i]);
  } else {
    for (std::size_t i = 0; i < width; ++i)
      v |= static_cast<std::uint64_t>(std::to_integer<unsigned>(d[off + i])) << (8 * i);
  }
  return v;
}

inline void write_uint(std::byte* d, std::size_t off, std::size_t width, Endian e,
                       std::uint64_t v) {
  if (e == Endian::Big) {
    for (std::size_t i = 0; i < width; ++i)
      d[off + i] = static_cast<std::byte>((v >> (8 * (width - 1 - i))) & 0xFF);
  } else {
    for (std::size_t i = 0; i < width; ++i)
      d[off + i] = static_cast<std::byte>((v >> (8 * i)) & 0xFF);
  }
}

// An integer field found by the scan, with the structural role the schema gives
// it: 0 plain, 1 Ref length, 2 Ref array count, 3 Choice tag.
struct IntMark {
  std::size_t offset = 0;
  std::size_t width = 0;
  Endian endian = Endian::Big;
  int role = 0;
  std::size_t cap = 0;  // role 2
};

// One array element's byte range, and the index of the count field that governs
// it (SIZE_MAX for a fixed-count array).
struct ElemMark {
  std::size_t count_int = static_cast<std::size_t>(-1);
  std::size_t begin = 0;
  std::size_t end = 0;
};

// Walks the schema over `data[0,size)`, following Refs and tags through the
// bytes exactly as the parser would. Returns false the moment the structure
// does not fit the bytes (truncated, over-cap count, unknown tag) — the caller
// then falls back to a byte flip.
template <class Top>
struct Scanner {
  const std::byte* data;
  std::size_t size;
  std::map<std::string, std::uint64_t> values;
  std::map<std::string, std::size_t> last_int;  // field name -> index into ints
  std::vector<IntMark> ints;
  std::vector<std::size_t> boundaries;
  std::vector<ElemMark> elems;
  std::size_t pos = 0;

  static constexpr std::size_t kMax = 1u << 16;  // guard against pathological growth

  Scanner(const std::byte* d, std::size_t s) : data(d), size(s) {}

  bool guard() const { return ints.size() < kMax && elems.size() < kMax && boundaries.size() < kMax; }

  template <class S>
  bool scan_struct() {
    bool ok = true;
    for_each_field<S>([&](auto fi) {
      if (!ok) return;
      using T = typename decltype(fi)::type;
      constexpr auto nm = decltype(fi)::name;
      if (!scan_field<T>(std::string(nm.sv()))) ok = false;
    });
    return ok;
  }

  template <class E>
  bool scan_type() {
    if constexpr (is_struct_v<E>)
      return scan_struct<E>();
    else
      return scan_field<E>(std::string{});
  }

  template <class C0, class... Cs>
  bool scan_cases(std::uint64_t tagv) {
    if (static_cast<std::uint64_t>(portcullis::detail::case_value_type<C0>::value) == tagv)
      return scan_type<typename portcullis::detail::case_value_type<C0>::type>();
    if constexpr (sizeof...(Cs) > 0) return scan_cases<Cs...>(tagv);
    return false;  // unknown tag: a BadTag input does not scan cleanly
  }

  template <class T>
  bool scan_field(const std::string& name) {
    if (!guard()) return false;
    boundaries.push_back(pos);

    if constexpr (Primitive<T>) {
      if (pos + T::width > size) return false;
      std::uint64_t v = read_uint(data, pos, T::width, T::endian);
      IntMark m;
      m.offset = pos;
      m.width = T::width;
      m.endian = T::endian;
      ints.push_back(m);
      if (!name.empty()) {
        values[name] = v;
        last_int[name] = ints.size() - 1;
      }
      pos += T::width;
      return true;

    } else if constexpr (is_bytes_v<T>) {
      std::size_t n;
      if constexpr (bytes_len_is_ref_v<T>) {
        std::string r(bytes_len_v<T>.name.sv());
        if (auto it = last_int.find(r); it != last_int.end()) ints[it->second].role = 1;
        n = static_cast<std::size_t>(values[r]);
      } else {
        n = static_cast<std::size_t>(bytes_len_v<T>);
      }
      if (pos + n > size) return false;
      pos += n;
      return true;

    } else if constexpr (is_array_v<T>) {
      std::size_t count;
      std::size_t count_int = static_cast<std::size_t>(-1);
      if constexpr (array_count_is_ref_v<T>) {
        std::string r(array_count_v<T>.name.sv());
        if (auto it = last_int.find(r); it != last_int.end()) {
          ints[it->second].role = 2;
          ints[it->second].cap = array_cap_v<T>;
          count_int = it->second;
        }
        count = static_cast<std::size_t>(values[r]);
        if (count > array_cap_v<T>) return false;  // parser would reject (TooMany)
      } else {
        count = static_cast<std::size_t>(array_count_v<T>);
      }
      for (std::size_t i = 0; i < count; ++i) {
        if (!guard()) return false;
        std::size_t b = pos;
        if (!scan_type<array_elem_t<T>>()) return false;
        ElemMark em;
        em.count_int = count_int;
        em.begin = b;
        em.end = pos;
        elems.push_back(em);
      }
      return true;

    } else if constexpr (is_choice_v<T>) {
      return [&]<auto TagRef, class... Cases>(Choice<TagRef, Cases...>*) -> bool {
        std::string t(TagRef.name.sv());
        if (auto it = last_int.find(t); it != last_int.end()) ints[it->second].role = 3;
        std::uint64_t tagv = values[t];
        return scan_cases<Cases...>(tagv);
      }(static_cast<T*>(nullptr));

    } else if constexpr (is_struct_v<T>) {
      return scan_struct<T>();

    } else {
      static_assert(sizeof(T) == 0, "fuzz::Scanner: unsupported schema type");
      return false;
    }
  }
};

inline std::size_t do_byte_flip(std::span<std::byte> buf, std::size_t size, std::size_t max_size,
                                Rng& rng) {
  last_op = MutOp::ByteFlip;
  if (size == 0) {
    if (max_size == 0) return 0;
    std::size_t hi = max_size < 8 ? max_size : 8;
    std::size_t n = 1 + static_cast<std::size_t>(rng.below(hi));
    if (n > max_size) n = max_size;
    for (std::size_t i = 0; i < n; ++i) buf[i] = static_cast<std::byte>(rng.next() & 0xFF);
    return n;
  }
  std::size_t flips = 1 + static_cast<std::size_t>(rng.below(3));
  for (std::size_t f = 0; f < flips; ++f) {
    std::size_t idx = static_cast<std::size_t>(rng.below(size));
    std::uint8_t x = static_cast<std::uint8_t>(1 + rng.below(255));  // nonzero: always changes a byte
    buf[idx] = static_cast<std::byte>(std::to_integer<unsigned>(buf[idx]) ^ x);
  }
  return size;
}

template <class S>
std::size_t apply_field_op(std::span<std::byte> buf, std::size_t size, std::size_t max_size,
                           Rng& rng, Scanner<S>& sc) {
  std::byte* d = buf.data();

  std::vector<std::size_t> len_marks, tag_marks;
  for (std::size_t i = 0; i < sc.ints.size(); ++i) {
    if (sc.ints[i].role == 1 || sc.ints[i].role == 2) len_marks.push_back(i);
    if (sc.ints[i].role == 3) tag_marks.push_back(i);
  }
  std::vector<std::size_t> trunc_pts;
  for (std::size_t b : sc.boundaries)
    if (b < size) trunc_pts.push_back(b);
  std::vector<std::size_t> dup_elems;
  for (std::size_t i = 0; i < sc.elems.size(); ++i) {
    const auto& e = sc.elems[i];
    std::size_t L = e.end - e.begin;
    if (L > 0 && e.end <= size && size + L <= max_size) dup_elems.push_back(i);
  }

  std::vector<int> ops;
  if (!len_marks.empty()) ops.push_back(1);
  if (!tag_marks.empty()) ops.push_back(2);
  if (!trunc_pts.empty()) ops.push_back(3);
  if (!dup_elems.empty()) ops.push_back(4);
  ops.push_back(5);  // byte flip is always available

  switch (ops[static_cast<std::size_t>(rng.below(ops.size()))]) {
    case 1: {  // grow/shrink a length or count
      const auto& m = sc.ints[len_marks[static_cast<std::size_t>(rng.below(len_marks.size()))]];
      std::uint64_t maxw = width_max(m.width);
      std::uint64_t cur = read_uint(d, m.offset, m.width, m.endian);
      std::uint64_t nv;
      switch (rng.below(4)) {
        case 0: nv = cur + 1 + rng.below(8); break;
        case 1: nv = cur > 0 ? cur - 1 : 0; break;
        case 2: nv = maxw; break;
        default: nv = rng.next() & maxw; break;
      }
      if (nv > maxw) nv = maxw;
      write_uint(d, m.offset, m.width, m.endian, nv);
      last_op = MutOp::LenChange;
      return size;
    }
    case 2: {  // swap a Choice tag
      const auto& m = sc.ints[tag_marks[static_cast<std::size_t>(rng.below(tag_marks.size()))]];
      std::uint64_t maxw = width_max(m.width);
      std::uint64_t cur = read_uint(d, m.offset, m.width, m.endian);
      std::uint64_t nv = rng.next() & maxw;
      if (nv == cur) nv = (nv ^ 1u) & maxw;
      write_uint(d, m.offset, m.width, m.endian, nv);
      last_op = MutOp::TagSwap;
      return size;
    }
    case 3: {  // truncate at a field boundary
      std::size_t b = trunc_pts[static_cast<std::size_t>(rng.below(trunc_pts.size()))];
      last_op = MutOp::Truncate;
      return b;
    }
    case 4: {  // duplicate an array element
      const auto& e = sc.elems[dup_elems[static_cast<std::size_t>(rng.below(dup_elems.size()))]];
      std::size_t L = e.end - e.begin;
      std::memmove(d + e.end + L, d + e.end, size - e.end);  // shift the tail right
      std::memmove(d + e.end, d + e.begin, L);               // copy the element into the gap
      if (e.count_int != static_cast<std::size_t>(-1)) {
        const auto& cm = sc.ints[e.count_int];
        std::uint64_t c = read_uint(d, cm.offset, cm.width, cm.endian);
        std::uint64_t mw = width_max(cm.width);
        if (c + 1 <= mw) write_uint(d, cm.offset, cm.width, cm.endian, c + 1);
      }
      last_op = MutOp::DupElem;
      return size + L;
    }
    default:
      return do_byte_flip(buf, size, max_size, rng);
  }
}

}  // namespace detail

// Mutate `buf[0,size)` in place against schema S. Returns the new size, never
// greater than max_size, never reading past `size` or writing past max_size.
template <class S>
std::size_t mutate(std::span<std::byte> buf, std::size_t size, std::size_t max_size,
                   std::uint32_t seed) noexcept {
  if (max_size == 0) {
    last_op = MutOp::ByteFlip;
    return 0;
  }
  if (max_size > buf.size()) max_size = buf.size();
  if (size > max_size) size = max_size;

  Rng rng{mix(seed, 0xF00Du)};
  detail::Scanner<S> sc(buf.data(), size);
  const bool ok = sc.template scan_struct<S>();
  if (ok && (!sc.ints.empty() || !sc.elems.empty()))
    return detail::apply_field_op<S>(buf, size, max_size, rng, sc);

  // The input did not scan (truncated, over-cap count, or unknown tag). Most of
  // the time, seed a freshly generated valid or violating structure so the
  // fuzzer escapes a shallow state instead of byte-flipping noise forever —
  // generation and mutation are two products of one schema walk. Only when the
  // generated instance does not fit max_size do we fall back to a byte flip.
  if (rng.below(4) != 0) {
    Generator<S> g(seed ^ 0x9E3779B9u);
    std::vector<std::byte> gen = (rng.next() & 1u) ? g.valid() : g.violating();
    if (!gen.empty() && gen.size() <= max_size) {
      for (std::size_t i = 0; i < gen.size(); ++i) buf[i] = gen[i];
      last_op = MutOp::GenSeed;
      return gen.size();
    }
  }
  return detail::do_byte_flip(buf, size, max_size, rng);
}

}  // namespace portcullis::fuzz

#endif  // PORTCULLIS_FUZZ_MUTATOR_HPP
