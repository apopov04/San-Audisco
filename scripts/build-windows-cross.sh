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
room_crypto_prefix=${BORINGSSL_ROOT:?Set BORINGSSL_ROOT to the pinned Windows room-crypto install prefix}
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
"$1/bin/x86_64-w64-mingw32-windres" -I"$project_root/apps/windows" "$project_root/apps/windows/app.rc" -O coff -o "$output_dir/app.res"
"$compiler" -std=c++20 -O2 -Wall -Wextra -Werror -static -municode -mwindows \
    -I"$project_root/third_party/json/include" \
    "$project_root/apps/windows/main.cpp" "$output_dir/app.res" -lcomctl32 -lcomdlg32 -luser32 -lgdi32 -ladvapi32 -lshell32 \
    -o "$output_dir/AudioBridgeApp.exe"
"$compiler" -std=c++20 -O2 -static -DSODIUM_STATIC \
    -I"$project_root/include" -I"$dependency_prefix/include" \
    -I"$project_root/third_party/json/include" -I"$room_crypto_prefix/include" \
    "$project_root/apps/agent.cpp" "$project_root/src/agent_platform.cpp" \
    "$project_root/src/output_routing.cpp" "$project_root/src/output_routing_windows.cpp" \
    "$project_root/src/room.cpp" "$project_root/src/audio_windows.cpp" \
    "$project_root/src/credentials.cpp" "$project_root/src/engine.cpp" \
    "$project_root/src/transport.cpp" -L"$dependency_prefix/lib" \
    "$room_crypto_prefix/lib/libcrypto.a" \
    -lsodium -lopus -lws2_32 -lole32 -luuid -lavrt -lpropsys -lshell32 -lcrypt32 -ladvapi32 -lbcrypt -lwinpthread \
    -o "$output_dir/bridge-agent.exe"
printf '%s\n' "Built Windows x64 apps in $output_dir"
