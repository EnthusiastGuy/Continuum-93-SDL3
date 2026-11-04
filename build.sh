#!/bin/sh
# Builds Continuum 93 on Linux/macOS (needs cmake, ninja or make, a C++20 compiler, and SDL3's
# build deps: on Debian/Ubuntu see https://wiki.libsdl.org/SDL3/README-linux).
#   ./build.sh          Release -> build/release/continuum
#   ./build.sh debug    Debug   -> build/debug/continuum
#   ./build.sh linux    Docker  -> build/linux-x64, build/linux-arm64 (glibc 2.36+: Raspberry Pi OS bookworm too)
#   ./build.sh web      Docker  -> build/web (serve with COOP/COEP headers, docs/WEB.md)
#   ./build.sh all      release + linux + web
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"
EMSDK=emscripten/emsdk:6.0.13

native() {
    DIR="$ROOT/build/$(echo $1 | tr A-Z a-z)"
    GEN=""; command -v ninja >/dev/null && GEN="-G Ninja"
    cmake -S "$ROOT" -B "$DIR" $GEN -DCMAKE_BUILD_TYPE=$1 >/dev/null
    cmake --build "$DIR" --parallel
    echo "build: $DIR/continuum"
}

# docked <image> <build dir> <configure command> [docker args]: builds as root in the container,
# then hands the outputs back to the calling user.
docked() {
    img=$1 dir=$2 cfg=$3; shift 3
    docker run --rm "$@" -v "$ROOT:/src" -w /src "$img" sh -c \
        "$cfg >/dev/null && cmake --build $dir --parallel && chown -R $(id -u):$(id -g) $dir"
    echo "build: $ROOT/$dir"
}

linux() {
    for a in amd64:x64 arm64:arm64; do
        p=${a%:*} n=${a#*:}
        # arm64 runs emulated (QEMU, built into Docker Desktop; on Linux: docker run --privileged --rm tonistiigi/binfmt --install arm64)
        docker build -q --platform linux/$p -t continuum93-linux:$p -f "$ROOT/docker/linux.Dockerfile" "$ROOT/docker" >/dev/null
        docked continuum93-linux:$p build/linux-$n "cmake -S . -B build/linux-$n -G Ninja -DCMAKE_BUILD_TYPE=Release" --platform linux/$p
    done
}

# The named volume keeps Emscripten's compiled system libraries between runs.
web() {
    docked $EMSDK build/web "emcmake cmake -S . -B build/web -DCMAKE_BUILD_TYPE=Release" \
        -v continuum93-emcache:/emsdk/upstream/emscripten/cache
}

case "$1" in
    "")    native Release ;;
    debug) native Debug ;;
    linux) linux ;;
    web)   web ;;
    all)   native Release; linux; web ;;
    *)     echo "usage: $0 [debug|linux|web|all]"; exit 1 ;;
esac
