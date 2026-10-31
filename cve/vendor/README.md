# Vendored vulnerable sources (ASan reproduction)

`fetch.sh` downloads the pinned vulnerable version of each project, verifies its
sha256, and builds it with AddressSanitizer into `build/<project>/` (gitignored).
This is the **optional** second data point for the CVE suite — it shows the
original parser crashing on the trigger vector. The primary proof is the Wave 2
reject-tests, which need only the synthesized trigger vectors in
`../SELECTION.md` and do not depend on these builds.

These builds target Linux (the CVE-era build systems and ASan integration are
Linux-oriented); they run in the Linux CI job, not on the Windows dev machine.

Pinned versions:

| Project | Version | CVEs |
| --- | --- | --- |
| openssl | 1.0.1f | CVE-2014-0160; (DTLS 0195 on a <1.0.1h build) |
| libpng | 1.6.18 | CVE-2015-8126 (8540 may need a 1.5.x build) |
| libtiff | 4.0.6 / 4.0.7 | CVE-2016-9535 / CVE-2017-9147 |

sha256 values are filled in on the first Linux run (`fetch.sh --record-hashes`)
and then checked by `fetch.sh --verify`.
