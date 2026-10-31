#ifndef PORTCULLIS_FUZZ_GENERATOR_HPP
#define PORTCULLIS_FUZZ_GENERATOR_HPP

// Structure-aware generation. Generator<S> walks the schema S with the SAME
// `for_each_field` the parser uses — there is no second description of the
// format here. From one walk it produces two things:
//
//   valid()     — bytes that always parse<S> successfully, biased to boundaries
//                 (lengths 0, array counts 0 and the cap, every Choice case).
//   violating() — bytes that fail parse<S> with exactly one contract broken;
//                 over many seeds every ErrorKind the schema can produce appears.
//
// References (Bytes<Ref>, Array<Ref>, Choice<Tag>) are resolved by name through
// a value map built as the walk proceeds, so a generated length/count/tag is
// always consistent with the bytes that follow it. Refinements cannot be
// inverted (design 6); valid() keeps free fields small so range refines pass by
// construction and verifies with parse<S>, and violating() generates candidates
// and lets the contract reject them.

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "portcullis/core/untrusted.hpp"
#include "portcullis/parse/parse.hpp"
#include "portcullis/parse/value_of.hpp"
#include "portcullis/schema/combinators.hpp"
#include "portcullis/schema/primitives.hpp"
#include "portcullis/schema/ref.hpp"
#include "portcullis/visit/for_each_field.hpp"

namespace portcullis::fuzz {

// A small deterministic PRNG (splitmix64). The fuzzer never needs a strong
// generator, only a reproducible one: the same seed must give the same bytes.
inline std::uint64_t splitmix64(std::uint64_t& s) noexcept {
  s += 0x9E3779B97F4A7C15ull;
  std::uint64_t z = s;
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  return z ^ (z >> 31);
}

struct Rng {
  std::uint64_t state;
  explicit Rng(std::uint64_t seed) noexcept : state(seed) {}
  std::uint64_t next() noexcept { return splitmix64(state); }
  // A value in [0, n); 0 when n == 0.
  std::uint64_t below(std::uint64_t n) noexcept { return n == 0 ? 0 : next() % n; }
};

inline std::uint64_t mix(std::uint64_t a, std::uint64_t b) noexcept {
  std::uint64_t s = a * 0x9E3779B97F4A7C15ull + b + 0x12345678ull;
  return splitmix64(s);
}

namespace detail {

// kLenHi caps the byte-length a generated Ref length takes, so a valid instance
// stays small (array counts use their own schema cap instead).
inline constexpr std::uint64_t kLenHi = 24;

// The role every referenced field plays, gathered once per schema by a walk
// that mirrors for_each_field and recurses through nested structs, array
// elements and choice cases. Everything is keyed by field name, the same way
// the parser resolves a Ref.
struct Roles {
  std::set<std::string> length_refs;                             // Bytes<Ref("f")>
  std::map<std::string, std::size_t> count_refs;                 // Array<_,Ref("f"),Cap>
  std::map<std::string, std::vector<std::uint64_t>> tag_cases;   // Choice<Tag("f"),...>
  bool has_refine = false;
};

template <class S>
void collect_struct(Roles& r);

template <class T>
void collect_type(Roles& r) {
  if constexpr (is_bytes_v<T>) {
    if constexpr (bytes_len_is_ref_v<T>) {
      r.length_refs.insert(std::string(bytes_len_v<T>.name.sv()));
    }
  } else if constexpr (is_array_v<T>) {
    if constexpr (array_count_is_ref_v<T>) {
      r.count_refs[std::string(array_count_v<T>.name.sv())] = array_cap_v<T>;
    }
    collect_type<array_elem_t<T>>(r);
  } else if constexpr (is_choice_v<T>) {
    [&]<auto TagRef, class... Cases>(Choice<TagRef, Cases...>*) {
      auto& vec = r.tag_cases[std::string(TagRef.name.sv())];
      (vec.push_back(static_cast<std::uint64_t>(portcullis::detail::case_value_type<Cases>::value)),
       ...);
      (collect_type<typename portcullis::detail::case_value_type<Cases>::type>(r), ...);
    }(static_cast<T*>(nullptr));
  } else if constexpr (is_struct_v<T>) {
    collect_struct<T>(r);
  }
  // Primitive and Bytes<N>: nothing to reference.
}

template <class S>
void collect_struct(Roles& r) {
  for_each_field<S>([&](auto fi) {
    using T = typename decltype(fi)::type;
    collect_type<T>(r);
  });
}

// has_refine_v<S>: does S, or anything nested inside it, carry a Refine? The
// fuzzer only attempts a RefinementFailed violation when the answer is yes.
template <class T>
consteval bool type_has_refine();

template <class Item>
consteval bool item_has_refine() {
  if constexpr (is_refine_v<Item>) {
    return true;
  } else if constexpr (is_field_v<Item>) {
    return type_has_refine<typename Item::type>();
  } else {
    return false;
  }
}

template <class... Items>
consteval bool struct_items_refine(Struct<Items...>*) {
  return (false || ... || item_has_refine<Items>());
}

template <class S>
consteval bool struct_has_refine() {
  return struct_items_refine(static_cast<S*>(nullptr));
}

template <class T>
consteval bool type_has_refine() {
  if constexpr (is_struct_v<T>) {
    return struct_has_refine<T>();
  } else if constexpr (is_array_v<T>) {
    return type_has_refine<array_elem_t<T>>();
  } else if constexpr (is_choice_v<T>) {
    return []<auto TagRef, class... Cases>(Choice<TagRef, Cases...>*) {
      return (false || ... ||
              type_has_refine<typename portcullis::detail::case_value_type<Cases>::type>());
    }(static_cast<T*>(nullptr));
  } else {
    return false;
  }
}

template <class S>
inline constexpr bool has_refine_v = struct_has_refine<S>();

// What kind of output the emitter is building. Valid keeps everything in range;
// each violation injects exactly one defect into an otherwise valid instance.
enum class VMode { Valid, Truncated, BadTag, TooMany, RefineFail };

struct Plan {
  VMode mode = VMode::Valid;
  bool free_wide = false;          // draw free (unreferenced) fields full-range
  bool force_nonzero_count = false;  // a BadTag needs at least one array element
};

// The walker that turns a schema into bytes. It writes integer fields with a
// "buffer value" (what lands in the output) that may differ from the "logical
// value" used to drive the structure that follows, which is how one defect is
// injected without disturbing anything else: a bad tag byte while the body is
// still a real case, or an over-cap count byte while zero elements are emitted.
template <class Top>
struct Emitter {
  std::vector<std::byte>& out;
  Rng& rng;
  const Roles& roles;
  const Plan& plan;
  std::map<std::string, std::uint64_t> logical{};
  bool bad_tag_done = false;
  bool too_many_done = false;

