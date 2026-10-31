#ifndef PORTCULLIS_ARENA_TAGGED_ARENA_HPP
#define PORTCULLIS_ARENA_TAGGED_ARENA_HPP

// portcullis::arena::TaggedArena — an optional, experimental bump arena that
// holds validated parse output. On ARM hardware with the Memory Tagging
// Extension (MTE), each allocation is given a random tag and its granules are
// painted with it, so any out-of-bounds access — including from code that never
// went through this library — faults synchronously. Everywhere else the same
// API is a plain portable bump arena.
//
// `materialize<S>(parsed, arena)` deep-copies a Parsed<S>: integer fields by
// value and the bytes behind every span into the arena, so the result survives
// the input buffer being freed or overwritten.
//
// Documented limits (spec §7, §11): 16-byte granule (an overflow that stays
// inside the last granule of an allocation is NOT caught); heap only; detection
// is by fault, not prevention; MTE path is Linux/Android + aarch64 only.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <optional>
#include <span>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "portcullis/parse/parsed.hpp"
#include "portcullis/parse/value_of.hpp"
#include "portcullis/schema/combinators.hpp"
#include "portcullis/visit/for_each_field.hpp"

// Runtime MTE detection is available on any aarch64 Linux host (HWCAP2_MTE).
#if defined(__aarch64__) && defined(__linux__)
#include <sys/auxv.h>
#ifndef HWCAP2_MTE
#define HWCAP2_MTE (1UL << 18)
#endif
#endif

// The tag-painting path needs the MTE intrinsics (compiled in only with
// -march=...+memtag) plus the Linux mmap/prctl ABI.
#if defined(__aarch64__) && defined(__ARM_FEATURE_MEMORY_TAGGING) && defined(__linux__)
#define PORTCULLIS_ARENA_MTE 1
#else
#define PORTCULLIS_ARENA_MTE 0
#endif

#if PORTCULLIS_ARENA_MTE
#include <arm_acle.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#ifndef PROT_MTE
#define PROT_MTE 0x20
#endif
#ifndef PR_SET_TAGGED_ADDR_CTRL
#define PR_SET_TAGGED_ADDR_CTRL 55
#endif
#ifndef PR_TAGGED_ADDR_ENABLE
#define PR_TAGGED_ADDR_ENABLE (1UL << 0)
#endif
#ifndef PR_MTE_TCF_SYNC
#define PR_MTE_TCF_SYNC (1UL << 1)
#endif
#ifndef PR_MTE_TAG_SHIFT
#define PR_MTE_TAG_SHIFT 3
#endif
#endif  // PORTCULLIS_ARENA_MTE

