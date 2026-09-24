#!/usr/bin/env bash
# Bundles one rm binary into a self-contained zip (Qt via windeployqt plus the
# MinGW runtime). Run from an MSYS2 MINGW64 shell.
#
# Usage: packaging/bundle-windows.sh <exe-path-without-.exe> <output-name> [--with-ffmpeg]
set -euo pipefail

EXE="$1"
NAME="$2"
WITH_FFMPEG="${3:-}"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$ROOT/dist/$NAME"
rm -rf "$DEST"
mkdir -p "$DEST"

cp "$ROOT/$EXE.exe" "$DEST/"

# Qt libraries + plugins.
WINDEPLOYQT="$(command -v windeployqt-qt6 || command -v windeployqt6 || command -v windeployqt)"
"$WINDEPLOYQT" --release --no-translations --no-system-d3d-compiler --no-opengl-sw \
  "$DEST/$NAME.exe"

# MinGW runtime (windeployqt only handles MSVC's runtime).
for dll in libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll; do
  [ -e "$MINGW_PREFIX/bin/$dll" ] && cp "$MINGW_PREFIX/bin/$dll" "$DEST/"
done

# Vendored shared deps, if present (static builds don't need them).
cp "$ROOT"/third_party/protobuf3196/bin/*.dll "$DEST/" 2>/dev/null || true
cp "$ROOT"/third_party/paho/bin/*.dll "$DEST/" 2>/dev/null || true

# Optional: bundled static ffmpeg CLI for the simulator's video import.
if [ "$WITH_FFMPEG" = "--with-ffmpeg" ]; then
  FF="$ROOT/third_party/ffmpeg-cli/bin/ffmpeg.exe"
  [ -e "$FF" ] || { echo "missing $FF (run third_party/setup-deps.sh)" >&2; exit 1; }
  cp "$FF" "$DEST/"
fi

( cd "$ROOT/dist" && rm -f "$NAME.zip" && zip -qr "$NAME.zip" "$NAME" )
echo "==> $ROOT/dist/$NAME.zip"