  Emitter(std::vector<std::byte>& out_, Rng& rng_, const Roles& roles_, const Plan& plan_) noexcept
      : out(out_), rng(rng_), roles(roles_), plan(plan_) {}

  void put_uint(std::uint64_t v, std::size_t width, Endian e) {
    if (e == Endian::Big) {
      for (std::size_t i = 0; i < width; ++i)
        out.push_back(static_cast<std::byte>((v >> (8 * (width - 1 - i))) & 0xFF));
    } else {
      for (std::size_t i = 0; i < width; ++i)
        out.push_back(static_cast<std::byte>((v >> (8 * i)) & 0xFF));
    }
  }

  std::uint64_t width_max(std::size_t width) const {
    return width >= 8 ? ~0ull : ((std::uint64_t(1) << (8 * width)) - 1);
  }

  // A value in [0, hi] biased toward the boundaries 0 and hi.
  std::uint64_t boundary(std::uint64_t hi) {
    switch (rng.below(6)) {
      case 0: return 0;
      case 1: return hi;
      case 2: return hi > 0 ? 1 : 0;
      default: return rng.below(hi + 1);
    }
  }

  static std::uint64_t first_not_in(const std::vector<std::uint64_t>& vals, std::uint64_t hi) {
    for (std::uint64_t c = 0; c <= hi; ++c) {
      bool present = false;
      for (std::uint64_t v : vals)
        if (v == c) { present = true; break; }
      if (!present) return c;
    }
    return hi;  // unreachable when |vals| <= hi
  }

