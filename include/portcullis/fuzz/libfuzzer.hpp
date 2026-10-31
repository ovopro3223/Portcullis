#ifndef PORTCULLIS_FUZZ_LIBFUZZER_HPP
#define PORTCULLIS_FUZZ_LIBFUZZER_HPP

// The libFuzzer hook. PORTCULLIS_FUZZ_TARGET(S) defines a complete fuzz target
// for the schema S: LLVMFuzzerTestOneInput drives parse<S> over the fuzzer's
// bytes — the claim under test is that parse never reads out of bounds, so the
// target is built with -fsanitize=fuzzer,address,undefined — and
// LLVMFuzzerCustomMutator drives the structure-aware mutate<S>, which reaches
// deep parser states far faster than byte-level mutation.
//
// One schema, one line:  PORTCULLIS_FUZZ_TARGET(portcullis::schemas::Heartbeat)

#include <cstddef>
#include <cstdint>
#include <span>

#include "portcullis/core/untrusted.hpp"
#include "portcullis/fuzz/mutator.hpp"
#include "portcullis/parse/parse.hpp"

namespace portcullis::fuzz {

template <class S>
inline int run_one_input(const std::uint8_t* data, std::size_t size) noexcept {
  Untrusted in(std::span<const std::byte>(reinterpret_cast<const std::byte*>(data), size));
  auto result = parse<S>(in);
  (void)result;  // the point is that parse never reads out of bounds, not the value
  return 0;
}

template <class S>
inline std::size_t run_custom_mutator(std::uint8_t* data, std::size_t size, std::size_t max_size,
                                      unsigned int seed) noexcept {
  return mutate<S>(std::span<std::byte>(reinterpret_cast<std::byte*>(data), max_size), size,
                   max_size, static_cast<std::uint32_t>(seed));
}

}  // namespace portcullis::fuzz

#define PORTCULLIS_FUZZ_TARGET(S)                                                        \
  extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {    \
    return ::portcullis::fuzz::run_one_input<S>(data, size);                             \
  }                                                                                      \
  extern "C" std::size_t LLVMFuzzerCustomMutator(std::uint8_t* data, std::size_t size,   \
                                                 std::size_t max_size, unsigned seed) {  \
    return ::portcullis::fuzz::run_custom_mutator<S>(data, size, max_size, seed);        \
  }

#endif  // PORTCULLIS_FUZZ_LIBFUZZER_HPP
