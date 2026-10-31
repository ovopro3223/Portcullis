// Task 8.2 — the MTE fault test. On ARM hardware (or QEMU) with MTE active, an
// access past an allocation must fault synchronously. We prove it the only way
// that is safe for a test runner: fork a child, make it commit the overflow, and
// assert in the parent that the child died with SIGSEGV. On any host without MTE
// the test is reported as skipped. The fork machinery is POSIX-only, so on
// non-POSIX platforms (e.g. MinGW) the case compiles to a skip as well.
#include "third_party/doctest.h"

#include <cstddef>
#include <cstring>

#include <portcullis/arena/tagged_arena.hpp>

using portcullis::arena::TaggedArena;

#if defined(__linux__)
#include <sys/wait.h>
#include <unistd.h>

namespace {
// Runs `fn` in a forked child and returns the child's raw wait status.
template <class F>
int run_in_child(F&& fn) {
  pid_t pid = ::fork();
  REQUIRE(pid >= 0);
  if (pid == 0) {
    fn();
    // If the child survives the overflow, exit 0 so the parent sees NO signal
    // and the test fails loudly rather than hanging.
    _exit(0);
  }
  int status = 0;
  REQUIRE(::waitpid(pid, &status, 0) == pid);
  return status;
}
}  // namespace
#endif

TEST_CASE("overflow_past_allocation_faults") {
  if (!TaggedArena::mte_available()) {
    MESSAGE("SKIPPED: MTE not available on this host (needs aarch64 + MTE)");
    return;
  }
#if defined(__linux__)
  int status = run_in_child([] {
    TaggedArena arena(4096);
    volatile unsigned char* p =
        static_cast<volatile unsigned char*>(arena.allocate(32, 16));
    if (p == nullptr) _exit(0);  // would make the parent's SIGSEGV check fail
    // One full granule past the 32-byte allocation: offset 48 lives in a granule
    // that carries a different tag, so a synchronous MTE check must fault here.
    p[48] = 0x41;
    // Touch it back so the compiler cannot elide the store.
    (void)p[48];
  });
  CHECK(WIFSIGNALED(status));
  if (WIFSIGNALED(status)) {
    CHECK(WTERMSIG(status) == SIGSEGV);
  }
#else
  MESSAGE("SKIPPED: fork-based fault test requires a POSIX host");
#endif
}