  // Returns {buffer value, logical value} for a primitive field named `name`.
  std::pair<std::uint64_t, std::uint64_t> pick_primitive(const std::string& name,
                                                         std::size_t width) {
    const std::uint64_t maxw = width_max(width);

    if (auto it = roles.tag_cases.find(name); it != roles.tag_cases.end()) {
      const auto& cv = it->second;
      std::uint64_t logv = cv.empty() ? 0 : cv[static_cast<std::size_t>(rng.below(cv.size()))];
      std::uint64_t bufv = logv;
      if (plan.mode == VMode::BadTag && !bad_tag_done) {
        bufv = first_not_in(cv, maxw);
        bad_tag_done = true;
      }
      return {bufv, logv};
    }

    if (auto it = roles.count_refs.find(name); it != roles.count_refs.end()) {
      const std::size_t cap = it->second;
      std::uint64_t logv = boundary(static_cast<std::uint64_t>(cap));
      if (plan.force_nonzero_count && logv == 0) logv = 1;
      std::uint64_t bufv = logv;
      if (plan.mode == VMode::TooMany && !too_many_done) {
        const std::uint64_t lo = static_cast<std::uint64_t>(cap) + 1;
        bufv = (maxw >= lo) ? (lo + rng.below(maxw - lo + 1)) : maxw;
        logv = 0;  // the parser rejects at the count; emit no elements
        too_many_done = true;
      }
      return {bufv, logv};
    }

    if (roles.length_refs.count(name)) {
      std::uint64_t v = boundary(kLenHi);
      if (v > maxw) v = maxw;
      return {v, v};
    }

    // A free field: small in valid mode (so range refines pass), full-range
    // when we are trying to trip a refine.
    std::uint64_t v = plan.free_wide ? (rng.next() & maxw) : rng.below(64);
    return {v, v};
  }

  template <class E>
  void emit_type() {
    if constexpr (is_struct_v<E>) {
      emit_struct<E>();
    } else {
      emit_field<E>(std::string{});  // anonymous array element
    }
  }

  template <class C0, class... Cs>
  void emit_cases(std::uint64_t tagv) {
    if (static_cast<std::uint64_t>(portcullis::detail::case_value_type<C0>::value) == tagv) {
      emit_type<typename portcullis::detail::case_value_type<C0>::type>();
      return;
    }
    if constexpr (sizeof...(Cs) > 0) emit_cases<Cs...>(tagv);
  }

  template <class T>
  void emit_choice() {
    [&]<auto TagRef, class... Cases>(Choice<TagRef, Cases...>*) {
      const std::uint64_t tagv = logical[std::string(TagRef.name.sv())];
      emit_cases<Cases...>(tagv);
    }(static_cast<T*>(nullptr));
  }

  template <class T>
  void emit_field(const std::string& name) {
    if constexpr (Primitive<T>) {
      auto [bufv, logv] = pick_primitive(name, T::width);
      if (!name.empty()) logical[name] = logv;
      put_uint(bufv, T::width, T::endian);

    } else if constexpr (is_bytes_v<T>) {
      std::size_t n;
      if constexpr (bytes_len_is_ref_v<T>) {
        n = static_cast<std::size_t>(logical[std::string(bytes_len_v<T>.name.sv())]);
      } else {
        n = static_cast<std::size_t>(bytes_len_v<T>);
      }
      for (std::size_t i = 0; i < n; ++i) out.push_back(static_cast<std::byte>(rng.next() & 0xFF));

    } else if constexpr (is_array_v<T>) {
      std::size_t count;
      if constexpr (array_count_is_ref_v<T>) {
        count = static_cast<std::size_t>(logical[std::string(array_count_v<T>.name.sv())]);
      } else {
        count = static_cast<std::size_t>(array_count_v<T>);
      }
      for (std::size_t i = 0; i < count; ++i) emit_type<array_elem_t<T>>();

    } else if constexpr (is_choice_v<T>) {
      emit_choice<T>();

    } else if constexpr (is_struct_v<T>) {
      emit_struct<T>();

    } else {
      static_assert(sizeof(T) == 0, "fuzz::Emitter: unsupported schema type");
    }
  }

