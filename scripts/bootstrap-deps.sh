#!/bin/sh
# POSIX native/cross dependency build. Usage: bootstrap-deps.sh PREFIX [BUILD_DIR]
# Cross builds: CC=... AR=... RANLIB=... HOST=x86_64-w64-mingw32 ./bootstrap-deps.sh ...
set -eu
prefix=${1:?Pass an absolute install prefix}
case "$prefix" in /*) ;; *) echo 'Install prefix must be absolute' >&2; exit 2;; esac
build=${2:-"$prefix/../dependency-build"}
mkdir -p "$build" "$prefix"
build=$(cd "$build" && pwd)
verify() {
  expected=$1
  archive=$2
  if command -v sha256sum >/dev/null 2>&1; then
    actual=$(sha256sum "$archive" | cut -d ' ' -f 1)
  else
    actual=$(shasum -a 256 "$archive" | cut -d ' ' -f 1)
  fi
  if [ "$actual" != "$expected" ]; then echo "SHA256 mismatch: $archive" >&2; exit 1; fi
}
fetch() {
  url=$1
  archive=$2
  hash=$3
  if [ ! -f "$archive" ]; then curl --fail --location --proto '=https' --tlsv1.2 "$url" -o "$archive.tmp"; mv "$archive.tmp" "$archive"; fi
  verify "$hash" "$archive"
}
fetch https://download.libsodium.org/libsodium/releases/libsodium-1.0.20.tar.gz "$build/libsodium-1.0.20.tar.gz" ebb65ef6ca439333c2bb41a0c1990587288da07f6c7fd07cb3a18cc18d30ce19
fetch https://downloads.xiph.org/releases/opus/opus-1.5.2.tar.gz "$build/opus-1.5.2.tar.gz" 65c1d2f78b9f2fb20082c38cbe47c951ad5839345876e46941612ee87f9a7ce1
for package in libsodium-1.0.20 opus-1.5.2; do
  if [ ! -d "$build/$package" ]; then tar -xzf "$build/$package.tar.gz" -C "$build"; fi
  (
    cd "$build/$package"
    set -- --prefix="$prefix" --disable-shared --enable-static
    if [ -n "${HOST:-}" ]; then set -- "$@" --host="$HOST"; fi
    if [ "$package" = opus-1.5.2 ]; then set -- "$@" --disable-extra-programs --disable-doc; fi
    ./configure "$@"
    make -j"${JOBS:-4}"
    make install
  )
done
printf 'Dependencies installed in %s\n' "$prefix"
