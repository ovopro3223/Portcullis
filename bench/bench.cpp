// Throughput runner for BENCHMARKS.md.
//
// For each (format, parser) it parses one fixed sample repeatedly and prints
// one JSON line on stdout:
//
//   {"format":"png_chunks","parser":"handwritten","mbps":...,"ns_per_parse":...,
//    "bytes":...,"sample":"...","iterations":2000,"batch":...,"optimized":true}
//
// Method. A single parse is far below the resolution of a clock read, so one
// "iteration" is a timed batch of `batch` back-to-back parses. `batch` is
// calibrated once per row so that a batch takes at least ~100 microseconds;
// the row's number is the MEDIAN over `iterations` batches (default 2000), so
// a scheduler hiccup or a cold cache in one batch does not move it. MB/s is
// sample bytes / median time per parse, with 1 MB = 10^6 bytes. Parses run
// back to back with no data dependency between them, so this is throughput,
// not single-parse latency.
//
// What the number means. These parsers validate structure; they do not touch
// payload bytes (a PNG IDAT, a heartbeat payload, a TIFF value area). MB/s
// therefore scales with how much of the sample is payload, and is meaningful
// only as a ratio between parsers on the same sample. ns_per_parse is the
// figure to read when comparing across formats.
//
// Adding a parser. Add one `emit(measure(sample, "<name>", n, <callable>))`
// call in main() next to the others, where the callable is
// `bool(const uint8_t*, size_t)`: it parses the sample and returns whether it
// was accepted. scripts/gen-benchmarks.py turns any number of parser rows per
// format into BENCHMARKS.md; it needs no change. Every parser is checked to
// accept its sample before it is timed; one that rejects it aborts the run,
// because timing a rejection measures nothing.
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "handwritten/png_chunks.hpp"
#include "handwritten/tiff_ifd.hpp"
#include "handwritten/tls_heartbeat.hpp"
#include "portcullis_schemas.hpp"
#include "samples.hpp"

namespace {

// Sample shapes. Chosen to look like real traffic, not to flatter anyone.
constexpr std::size_t kTlsPayloadBytes = 64;  // a heartbeat with a small payload
constexpr std::size_t kPngIdatChunks = 8;     // 64 KiB of image data ...
constexpr std::size_t kPngIdatBytes = 8192;   // ... in 8 KiB IDAT chunks
constexpr std::size_t kTiffEntries = 24;      // a camera-style IFD

constexpr std::size_t kDefaultIterations = 2000;
constexpr double kMinBatchNs = 100000.0;

// ---------------------------------------------------------------------------
// Optimisation barriers. Without them the compiler may notice that the input
// never changes, parse once, and hoist or delete the rest of the loop.

#if defined(__GNUC__) || defined(__clang__)
inline void escape(const void* p) { asm volatile("" : : "r"(p) : "memory"); }
inline void keep(bool v) { asm volatile("" : : "r"(v)); }
#if defined(__OPTIMIZE__)
constexpr bool kOptimized = true;
#else
constexpr bool kOptimized = false;
#endif
#else
volatile const void* g_sink;
inline void escape(const void* p) { g_sink = p; }
inline void keep(bool v) { g_sink = v ? &g_sink : nullptr; }
constexpr bool kOptimized = true;  // cannot tell; the CMake target sets optimising flags
#endif

// One input and the words that describe it in the report.
struct Sample {
  const char* format;
  samples::Bytes bytes;
  std::string description;
};

struct Row {
  const Sample* sample;
  const char* parser;
  std::size_t iterations;
  std::size_t batch;
  double ns_per_parse;
  double mbps;
};

using Clock = std::chrono::steady_clock;

template <class Parse>
double time_batch_ns(const std::uint8_t* data, std::size_t size, std::size_t batch,
                     Parse& parse) {
  const auto t0 = Clock::now();
  for (std::size_t i = 0; i < batch; ++i) {
    escape(data);  // the bytes may have changed: forces a fresh parse each time
    keep(parse(data, size));
  }
  const auto t1 = Clock::now();
  return std::chrono::duration<double, std::nano>(t1 - t0).count();
}

template <class Parse>
Row measure(const Sample& sample, const char* parser, std::size_t iterations, Parse parse) {
  const std::uint8_t* data = sample.bytes.data();
  const std::size_t size = sample.bytes.size();

  if (!parse(data, size)) {
    std::fprintf(stderr, "bench: %s/%s rejects its own sample; refusing to time a rejection\n",
                 sample.format, parser);
    std::exit(2);
  }

  std::size_t batch = 1;
  while (time_batch_ns(data, size, batch, parse) < kMinBatchNs &&
         batch < (std::size_t{1} << 26)) {
    batch *= 2;
  }
  for (int i = 0; i < 100; ++i) (void)time_batch_ns(data, size, batch, parse);  // warm up

  std::vector<double> ns(iterations);
  for (double& v : ns) v = time_batch_ns(data, size, batch, parse) / static_cast<double>(batch);
  std::nth_element(ns.begin(), ns.begin() + static_cast<std::ptrdiff_t>(ns.size() / 2), ns.end());
  const double median = ns[ns.size() / 2];

  return Row{&sample, parser, iterations, batch, median,
             static_cast<double>(size) / median * 1e3};
}

// Descriptions are plain ASCII without quotes or backslashes, so they need no
// JSON escaping.
void emit(const Row& r) {
  std::printf(
      "{\"format\":\"%s\",\"parser\":\"%s\",\"mbps\":%.2f,\"ns_per_parse\":%.3f,"
      "\"bytes\":%zu,\"sample\":\"%s\",\"iterations\":%zu,\"batch\":%zu,\"optimized\":%s}\n",
      r.sample->format, r.parser, r.mbps, r.ns_per_parse, r.sample->bytes.size(),
      r.sample->description.c_str(), r.iterations, r.batch, kOptimized ? "true" : "false");
  std::fflush(stdout);
}

std::size_t parse_iterations(int argc, char** argv) {
  std::size_t n = kDefaultIterations;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--iterations") == 0 && i + 1 < argc) {
      const long v = std::strtol(argv[++i], nullptr, 10);
      if (v < 1) {
        std::fprintf(stderr, "bench: --iterations needs a positive integer\n");
        std::exit(64);
      }
      n = static_cast<std::size_t>(v);
    } else {
      std::fprintf(stderr, "usage: portcullis_bench [--iterations N]   (default %zu)\n",
                   kDefaultIterations);
      std::exit(std::strcmp(argv[i], "--help") == 0 ? 0 : 64);
    }
  }
  return n;
}

}  // namespace

