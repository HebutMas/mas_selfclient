#!/usr/bin/env bash
# Builds a self-contained AppImage for one rm binary using linuxdeploy + its Qt
# plugin. Requires Qt (qmake) on PATH (CI provides it via install-qt-action).
#
# Usage: packaging/linux-appimage.sh <exe-path> <desktop-file> <output-name> [--with-ffmpeg]
set -euo pipefail

EXE="$1"
DESKTOP="$2"
NAME="$3"
WITH_FFMPEG="${4:-}"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TOOLS="$ROOT/.tools"
OUT="$ROOT/dist"
mkdir -p "$TOOLS" "$OUT"

# linuxdeploy tools are themselves AppImages; extract-and-run avoids needing FUSE.
export APPIMAGE_EXTRACT_AND_RUN=1
export PATH="$TOOLS:$PATH"

fetch() { # url dest
  if [ ! -x "$2" ]; then
    echo "==> fetching $(basename "$2")"
    curl -fL --retry 3 -o "$2" "$1"
    chmod +x "$2"
  fi
}
fetch "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage" "$TOOLS/linuxdeploy"
fetch "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage" "$TOOLS/linuxdeploy-plugin-qt"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# linuxdeploy matches the desktop file's Icon= entry against the icon file's
# basename, so deploy the shared icon under the app's own name.
ICON="$WORK/$NAME.png"
cp "$ROOT/packaging/icon.png" "$ICON"

# Bundle the app (and its Qt deps) into the AppDir first.
"$TOOLS/linuxdeploy" \
  --appdir "$WORK/AppDir" \
  --executable "$ROOT/$EXE" \
  --desktop-file "$ROOT/$DESKTOP" \
  --icon-file "$ICON" \
  --plugin qt

# Optional: bundled static ffmpeg CLI for the simulator's video import.
# Copied after linuxdeploy so patchelf never touches the static binary.
if [ "$WITH_FFMPEG" = "--with-ffmpeg" ]; then
  FF="$ROOT/third_party/ffmpeg-cli/bin/ffmpeg"
  [ -x "$FF" ] || { echo "missing $FF (run third_party/setup-deps.sh)" >&2; exit 1; }
  cp "$FF" "$WORK/AppDir/usr/bin/"
fi

"$TOOLS/linuxdeploy" --appdir "$WORK/AppDir" --output appimage

mv "$WORK"/*.AppImage "$OUT/$NAME.AppImage"
echo "==> $OUT/$NAME.AppImage"
