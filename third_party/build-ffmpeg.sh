#!/usr/bin/env bash
# Builds a slim, static FFmpeg into third_party/ffmpeg/ for the RM client and
# simulator. Idempotent: skips when third_party/ffmpeg/lib/libavcodec.a exists.
#
# What it builds (minimal, not the full FFmpeg):
#   - software HEVC / H.264 decode  + swscale
#   - AMD / Intel hardware  : VAAPI  (decode + encode)
#   - NVIDIA     hardware  : NVDEC / NVENC (via ffnvcodec, dlopen at runtime)
#
# Dev headers for VAAPI (libva) and NVIDIA (ffnvcodec) are provisioned into
# third_party/ffmpeg-deps/. VAAPI headers come from the distro's -devel package
# (dnf/apt download, extracted without installing); if unavailable, VAAPI is
# skipped and only software + NVDEC remain.
#
# Requires: curl/git, make, a C compiler. First build takes a few minutes.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"          # third_party/
FF_VER=8.1.2
FF_SRC="$ROOT/ffmpeg-$FF_VER"
FF_PREFIX="$ROOT/ffmpeg"
DEPS="$ROOT/ffmpeg-deps"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

if [ -e "$FF_PREFIX/lib/libavcodec.a" ]; then
  echo "==> ffmpeg $FF_VER already built, skipping"
  exit 0
fi

# --- NVDEC/NVENC headers (header-only, dynamically linked at runtime) --------
# Always (re)install: ffnvcodec.pc embeds an absolute prefix from whoever
# generated it, so a fresh clone (CI) must regenerate it for this machine.
echo "==> nv-codec-headers (NVDEC/NVENC)"
[ -d "$ROOT/nv-codec-headers/.git" ] || \
  git clone --depth 1 https://github.com/FFmpeg/nv-codec-headers.git "$ROOT/nv-codec-headers"
make -C "$ROOT/nv-codec-headers" install PREFIX="$DEPS" >/dev/null

# --- VAAPI headers (libva + libdrm) ------------------------------------------
HAVE_VA=0
mkdir -p "$DEPS/include" "$DEPS/lib/pkgconfig"
if pkg-config --exists libva libva-drm libdrm 2>/dev/null; then
  HAVE_VA=1                                   # system -devel already installed
  # Refresh the .pc files: the committed ones may carry another machine's
  # absolute prefix, which would break configure on a fresh clone (CI).
  for p in libva libva-drm libdrm; do
    src="$(pkg-config --variable=pcfiledir "$p" 2>/dev/null)/$p.pc"
    [ -f "$src" ] && cp "$src" "$DEPS/lib/pkgconfig/$p.pc"
  done
elif command -v dnf >/dev/null 2>&1; then
  echo "==> libva-devel / libdrm-devel headers via dnf download"
  ( cd "$TMP" && dnf download --destdir rpm libva-devel libdrm-devel >/dev/null 2>&1 )
  ( cd "$TMP" && for r in rpm/*x86_64.rpm; do rpm2cpio "$r" | cpio -idm --quiet; done )
  cp -r "$TMP/usr/include/va" "$DEPS/include/va"
  cp -r "$TMP/usr/include/libdrm" "$DEPS/include/libdrm"
  # xf86drm*.h live at the include root (Fedora and Debian/Ubuntu alike), but
  # ffmpeg's libdrm check does #include <xf86drm.h>; drop them beside the rest.
  cp "$TMP/usr/include/"xf86drm*.h "$DEPS/include/libdrm/"
  for p in libva libva-drm libdrm; do
    sed -e "s|^prefix=.*|prefix=$DEPS|" -e "s|^libdir=.*|libdir=$DEPS/lib|" \
        "$TMP/usr/lib64/pkgconfig/$p.pc" > "$DEPS/lib/pkgconfig/$p.pc"
  done
  for so in libva.so.2 libva-drm.so.2 libdrm.so.2; do
    [ -e "/usr/lib64/$so" ] && ln -sf "/usr/lib64/$so" "$DEPS/lib/${so%%.so*}.so"
  done
  HAVE_VA=1
elif command -v apt-get >/dev/null 2>&1; then
  echo "==> libva-dev / libdrm-dev headers via apt-get download"
  ( cd "$TMP" && apt-get download libva-dev libdrm-dev >/dev/null 2>&1 )
  ( cd "$TMP" && for d in *.deb; do dpkg-deb -x "$d" .; done )
  cp -r "$TMP/usr/include/va" "$DEPS/include/va"
  cp -r "$TMP/usr/include/libdrm" "$DEPS/include/libdrm"
  # xf86drm*.h live at the include root (Fedora and Debian/Ubuntu alike), but
  # ffmpeg's libdrm check does #include <xf86drm.h>; drop them beside the rest.
  cp "$TMP/usr/include/"xf86drm*.h "$DEPS/include/libdrm/"
  for p in libva libva-drm libdrm; do
    sed -e "s|^prefix=.*|prefix=$DEPS|" -e "s|^libdir=.*|libdir=$DEPS/lib|" \
        "$TMP/usr/lib/x86_64-linux-gnu/pkgconfig/$p.pc" > "$DEPS/lib/pkgconfig/$p.pc"
  done
  for so in libva.so.2 libva-drm.so.2 libdrm.so.2; do
    [ -e "/usr/lib/x86_64-linux-gnu/$so" ] && ln -sf "/usr/lib/x86_64-linux-gnu/$so" "$DEPS/lib/${so%%.so*}.so"
  done
  HAVE_VA=1
else
  echo "!! libva-dev not found; building FFmpeg without VAAPI (software + NVDEC only)"
fi

# --- FFmpeg source -----------------------------------------------------------
if [ ! -d "$FF_SRC" ]; then
  echo "==> ffmpeg $FF_VER source"
  curl -fL -o "$ROOT/ffmpeg.tar.xz" "https://ffmpeg.org/releases/ffmpeg-$FF_VER.tar.xz"
  tar xf "$ROOT/ffmpeg.tar.xz" -C "$ROOT"
fi

# --- configure + build + install --------------------------------------------
VA_FLAGS="--disable-vaapi --disable-libdrm"
[ "$HAVE_VA" = 1 ] && VA_FLAGS="--enable-vaapi --enable-libdrm"

echo "==> configuring ffmpeg (soft + vaapi=$HAVE_VA + nvdec)"
( cd "$FF_SRC" && \
  PKG_CONFIG_PATH="$DEPS/lib/pkgconfig" ./configure \
    --prefix="$FF_PREFIX" \
    --disable-shared --enable-static --enable-pic \
    --disable-programs --disable-doc --disable-network \
    --disable-avdevice --disable-avfilter \
    --disable-everything \
    --enable-decoder=hevc,h264 \
    --enable-parser=hevc,h264 \
    --enable-hwaccel=hevc_vaapi,h264_vaapi,hevc_nvdec,h264_nvdec \
    --enable-encoder=hevc_vaapi,h264_vaapi,hevc_nvenc,h264_nvenc \
    --enable-swscale --enable-protocol=file \
    --disable-x86asm \
    --disable-iconv --disable-zlib \
    --enable-ffnvcodec --enable-nvdec --enable-nvenc \
    $VA_FLAGS )

echo "==> building ffmpeg (this takes a few minutes)"
make -C "$FF_SRC" -j"$(nproc)" >/dev/null
make -C "$FF_SRC" install >/dev/null
echo "==> ffmpeg installed to $FF_PREFIX"