int main(int argc, char** argv) {
  const std::size_t n = parse_iterations(argc, argv);
  if (!kOptimized) {
    std::fprintf(stderr,
                 "bench: WARNING: built without optimisation; numbers are not meaningful\n");
  }

  const Sample tls{"tls_heartbeat", samples::tls_heartbeat(kTlsPayloadBytes),
                   std::to_string(kTlsPayloadBytes) + " B payload, 16 B padding"};
  const Sample png{"png_chunks", samples::png(kPngIdatChunks, kPngIdatBytes),
                   std::to_string(5 + kPngIdatChunks) + " chunks, " +
                       std::to_string(kPngIdatChunks) + " x " + std::to_string(kPngIdatBytes) +
                       " B IDAT"};
  const Sample tiff{"tiff_ifd", samples::tiff(kTiffEntries),
                    std::to_string(kTiffEntries) + " IFD entries, little-endian"};

  // ---- tls_heartbeat ------------------------------------------------------
  emit(measure(tls, "handwritten", n, [](const std::uint8_t* p, std::size_t len) {
    static handwritten::TlsHeartbeat out{};  // static: zeroed once, not per parse
    const bool ok = handwritten::parse_tls_heartbeat(p, len, out);
    escape(&out);  // the parsed fields are used: they must be computed
    return ok;
  }));
  emit(measure(tls, "portcullis", n, [](const std::uint8_t* p, std::size_t len) {
    return portcullis_bench::parse_tls_heartbeat(p, len);
  }));

  // ---- png_chunks ---------------------------------------------------------
  emit(measure(png, "handwritten", n, [](const std::uint8_t* p, std::size_t len) {
    static handwritten::PngChunks out{};
    const bool ok = handwritten::parse_png_chunks(p, len, out);
    escape(&out);
    return ok;
  }));
  emit(measure(png, "portcullis", n, [](const std::uint8_t* p, std::size_t len) {
    return portcullis_bench::parse_png_chunks(p, len);
  }));

  // ---- tiff_ifd -----------------------------------------------------------
  // The IFD-table-only variant (<false>): like for like with the Portcullis
  // schema, which is linear and does not follow out-of-line value offsets.
  emit(measure(tiff, "handwritten", n, [](const std::uint8_t* p, std::size_t len) {
    static handwritten::TiffIfd out{};
    const bool ok = handwritten::parse_tiff_ifd<false>(p, len, out);
    escape(&out);
    return ok;
  }));
  emit(measure(tiff, "portcullis", n, [](const std::uint8_t* p, std::size_t len) {
    return portcullis_bench::parse_tiff_ifd(p, len);
  }));

  return 0;
}
