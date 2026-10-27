#!/bin/sh
# Pinned Newlib; install only under this repository's build directory.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
version=4.6.0.20260123
archive="$root/build/deps/newlib-$version.tar.gz"
prefix="$root/build/newlib"
mkdir -p "$root/build/deps" "$root/build/newlib-build"
if [ ! -f "$archive" ]; then
    curl -fL "https://sourceware.org/pub/newlib/newlib-$version.tar.gz" -o "$archive"
fi
expected=6ff27e3bf022666f43f7802255be680eeff722ac181b1725d21e2e8318604ee3
actual=$(shasum -a 256 "$archive" | cut -d ' ' -f 1)
[ "$actual" = "$expected" ] || { echo 'Newlib archive checksum mismatch' >&2; exit 1; }
if [ ! -d "$root/build/deps/newlib-$version" ]; then
    tar -xzf "$archive" -C "$root/build/deps"
fi
cd "$root/build/newlib-build"
if [ ! -f Makefile ]; then
    "../deps/newlib-$version/configure" --target=aarch64-elf --prefix="$prefix" \
        --disable-multilib --disable-nls --disable-newlib-supplied-syscalls \
        --disable-newlib-multithread CFLAGS_FOR_TARGET='-O2 -g -fno-pic -mno-outline-atomics'
fi
make -j"${JOBS:-4}" all-target-newlib
make install-target-newlib
