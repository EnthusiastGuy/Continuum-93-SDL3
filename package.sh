#!/bin/sh
# Packages Continuum 93 for every platform this host can build (needs Docker; Windows: package.cmd).
#   ./package.sh    builds (./build.sh all), then writes
#                   dist/continuum93-<platform>.zip   Windows*, Linux x64, Linux arm64, web
#                   dist/web/                         the site: plays in the browser, links the zips
#   (* the Windows zip comes from package.cmd's build; macOS: ./package-macos.sh on a Mac, then run this again)
# Each zip: Continuum93/ with the program, a clean Data/ from the repo (never the build folder's, which holds
# local settings), README.md and LICENSE. Zipping runs in the emsdk container so every host makes the same zips.
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"

if [ "$1" != "--inside" ]; then
    [ "$1" = "--no-build" ] || "$ROOT/build.sh" all
    docker run --rm -v "$ROOT:/src" -w /src -e HOST_ID="$(id -u):$(id -g)" emscripten/emsdk:6.0.13 sh package.sh --inside
    exit
fi

# --- in the container, at the repo root -------------------------------------------------------------
V=$(sed -n 's/^project(Continuum93 VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
mkdir -p dist
rm -f dist/continuum93-windows-x64.zip dist/continuum93-linux-*.zip dist/continuum93-web.zip
rm -rf dist/web

pack() {   # pack <platform> <program>
    if [ ! -f "$2" ]; then echo "package: $1 skipped ($2 not built)"; return; fi
    rm -rf /tmp/pkg && mkdir -p /tmp/pkg/Continuum93
    cp "$2" /tmp/pkg/Continuum93/ && chmod 755 "/tmp/pkg/Continuum93/$(basename "$2")"
    cp -r Data README.md LICENSE /tmp/pkg/Continuum93/
    (cd /tmp/pkg && zip -qr9 "/src/dist/continuum93-$1.zip" Continuum93)
    echo "package: dist/continuum93-$1.zip"
}
WIN=build/Release/continuum.exe; [ -f "$WIN" ] || WIN=build/release/continuum.exe
pack windows-x64 "$WIN"
pack linux-x64 build/linux-x64/continuum
pack linux-arm64 build/linux-arm64/continuum

if [ -f build/web/continuum.wasm ]; then
    zip -qj9 dist/continuum93-web.zip build/web/continuum.html build/web/continuum.js build/web/continuum.wasm \
        build/web/continuum.data web/.htaccess web/_headers   # the headers it needs, for Apache / Netlify
    echo "package: dist/continuum93-web.zip"
    # The site: the page, the header files and the service worker; the machine in play/; every zip in downloads/.
    mkdir -p dist/web/play dist/web/downloads
    sed "s/@VERSION@/$V/g" web/index.html > dist/web/index.html
    cp web/coi-sw.js web/.htaccess web/_headers dist/web/
    cp build/web/continuum.html build/web/continuum.js build/web/continuum.wasm build/web/continuum.data dist/web/play/
    cp dist/continuum93-*.zip dist/web/downloads/
    echo "package: dist/web/ (upload its contents as they are)"
else
    echo "package: web skipped (build/web not built)"
fi
[ -z "$HOST_ID" ] || chown -R "$HOST_ID" dist