namespace portcullis::arena {

// The MTE tag granule: tags apply to aligned 16-byte chunks.
inline constexpr std::size_t kGranule = 16;
// The portable backing buffer is page-aligned so allocate() can satisfy any
// alignment a materialized value needs, matching the mmap path.
inline constexpr std::size_t kBaseAlign = 4096;

class TaggedArena {
 public:
  explicit TaggedArena(std::size_t capacity) : capacity_(capacity) {
#if PORTCULLIS_ARENA_MTE
    if (mte_available()) {
      void* m = ::mmap(nullptr, capacity_ ? capacity_ : 1,
                       PROT_READ | PROT_WRITE | PROT_MTE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
      if (m != MAP_FAILED) {
        // Enable the tagged-address ABI with synchronous tag-check faults for
        // this thread, and exclude tag 0 from the random set so an allocation's
        // tag always differs from untouched (tag-0) memory around it.
        if (::prctl(PR_SET_TAGGED_ADDR_CTRL,
                    PR_TAGGED_ADDR_ENABLE | PR_MTE_TCF_SYNC |
                        (0xfffeUL << PR_MTE_TAG_SHIFT),
                    0, 0, 0) == 0) {
          base_ = static_cast<std::byte*>(m);
          mmap_len_ = capacity_ ? capacity_ : 1;
          tagging_ = true;
        } else {
          ::munmap(m, capacity_ ? capacity_ : 1);
        }
      }
    }
#endif
    if (base_ == nullptr) {
      base_ = static_cast<std::byte*>(
          ::operator new(capacity_ ? capacity_ : 1, std::align_val_t(kBaseAlign)));
    }
  }

  ~TaggedArena() {
#if PORTCULLIS_ARENA_MTE
    if (mmap_len_ != 0) {
      ::munmap(base_, mmap_len_);
      return;
    }
#endif
    ::operator delete(base_, std::align_val_t(kBaseAlign));
  }

  TaggedArena(const TaggedArena&) = delete;
  TaggedArena& operator=(const TaggedArena&) = delete;

  // Carve `bytes` of storage aligned to `align`. Returns nullptr when the arena
  // cannot satisfy the request — never UB, never a partial allocation. On the
  // MTE path the returned pointer carries the allocation's random tag.
  void* allocate(std::size_t bytes, std::size_t align) noexcept {
    if (base_ == nullptr) return nullptr;
    if (align == 0) align = 1;
    const std::size_t granule = tagging_ ? kGranule : 1;
    if (align < granule) align = granule;
    if (bytes == 0) bytes = granule;
    const std::size_t need = tagging_ ? round_up(bytes, kGranule) : bytes;

    const std::uintptr_t base_addr = reinterpret_cast<std::uintptr_t>(base_);
    const std::uintptr_t cur = base_addr + offset_;
    const std::uintptr_t aligned =
        (cur + (static_cast<std::uintptr_t>(align) - 1)) &
        ~(static_cast<std::uintptr_t>(align) - 1);
    const std::size_t pad = static_cast<std::size_t>(aligned - base_addr) - offset_;

    if (pad > capacity_ - offset_) return nullptr;
    const std::size_t new_off = offset_ + pad;
    if (need > capacity_ - new_off) return nullptr;

    void* p = reinterpret_cast<void*>(aligned);
    offset_ = new_off + need;
#if PORTCULLIS_ARENA_MTE
    if (tagging_) p = paint_tag(p, need);
#endif
    return p;
  }

  // True on an aarch64 Linux host whose CPU advertises MTE (HWCAP2_MTE).
  static bool mte_available() noexcept {
#if defined(__aarch64__) && defined(__linux__)
    return (::getauxval(AT_HWCAP2) & HWCAP2_MTE) != 0;
#else
    return false;
#endif
  }

  // True when this arena is actually painting tags (MTE compiled in, available
  // at runtime, and the mmap + prctl setup succeeded).
  bool tagging_active() const noexcept { return tagging_; }

 private:
  static constexpr std::size_t round_up(std::size_t n, std::size_t to) noexcept {
    return (n + (to - 1)) & ~(to - 1);
  }

#if PORTCULLIS_ARENA_MTE
  // Give [p, p+size) a fresh random tag (one per allocation), painting every
  // 16-byte granule, and return the pointer carrying that tag.
  static void* paint_tag(void* p, std::size_t size) noexcept {
    void* tagged = __arm_mte_create_random_tag(p, 0);
    for (std::size_t off = 0; off < size; off += kGranule) {
      __arm_mte_set_tag(static_cast<char*>(tagged) + off);
    }
    return tagged;
  }
#endif

  std::byte* base_ = nullptr;
  std::size_t capacity_ = 0;
  std::size_t offset_ = 0;
  bool tagging_ = false;
#if PORTCULLIS_ARENA_MTE
  std::size_t mmap_len_ = 0;
#endif
};

namespace detail {

// --- value-type classification (the shapes value_of_t can produce) ---
template <class T>
inline constexpr bool is_byte_span_v = false;
template <>
inline constexpr bool is_byte_span_v<std::span<const std::byte>> = true;

template <class T>
inline constexpr bool is_parsed_v = false;
template <class S>
inline constexpr bool is_parsed_v<Parsed<S>> = true;

template <class T>
inline constexpr bool is_vector_v = false;
template <class U, class A>
inline constexpr bool is_vector_v<std::vector<U, A>> = true;

template <class T>
inline constexpr bool is_optional_v = false;
template <class U>
inline constexpr bool is_optional_v<std::optional<U>> = true;

template <class T>
inline constexpr bool is_variant_v = false;
template <class... U>
inline constexpr bool is_variant_v<std::variant<U...>> = true;

// --- field name at a given field index of schema S ---
template <std::size_t I, class F0, class... Fs>
struct nth_name {
  static constexpr auto value = nth_name<I - 1, Fs...>::value;
};
template <class F0, class... Fs>
struct nth_name<0, F0, Fs...> {
  static constexpr auto value = F0::name;
};
template <std::size_t I, class L>
struct name_at_list;
template <std::size_t I, class... Fs>
struct name_at_list<I, portcullis::detail::type_list<Fs...>> {
  static constexpr auto value = nth_name<I, Fs...>::value;
};
template <class S, std::size_t I>
inline constexpr auto field_name_v =
    name_at_list<I, portcullis::detail::fields_of_t<S>>::value;

template <class V>
V relocate_value(const V& v, TaggedArena& a, bool& ok);

// Copy the bytes behind a span into the arena and return a span over the copy.
// On exhaustion, sets ok=false and returns an empty span.
inline std::span<const std::byte> relocate_span(std::span<const std::byte> s,
                                                 TaggedArena& a, bool& ok) {
  if (!ok) return {};
  if (s.empty()) return {};
  void* mem = a.allocate(s.size(), 1);
  if (mem == nullptr) {
    ok = false;
    return {};
  }
  std::memcpy(mem, s.data(), s.size());
  return std::span<const std::byte>(static_cast<const std::byte*>(mem), s.size());
}

template <class S, std::size_t... Is>
typename Parsed<S>::Fields relocate_fields(const Parsed<S>& p, TaggedArena& a,
                                           bool& ok, std::index_sequence<Is...>) {
  return typename Parsed<S>::Fields{
      relocate_value(p.template get<field_name_v<S, Is>>(), a, ok)...};
}

template <class S>
Parsed<S> relocate_parsed_value(const Parsed<S>& p, TaggedArena& a, bool& ok) {
  auto fields = relocate_fields<S>(p, a, ok,
                                   std::make_index_sequence<field_count_v<S>>{});
  auto bytes = relocate_span(p.bytes(), a, ok);
  return Parsed<S>(std::move(fields), bytes);
}

template <class V>
V relocate_value(const V& v, TaggedArena& a, bool& ok) {
  if constexpr (is_byte_span_v<V>) {
    return relocate_span(v, a, ok);
  } else if constexpr (is_parsed_v<V>) {
    return relocate_parsed_value(v, a, ok);
  } else if constexpr (is_vector_v<V>) {
    V out;
    out.reserve(v.size());
    for (const auto& e : v) out.push_back(relocate_value(e, a, ok));
    return out;
  } else if constexpr (is_optional_v<V>) {
    if (v.has_value()) return V(relocate_value(*v, a, ok));
    return V{};
  } else if constexpr (is_variant_v<V>) {
    return std::visit(
        [&](const auto& alt) -> V { return V(relocate_value(alt, a, ok)); }, v);
  } else {
    // Arithmetic / enum field: copied by value.
    return v;
  }
}

}  // namespace detail

// Deep-copy a Parsed<S> into the arena: integers by value, the bytes behind
// every span (recursively, through nested structs, arrays, optionals and
// choices) into tagged arena storage. The returned pointer and all its spans
// are independent of the original input buffer. Returns nullptr if the arena is
// exhausted at any point (never UB, never a partial object).
template <class S>
Parsed<S>* materialize(const Parsed<S>& p, TaggedArena& a) noexcept {
  bool ok = true;
  Parsed<S> copy = detail::relocate_parsed_value(p, a, ok);
  if (!ok) return nullptr;
  void* mem = a.allocate(sizeof(Parsed<S>), alignof(Parsed<S>));
  if (mem == nullptr) return nullptr;
  return ::new (mem) Parsed<S>(std::move(copy));
}

}  // namespace portcullis::arena

#endif  // PORTCULLIS_ARENA_TAGGED_ARENA_HPP
