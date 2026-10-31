#ifndef PORTCULLIS_TESTS_SCHEMAS_HEARTBEAT_HPP
#define PORTCULLIS_TESTS_SCHEMAS_HEARTBEAT_HPP

// The spec's worked example (design 5.2): a TLS heartbeat record. The payload
// is as many bytes as the `len` field declares — the exact relationship that
// CVE-2014-0160 (Heartbleed) violated. Shared across P1 tests and, later, the
// fuzz targets and the CVE contract.
#include <portcullis/schema/combinators.hpp>
#include <portcullis/schema/primitives.hpp>
#include <portcullis/schema/ref.hpp>

namespace portcullis::schemas {

using Heartbeat = Struct<Field<"type", U8>, Field<"len", U16BE>,
                         Field<"payload", Bytes<Ref("len")>>, Field<"padding", Bytes<16>>>;

}  // namespace portcullis::schemas

#endif  // PORTCULLIS_TESTS_SCHEMAS_HEARTBEAT_HPP
