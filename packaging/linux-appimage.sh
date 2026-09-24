#!/usr/bin/env bash
# Builds a self-contained AppImage for one rm binary using linuxdeploy + its Qt
# plugin. Requires Qt (qmake) on PATH (CI provides it via install-qt-action).
#
# Usage: packaging/linux-appimage.sh <exe-path> <desktop-file> <output-name> [--with-ffmpeg] [--with-broker]
set -euo pipefail

EXE="$1"
DESKTOP="$2"
NAME="$3"
shift 3
WITH_FFMPEG=""
WITH_BROKER=""
for arg in "$@"; do
  case "$arg" in
    --with-ffmpeg) WITH_FFMPEG=1 ;;
    --with-broker) WITH_BROKER=1 ;;
    *) echo "unknown option: $arg" >&2; exit 1 ;;
  esac
done

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

# linuxdeploy-plugin-qt deploys only the xcb platform plugin by default; add the
# Wayland platform plugin and have the qt plugin copy the shell/decoration/
# graphics integration plugin dirs too. Without this, Wayland sessions abort with
#   qt.qpa.plugin: Could not find the Qt platform plugin "wayland" in ""
export EXTRA_PLATFORM_PLUGINS="libqwayland.so"
export EXTRA_QT_MODULES="waylandcompositor"

# Bundle the app (and its Qt deps) into the AppDir first.
"$TOOLS/linuxdeploy" \
  --appdir "$WORK/AppDir" \
  --executable "$ROOT/$EXE" \
  --desktop-file "$ROOT/$DESKTOP" \
  --icon-file "$ICON" \
  --plugin qt

# Optional: bundled static ffmpeg CLI for the simulator's video import.
# Copied after linuxdeploy so patchelf never touches the static binary.
if [ -n "$WITH_FFMPEG" ]; then
  FF="$ROOT/third_party/ffmpeg-cli/bin/ffmpeg"
  [ -x "$FF" ] || { echo "missing $FF (run third_party/setup-deps.sh)" >&2; exit 1; }
  cp "$FF" "$WORK/AppDir/usr/bin/"
fi

# Optional: bundle the mosquitto broker so the simulator is self-contained
# (it starts it on loopback at launch). Copied after linuxdeploy, like ffmpeg.
if [ -n "$WITH_BROKER" ]; then
  MOSQ="$ROOT/third_party/mosquitto/sbin/mosquitto"
  [ -x "$MOSQ" ] || { echo "missing $MOSQ (run third_party/setup-deps.sh)" >&2; exit 1; }
  cp "$MOSQ" "$WORK/AppDir/usr/bin/"
  chmod +x "$WORK/AppDir/usr/bin/mosquitto"
fi

# OUTPUT tells linuxdeploy/appimagetool where to write the result (otherwise it
# lands in the current directory under a name derived from the desktop Name=).
OUTPUT="$OUT/$NAME.AppImage" "$TOOLS/linuxdeploy" --appdir "$WORK/AppDir" --output appimage
echo "==> $OUT/$NAME.AppImage"
