#!/bin/sh
# Build Windows x64 apps on macOS/Linux with an LLVM-MinGW toolchain and
# Windows-targeted static libsodium/Opus installations.
# Usage: ./scripts/build-windows-cross.sh TOOLCHAIN_ROOT DEPENDENCY_PREFIX [OUTPUT_DIR]
set -eu
if [ "$#" -lt 2 ] || [ "$#" -gt 3 ]; then
    printf '%s\n' 'Usage: build-windows-cross.sh TOOLCHAIN_ROOT DEPENDENCY_PREFIX [OUTPUT_DIR]' >&2
    exit 2
fi
project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
compiler="$1/bin/x86_64-w64-mingw32-clang++"
dependency_prefix=$2
output_dir=${3:-"$project_root/dist/windows"}
if [ ! -x "$compiler" ]; then
    printf '%s\n' "LLVM-MinGW compiler unavailable: $compiler" >&2
    exit 1
fi
if [ ! -f "$dependency_prefix/include/sodium.h" ] || [ ! -f "$dependency_prefix/include/opus/opus.h" ]; then
    printf '%s\n' 'Dependency prefix must contain Windows-targeted static libsodium and Opus.' >&2
    exit 1
fi
mkdir -p "$output_dir"
"$compiler" -std=c++20 -O2 -static -DSODIUM_STATIC \
    -I"$project_root/include" -I"$dependency_prefix/include" \
    "$project_root/apps/main.cpp" "$project_root/src/audio_windows.cpp" \
    "$project_root/src/credentials.cpp" "$project_root/src/engine.cpp" \
    "$project_root/src/transport.cpp" -L"$dependency_prefix/lib" \
    -lsodium -lopus -lws2_32 -lole32 -luuid -lavrt -lpropsys -lshell32 -lcrypt32 \
    -o "$output_dir/audiobridge.exe"
"$compiler" -std=c++20 -O2 -Wall -Wextra -Werror -static -municode -mwindows \
    "$project_root/apps/windows/main.cpp" -lcomctl32 -lcomdlg32 -luser32 -lgdi32 \
    -o "$output_dir/AudioBridgeApp.exe"
printf '%s\n' "Built Windows x64 apps in $output_dir"
