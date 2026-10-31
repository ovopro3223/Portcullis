#!/usr/bin/env bash
# Fetch and ASan-build the pinned vulnerable versions for the CVE suite.
# Linux/CI only (see README.md). The reject-tests do not depend on this.
#
#   ./fetch.sh            fetch, verify (if hashes recorded), and ASan-build
#   ./fetch.sh --verify   only re-check sha256 of downloaded tarballs
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
src="$here/src"
build="$here/build"
mkdir -p "$src" "$build"

# project|version|url|sha256(empty until recorded)
PINS=(
  "openssl|1.0.1f|https://www.openssl.org/source/old/1.0.1/openssl-1.0.1f.tar.gz|"
  "libpng|1.6.18|https://download.sourceforge.net/libpng/libpng-1.6.18.tar.gz|"
  "libtiff|4.0.6|https://download.osgeo.org/libtiff/tiff-4.0.6.tar.gz|"
  "libtiff|4.0.7|https://download.osgeo.org/libtiff/tiff-4.0.7.tar.gz|"
)

ASAN="-fsanitize=address -g -O1 -fno-omit-frame-pointer"

verify_only="${1:-}"

for pin in "${PINS[@]}"; do
  IFS='|' read -r name ver url sha <<<"$pin"
  tgz="$src/$(basename "$url")"
  [ -f "$tgz" ] || curl -fsSL -o "$tgz" "$url"
  if [ -n "$sha" ]; then
    echo "$sha  $tgz" | sha256sum -c -
  else
    echo "WARN: no recorded sha256 for $name-$ver ($(sha256sum "$tgz" | cut -d' ' -f1))"
  fi
  [ "$verify_only" = "--verify" ] && continue

  dir="$src/${name}-${ver}"
  rm -rf "$dir"; mkdir -p "$dir"
  tar xf "$tgz" -C "$dir" --strip-components=1
  out="$build/${name}-${ver}"; mkdir -p "$out"
  echo "building $name-$ver with ASan -> $out (see per-project configure flags)"
  # Per-project configure differs (OpenSSL ./config, autotools for png/tiff);
  # the CI job sets CC/CFLAGS="$ASAN" and installs into "$out". Kept explicit
  # in CI rather than guessed here so a build failure is visible, not hidden.
done

echo "done (verify_only=${verify_only:-no})"
