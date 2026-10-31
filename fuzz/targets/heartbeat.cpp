// libFuzzer target for the TLS heartbeat record (the Heartbleed shape): parse
// must never read out of bounds whatever bytes the fuzzer supplies.
#include <portcullis/fuzz/libfuzzer.hpp>

#include "schemas/heartbeat.hpp"

PORTCULLIS_FUZZ_TARGET(::portcullis::schemas::Heartbeat)