  template <class S>
  void emit_struct() {
    for_each_field<S>([&](auto fi) {
      using T = typename decltype(fi)::type;
      constexpr auto nm = decltype(fi)::name;
      emit_field<T>(std::string(nm.sv()));
    });
  }
};

inline bool kind_matches(ErrorKind got, VMode want) {
  switch (want) {
    case VMode::Truncated: return got == ErrorKind::Truncated;
    case VMode::BadTag: return got == ErrorKind::BadTag;
    case VMode::TooMany: return got == ErrorKind::TooManyElements;
    case VMode::RefineFail: return got == ErrorKind::RefinementFailed;
    case VMode::Valid: return false;
  }
  return false;
}

}  // namespace detail

template <class S>
class Generator {
 public:
  explicit Generator(std::uint64_t seed) noexcept : seed_(seed) {}

  // Bytes that parse<S> accepts. Structure is correct by construction; the only
  // thing that can reject is a refine, so valid mode keeps free fields small and
  // re-rolls (deterministically) on the rare miss. Deterministic per seed.
  std::vector<std::byte> valid() {
    detail::Roles roles;
    detail::collect_struct<S>(roles);
    std::vector<std::byte> best;
    for (int attempt = 0; attempt < 256; ++attempt) {
      detail::Plan plan;  // Valid
      std::vector<std::byte> out = build(roles, plan, mix(seed_, 0xA000u + attempt));
      if (parses(out)) return out;
      if (attempt == 0) best = std::move(out);
    }
    return best;
  }

  // Bytes that parse<S> rejects with exactly one contract broken. The violation
  // kind is fixed per seed (so coverage spreads across seeds); values re-roll
  // until the intended rejection appears. Deterministic per seed.
  std::vector<std::byte> violating() {
    detail::Roles roles;
    detail::collect_struct<S>(roles);
    roles.has_refine = detail::has_refine_v<S>;

    std::vector<detail::VMode> kinds;
    kinds.push_back(detail::VMode::Truncated);
    if (!roles.tag_cases.empty()) kinds.push_back(detail::VMode::BadTag);
    if (!roles.count_refs.empty()) kinds.push_back(detail::VMode::TooMany);
    if (roles.has_refine) kinds.push_back(detail::VMode::RefineFail);
    const detail::VMode kind =
        kinds[static_cast<std::size_t>(mix(seed_, 0xB1u) % kinds.size())];

    std::vector<std::byte> best;
    for (int attempt = 0; attempt < 512; ++attempt) {
      detail::Plan plan;
      plan.mode = kind;
      plan.free_wide = (kind == detail::VMode::RefineFail);
      plan.force_nonzero_count = (kind == detail::VMode::BadTag);

      std::vector<std::byte> out = build(roles, plan, mix(seed_, 0xB000u + attempt));
      if (kind == detail::VMode::Truncated && !out.empty()) {
        const std::size_t cut = static_cast<std::size_t>(mix(seed_, 0xC000u + attempt) % out.size());
        out.resize(cut);  // [0, full)
      }
      if (attempt == 0) best = out;

      Untrusted in(std::span<const std::byte>(out.data(), out.size()));
      auto r = parse<S>(in);
      if (!r && detail::kind_matches(r.error().kind, kind)) return out;
    }
    return best;
  }

 private:
  std::uint64_t seed_;

  std::vector<std::byte> build(const detail::Roles& roles, const detail::Plan& plan,
                               std::uint64_t subseed) {
    std::vector<std::byte> out;
    Rng rng{subseed};
    detail::Emitter<S> em{out, rng, roles, plan};
    em.template emit_struct<S>();
    return out;
  }

  static bool parses(const std::vector<std::byte>& out) {
    Untrusted in(std::span<const std::byte>(out.data(), out.size()));
    return static_cast<bool>(parse<S>(in));
  }
};

}  // namespace portcullis::fuzz

#endif  // PORTCULLIS_FUZZ_GENERATOR_HPP
