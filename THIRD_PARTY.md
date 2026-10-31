# Third-party components

The library (`include/portcullis/`) has **zero** dependencies. The items below
are used only for testing and tooling.

| Component | Version | License | Where | sha256 |
| --- | --- | --- | --- | --- |
| doctest | 2.4.11 | MIT | `tests/third_party/doctest.h` | `44faa038e9c3f9728efbda143748d01124ea0a27f4bf78f35a15d8fab2e039fb` |

CVE-replay vendored sources (OpenSSL, libpng, libtiff at pinned vulnerable
versions) are fetched by `cve/vendor/fetch.sh` with recorded sha256s and are
not committed; see `cve/vendor/README.md`.
