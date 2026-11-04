#!/bin/sh
# Builds Continuum 93 on Linux/macOS (needs cmake, ninja or make, a C++20 compiler, and SDL3's
# build deps: on Debian/Ubuntu see https://wiki.libsdl.org/SDL3/README-linux).
#   ./build.sh          Release -> build/release/continuum
#   ./build.sh debug    Debug   -> build/debug/continuum
set -e
CONFIG=Release
[ "$1" = "debug" ] && CONFIG=Debug
DIR="$(cd "$(dirname "$0")" && pwd)/build/$(echo $CONFIG | tr A-Z a-z)"
GEN=""; command -v ninja >/dev/null && GEN="-G Ninja"
cmake -S "$(dirname "$0")" -B "$DIR" $GEN -DCMAKE_BUILD_TYPE=$CONFIG >/dev/null
cmake --build "$DIR" --parallel
echo "build: $DIR/continuum"
