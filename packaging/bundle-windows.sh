#!/usr/bin/env bash

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
cp "$ROOT"/deps/protobuf3196/bin/*.dll "$DEST/" 2>/dev/null || true
cp "$ROOT"/deps/paho/bin/*.dll "$DEST/" 2>/dev/null || true

# Optional: bundled static ffmpeg CLI for the simulator's video import.
if [ "$WITH_FFMPEG" = "--with-ffmpeg" ]; then
  FF="$ROOT/deps/ffmpeg-cli/bin/ffmpeg.exe"
  [ -e "$FF" ] || { echo "missing $FF (run scripts/deps.sh)" >&2; exit 1; }
  cp "$FF" "$DEST/"
fi

# Pack the staged folder into one installer .exe.
case "$NAME" in
  rm_client)    DISPLAY_NAME="RM Custom Client" ;;
  rm_simulator) DISPLAY_NAME="RM Simulator" ;;
  *)            DISPLAY_NAME="$NAME" ;;
esac
if [ "${GITHUB_REF_TYPE:-}" = tag ]; then VERSION="$GITHUB_REF_NAME"; else VERSION="v0.1.${GITHUB_RUN_NUMBER:-0}"; fi

MAKENSIS="$(command -v makensis || true)"
[ -n "$MAKENSIS" ] || { echo "missing makensis (pacman -S mingw-w64-x86_64-nsis)" >&2; exit 1; }

OUT="$ROOT/dist/$NAME-setup.exe"
"$MAKENSIS" -V2 \
  -D"APP_NAME=$DISPLAY_NAME" -D"APP_EXE=$NAME.exe" -D"APP_VERSION=$VERSION" \
  -D"SRCDIR=$(cygpath -w "$DEST")" -D"OUTFILE=$(cygpath -w "$OUT")" \
  "$ROOT/packaging/windows-installer.nsi"
echo "==> $OUT"
