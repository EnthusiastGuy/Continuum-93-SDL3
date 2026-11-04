#!/bin/sh
# macOS package, on a Mac (Xcode command line tools and CMake): builds natively and writes
# dist/continuum93-macos-<arm64|x64>.zip; if the site (dist/web/) is there, the zip goes into its downloads too.
# The site's macOS button links the arm64 (Apple silicon) zip.
# Not signed or notarised: users right-click > Open the first time (or xattr -dr com.apple.quarantine).
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"
[ "$(uname)" = Darwin ] || { echo "package-macos: run this on a Mac"; exit 1; }
./build.sh
ARCH=$(uname -m); [ "$ARCH" = x86_64 ] && ARCH=x64
rm -rf build/pkg-macos && mkdir -p build/pkg-macos/Continuum93 dist
cp build/release/continuum build/pkg-macos/Continuum93/
cp -r Data README.md LICENSE build/pkg-macos/Continuum93/
rm -f "dist/continuum93-macos-$ARCH.zip"
(cd build/pkg-macos && zip -qr9 "$ROOT/dist/continuum93-macos-$ARCH.zip" Continuum93)
echo "package: dist/continuum93-macos-$ARCH.zip"
if [ -d dist/web/downloads ]; then cp "dist/continuum93-macos-$ARCH.zip" dist/web/downloads/; echo "package: added to dist/web/downloads/"; fi
