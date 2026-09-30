#!/usr/bin/env bash
# The one entry point for every third-party dependency, and for the bundle that
# ships them to everyone else.
#
#   scripts/deps.sh          install into deps/  (pinned prebuilt bundle, else
#                            build from source). This is what everyone runs.
#   scripts/deps.sh --pack   build everything from source and pack the bundle
#                            (maintainers / CI). deps.yml publishes the result,
#                            then deps.lock gets the tag + sha256.
#
# Contents: protobuf (protoc + libprotobuf), Eclipse Paho MQTT C/C++,
# Mosquitto broker (local test broker; Linux only), a slim static
# libavcodec/avutil/swscale (software + VAAPI + NVDEC/NVENC) and a prebuilt
# static ffmpeg CLI (simulator video import; BtbN GPL build, includes libx265).
#
# Versions and the pinned bundle tag/sha256 live only in scripts/deps.lock.
# RM_DEPS_BUNDLE_URL=<url> fetches the bundle from a mirror instead.
#
# Runs on Linux and in an MSYS2 MINGW64 shell.
# Install needs: curl, sha256sum, tar (Linux: gzip; Windows: unzip).
# Source build needs: git, curl, cmake, ninja, make, zip, C++17 compiler.
set -euo pipefail

SELF="$(cd "$(dirname "$0")" && pwd)"          # scripts/
ROOT="$(cd "$SELF/.." && pwd)"                 # repo root
DEPS="$ROOT/deps"                              # every dependency installs here
. "$SELF/deps.lock"

PACK=0
for a in "$@"; do
  case "$a" in
    --pack) PACK=1 ;;
    -h|--help) echo "usage: $0 [--pack]"; exit 0 ;;
    *) echo "unknown argument: $a" >&2; exit 2 ;;
  esac
done

case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*) IS_WINDOWS=1 ;;
  *)                    IS_WINDOWS=0 ;;
esac
# Force the install libdir so the vendored layout is deterministic across
# distros (Fedora defaults to lib64, Debian to lib/<multiarch>).
if [ "$IS_WINDOWS" = 1 ]; then LIBDIR=lib; else LIBDIR=lib64; fi
# Shared libs on Linux, static archives on Windows (no DLL juggling there).
if [ "$IS_WINDOWS" = 1 ]; then
  PB_SHARED=OFF; PAHO_SHARED=OFF; LIBEXT=a
else
  PB_SHARED=ON;  PAHO_SHARED=ON;  LIBEXT=so
fi
# Upstream only strips the "-static" suffix from the archive name on *nix, so
# on Windows the static libs are libpaho-mqtt3a-static.a / libpaho-mqttpp3-static.a.
if [ "$IS_WINDOWS" = 1 ]; then
  PAHO_C_LIBBASE=paho-mqtt3a-static; PAHO_CPP_LIBBASE=paho-mqttpp3-static
else
  PAHO_C_LIBBASE=paho-mqtt3a;       PAHO_CPP_LIBBASE=paho-mqttpp3
fi
if [ "$IS_WINDOWS" = 1 ]; then
  BUNDLE_PLATFORM=windows; BUNDLE_SHA="$DEPS_BUNDLE_SHA256_WINDOWS"; BUNDLE_EXT=zip
else
  BUNDLE_PLATFORM=linux;   BUNDLE_SHA="$DEPS_BUNDLE_SHA256_LINUX";   BUNDLE_EXT=tar.gz
fi

PB_DIR="$DEPS/protobuf3196"
SRC_DIR="$DEPS/protobuf-$PROTOC_VER"
PAHO="$DEPS/paho"
MOSQ="$DEPS/mosquitto"
FFCLI="$DEPS/ffmpeg-cli"
FF_SRC="$DEPS/ffmpeg-$FFMPEG_VER"
FF_PREFIX="$DEPS/ffmpeg"
FFDEPS="$DEPS/ffmpeg-deps"
if [ "$IS_WINDOWS" = 1 ]; then
  FFCLI_BIN="$FFCLI/bin/ffmpeg.exe"; FFCLI_ASSET="$FFCLI_ASSET_WINDOWS"
else
  FFCLI_BIN="$FFCLI/bin/ffmpeg";     FFCLI_ASSET="$FFCLI_ASSET_LINUX"
fi

deps_present() {
  [ -e "$PB_DIR/$LIBDIR/libprotobuf.$LIBEXT" ] && \
  [ -e "$PAHO/$LIBDIR/lib$PAHO_CPP_LIBBASE.$LIBEXT" ] && \
  [ -e "$FF_PREFIX/lib/libavcodec.a" ] && \
  [ -e "$FFCLI_BIN" ] && \
  { [ "$IS_WINDOWS" = 1 ] || [ -x "$MOSQ/sbin/mosquitto" ]; }
}

# --- install: the pinned bundle, else build from source ----------------------
if [ "$PACK" = 0 ]; then
  if deps_present; then
    echo "==> deps already present in $DEPS"
    exit 0
  fi
  mkdir -p "$DEPS"
  if [ -n "$DEPS_BUNDLE_TAG" ] && [ -n "$BUNDLE_SHA" ]; then
    URL="${RM_DEPS_BUNDLE_URL:-https://github.com/$DEPS_REPO/releases/download/$DEPS_BUNDLE_TAG/rm-deps-$BUNDLE_PLATFORM-x86_64.$BUNDLE_EXT}"
    echo "==> deps bundle $DEPS_BUNDLE_TAG ($BUNDLE_PLATFORM)"
    TMP_ARC="$DEPS/rm-deps.$BUNDLE_EXT"
    trap 'rm -f "$TMP_ARC"' EXIT
    if curl -fL --retry 3 -o "$TMP_ARC" "$URL"; then
      # A mismatch is drift or tampering, never something to rebuild past.
      echo "$BUNDLE_SHA  $TMP_ARC" | sha256sum -c -
      if [ "$IS_WINDOWS" = 1 ]; then
        unzip -q -o "$TMP_ARC" -d "$DEPS"
      else
        tar xzf "$TMP_ARC" -C "$DEPS"
      fi
      rm -f "$TMP_ARC"
      deps_present || { echo "!! bundle unpacked but deps still incomplete" >&2; exit 1; }
      echo "==> deps ready in $DEPS"
      exit 0
    fi
    rm -f "$TMP_ARC"
    echo "!! could not fetch $URL (RM_DEPS_BUNDLE_URL=<url> uses a mirror)" >&2
  else
    echo "!! no prebuilt deps bundle pinned in scripts/deps.lock" >&2
  fi
  echo "==> building all deps from source instead" >&2
fi

mkdir -p "$DEPS"

# --- protobuf (protoc + C++ headers + libprotobuf) ---------------------------
# Self-contained: no system libprotobuf required.
if [ ! -e "$PB_DIR/$LIBDIR/libprotobuf.$LIBEXT" ]; then
  if [ ! -d "$SRC_DIR/src/google/protobuf" ]; then
    echo "==> protobuf $PROTOC_VER source"
    curl -fL -o "$DEPS/pb-cpp.tar.gz" \
      "https://github.com/protocolbuffers/protobuf/releases/download/v$PROTOC_VER/protobuf-cpp-$PROTOC_VER.tar.gz"
    tar xzf "$DEPS/pb-cpp.tar.gz" -C "$DEPS"
  fi
  echo "==> building protobuf $PROTOC_VER (protoc + libprotobuf)"
  cmake -S "$SRC_DIR/cmake" -B "$DEPS/pb-build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PB_DIR" \
    -DCMAKE_INSTALL_LIBDIR="$LIBDIR" \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -Dprotobuf_BUILD_TESTS=OFF -Dprotobuf_BUILD_SHARED_LIBS="$PB_SHARED" \
    -Dprotobuf_WITH_ZLIB=OFF -Dprotobuf_BUILD_CONFORMANCE=OFF \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
  cmake --build "$DEPS/pb-build" --target install
fi

# --- Eclipse Paho C ----------------------------------------------------------
if [ ! -e "$PAHO/$LIBDIR/lib$PAHO_C_LIBBASE.$LIBEXT" ] || \
   [ ! -e "$PAHO/$LIBDIR/cmake/eclipse-paho-mqtt-c/eclipse-paho-mqtt-cConfig.cmake" ]; then
  echo "==> paho.mqtt.c $PAHO_C_VER"
  git clone --depth 1 -b "$PAHO_C_VER" https://github.com/eclipse-paho/paho.mqtt.c "$DEPS/paho.mqtt.c"
  # v1.3.16 bug (also in master): MQTTAsync.c uses `DWORD rc` with
  # Paho_thread_create_mutex(int*); GCC 14+ (MSYS2 MINGW64) errors on the
  # incompatible pointer. int and DWORD are both 32-bit, so just retype it.
  sed -i 's/DWORD rc = 0;/int rc = 0;/' "$DEPS/paho.mqtt.c/src/MQTTAsync.c"
  cmake -S "$DEPS/paho.mqtt.c" -B "$DEPS/paho-c-build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PAHO" \
    -DCMAKE_INSTALL_LIBDIR="$LIBDIR" \
    -DPAHO_WITH_SSL=OFF -DPAHO_BUILD_STATIC=ON -DPAHO_BUILD_SHARED="$PAHO_SHARED" \
    -DPAHO_BUILD_SAMPLES=OFF -DPAHO_ENABLE_TESTING=OFF
  cmake --build "$DEPS/paho-c-build" --target install
fi

# --- Eclipse Paho C++ --------------------------------------------------------
if [ ! -e "$PAHO/$LIBDIR/lib$PAHO_CPP_LIBBASE.$LIBEXT" ] || \
   [ ! -e "$PAHO/$LIBDIR/cmake/PahoMqttCpp/PahoMqttCppConfig.cmake" ]; then
  echo "==> paho.mqtt.cpp $PAHO_CPP_VER"
  git clone --depth 1 -b "$PAHO_CPP_VER" https://github.com/eclipse-paho/paho.mqtt.cpp "$DEPS/paho.mqtt.cpp"
  cmake -S "$DEPS/paho.mqtt.cpp" -B "$DEPS/paho-cpp-build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PAHO" \
    -DCMAKE_INSTALL_LIBDIR="$LIBDIR" \
    -DCMAKE_PREFIX_PATH="$PAHO" \
    -Declipse-paho-mqtt-c_DIR="$PAHO/$LIBDIR/cmake/eclipse-paho-mqtt-c" \
    -DPAHO_WITH_SSL=OFF \
    -DPAHO_BUILD_STATIC=ON -DPAHO_BUILD_SHARED="$PAHO_SHARED" \
    -DPAHO_BUILD_SAMPLES=OFF -DPAHO_BUILD_TESTS=OFF
  cmake --build "$DEPS/paho-cpp-build" --target install
fi

# --- Eclipse Mosquitto broker (local test broker; Linux only) ----------------
# Broker only: apps/clients/plugins/TLS/cJSON/docs off, so the binary needs only libc.
# (DOCUMENTATION=OFF avoids the hard xsltproc requirement in man/CMakeLists.txt.)
if [ "$IS_WINDOWS" = 0 ] && [ ! -x "$MOSQ/sbin/mosquitto" ]; then
  echo "==> mosquitto $MOSQ_VER"
  if [ ! -d "$DEPS/mosquitto-$MOSQ_VER" ]; then
    curl -fL -o "$DEPS/mosquitto.tar.gz" \
      "https://github.com/eclipse/mosquitto/archive/refs/tags/v$MOSQ_VER.tar.gz"
    tar xzf "$DEPS/mosquitto.tar.gz" -C "$DEPS"
  fi
  cmake -S "$DEPS/mosquitto-$MOSQ_VER" -B "$DEPS/mosq-build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$MOSQ" \
    -DCMAKE_INSTALL_LIBDIR="$LIBDIR" \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DWITH_BROKER=ON -DWITH_CLIENTS=OFF -DWITH_APPS=OFF -DWITH_PLUGINS=OFF \
    -DWITH_TLS=OFF -DWITH_CJSON=OFF -DWITH_SYSTEMD=OFF -DWITH_SOCKS=OFF \
    -DDOCUMENTATION=OFF
  cmake --build "$DEPS/mosq-build" --target install
fi

# --- FFmpeg (slim static: software + VAAPI + NVDEC/NVENC) --------------------
# Dev headers for VAAPI (libva) and NVIDIA (ffnvcodec) land in deps/ffmpeg-deps/.
# VAAPI headers come from the distro's -devel package (dnf/apt download, extracted
# without installing); if unavailable, VAAPI is skipped and only software +
# NVDEC remain. First build takes a few minutes.
build_ffmpeg() {
  if [ -e "$FF_PREFIX/lib/libavcodec.a" ]; then
    echo "==> ffmpeg $FFMPEG_VER already built, skipping"
    return 0
  fi
  local TMP; TMP="$(mktemp -d)"

  # NVDEC/NVENC headers (header-only, dynamically linked at runtime).
  # Always (re)install: ffnvcodec.pc embeds an absolute prefix from whoever
  # generated it, so a fresh clone (CI) must regenerate it for this machine.
  echo "==> nv-codec-headers $NVCODEC_TAG (NVDEC/NVENC)"
  [ -d "$DEPS/nv-codec-headers/.git" ] || \
    git clone --depth 1 -b "$NVCODEC_TAG" https://github.com/FFmpeg/nv-codec-headers.git "$DEPS/nv-codec-headers"
  make -C "$DEPS/nv-codec-headers" install PREFIX="$FFDEPS" >/dev/null

  local HAVE_VA=0
  mkdir -p "$FFDEPS/include" "$FFDEPS/lib/pkgconfig"
  if pkg-config --exists libva libva-drm libdrm 2>/dev/null; then
    HAVE_VA=1                                 # system -devel already installed
    # Refresh the .pc files: the committed ones may carry another machine's
    # absolute prefix, which would break configure on a fresh clone (CI).
    local p src
    for p in libva libva-drm libdrm; do
      src="$(pkg-config --variable=pcfiledir "$p" 2>/dev/null)/$p.pc"
      if [ -f "$src" ]; then cp "$src" "$FFDEPS/lib/pkgconfig/$p.pc"; fi
    done
  elif command -v dnf >/dev/null 2>&1; then
    echo "==> libva-devel / libdrm-devel headers via dnf download"
    # The -devel packages ship headers plus a dangling `libva.so -> libva.so.2`
    # symlink; the real libraries live in the runtime packages, so fetch those too.
    ( cd "$TMP" && dnf download --destdir rpm libva-devel libdrm-devel libva libdrm >/dev/null 2>&1 )
    ( cd "$TMP" && for r in rpm/*x86_64.rpm; do rpm2cpio "$r" | cpio -idm --quiet; done )
    cp -r "$TMP/usr/include/va" "$FFDEPS/include/va"
    cp -r "$TMP/usr/include/libdrm" "$FFDEPS/include/libdrm"
    # xf86drm*.h live at the include root (Fedora and Debian/Ubuntu alike), but
    # ffmpeg's libdrm check does #include <xf86drm.h>; drop them beside the rest.
    cp "$TMP/usr/include/"xf86drm*.h "$FFDEPS/include/libdrm/"
    local p
    for p in libva libva-drm libdrm; do
      sed -e "s|^prefix=.*|prefix=$FFDEPS|" -e "s|^libdir=.*|libdir=$FFDEPS/lib|" \
          "$TMP/usr/lib64/pkgconfig/$p.pc" > "$FFDEPS/lib/pkgconfig/$p.pc"
    done
    local lib d
    for lib in libva libva-drm libdrm; do
      for d in "$TMP/usr/lib64" /usr/lib64; do
        [ -e "$d/$lib.so.2" ] || continue
        cp -fL "$d/$lib.so.2" "$FFDEPS/lib/$lib.so.2"
        ln -sf "$lib.so.2" "$FFDEPS/lib/$lib.so"
        break
      done
    done
    # Only enable VAAPI if the runtime library actually landed; the client link
    # keys on $FFDEPS/lib/libva.so, so a header-only tree would build vaapi code
    # we cannot link.
    if [ -e "$FFDEPS/lib/libva.so" ]; then
      HAVE_VA=1
    else
      echo "!! libva runtime library missing from downloaded packages; disabling VAAPI"
    fi
  elif command -v apt-get >/dev/null 2>&1; then
    echo "==> libva-dev / libdrm-dev headers via apt-get download"
    # -dev ships headers + a dangling `libva.so` symlink; the runtime packages hold
    # the actual libraries (libva2/libva-drm2/libdrm2), which CI runners lack.
    ( cd "$TMP" && apt-get download libva-dev libdrm-dev libva2 libva-drm2 libdrm2 >/dev/null 2>&1 )
    ( cd "$TMP" && for d in *.deb; do dpkg-deb -x "$d" .; done )
    cp -r "$TMP/usr/include/va" "$FFDEPS/include/va"
    cp -r "$TMP/usr/include/libdrm" "$FFDEPS/include/libdrm"
    # xf86drm*.h live at the include root (Fedora and Debian/Ubuntu alike), but
    # ffmpeg's libdrm check does #include <xf86drm.h>; drop them beside the rest.
    cp "$TMP/usr/include/"xf86drm*.h "$FFDEPS/include/libdrm/"
    local p
    for p in libva libva-drm libdrm; do
      sed -e "s|^prefix=.*|prefix=$FFDEPS|" -e "s|^libdir=.*|libdir=$FFDEPS/lib|" \
          "$TMP/usr/lib/x86_64-linux-gnu/pkgconfig/$p.pc" > "$FFDEPS/lib/pkgconfig/$p.pc"
    done
    local lib d
    for lib in libva libva-drm libdrm; do
      for d in "$TMP/usr/lib/x86_64-linux-gnu" /usr/lib/x86_64-linux-gnu; do
        [ -e "$d/$lib.so.2" ] || continue
        cp -fL "$d/$lib.so.2" "$FFDEPS/lib/$lib.so.2"
        ln -sf "$lib.so.2" "$FFDEPS/lib/$lib.so"
        break
      done
    done
    # Only enable VAAPI if the runtime library actually landed; the client link
    # keys on $FFDEPS/lib/libva.so, so a header-only tree would build vaapi code
    # we cannot link.
    if [ -e "$FFDEPS/lib/libva.so" ]; then
      HAVE_VA=1
    else
      echo "!! libva runtime library missing from downloaded packages; disabling VAAPI"
    fi
  else
    echo "!! libva-dev not found; building FFmpeg without VAAPI (software + NVDEC only)"
  fi

  if [ ! -d "$FF_SRC" ]; then
    echo "==> ffmpeg $FFMPEG_VER source"
    curl -fL -o "$DEPS/ffmpeg.tar.xz" "https://ffmpeg.org/releases/ffmpeg-$FFMPEG_VER.tar.xz"
    tar xf "$DEPS/ffmpeg.tar.xz" -C "$DEPS"
  fi

  local VA_FLAGS="--disable-vaapi --disable-libdrm"
  if [ "$HAVE_VA" = 1 ]; then VA_FLAGS="--enable-vaapi --enable-libdrm"; fi
  echo "==> configuring ffmpeg (soft + vaapi=$HAVE_VA + nvdec)"
  ( cd "$FF_SRC" && \
    PKG_CONFIG_PATH="$FFDEPS/lib/pkgconfig" ./configure \
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
  rm -rf "$TMP"
}
build_ffmpeg

# --- static ffmpeg CLI (simulator "import video") ----------------------------
# The simulator spawns an external `ffmpeg` to transcode to HEVC via libx265,
# which the slim libavcodec above does not provide. Fetch a prebuilt, static
# (BtbN GPL build, includes libx265) so the bundled app needs no system ffmpeg.
if [ ! -e "$FFCLI_BIN" ]; then
  echo "==> static ffmpeg CLI (simulator video import)"
  DL="$DEPS/ffmpeg-cli-dl"
  REL_URL="https://github.com/BtbN/FFmpeg-Builds/releases/download/$FFCLI_RELEASE"
  rm -rf "$DL"; mkdir -p "$DL" "$FFCLI/bin"
  curl -fL --retry 3 -o "$DL/$FFCLI_ASSET" "$REL_URL/$FFCLI_ASSET"
  # Pinned release tag, but BtbN names each asset by git-describe; verify against
  # the checksums.sha256 published in that same release instead of hardcoding.
  curl -fL --retry 3 -o "$DL/checksums.sha256" "$REL_URL/checksums.sha256"
  ( cd "$DL" && grep -F " $FFCLI_ASSET" checksums.sha256 > sum && sha256sum -c sum )
  if [ "$IS_WINDOWS" = 1 ]; then
    unzip -j -o "$DL/$FFCLI_ASSET" '*/bin/ffmpeg.exe' -d "$FFCLI/bin" >/dev/null
  else
    tar xf "$DL/$FFCLI_ASSET" -C "$DL"
    cp "$(find "$DL" -type f -name ffmpeg -print -quit)" "$FFCLI_BIN"
    chmod +x "$FFCLI_BIN"
  fi
  rm -rf "$DL"
fi

if [ "$PACK" = 0 ]; then
  deps_present || { echo "!! deps incomplete in $DEPS after the build" >&2; exit 1; }
  echo "==> deps ready in $DEPS"
  exit 0
fi

# --- pack: one relocatable archive -------------------------------------------------
# Only install trees, never the sources/build dirs/tarballs.
DIRS=(protobuf3196 paho ffmpeg ffmpeg-cli)
if [ "$IS_WINDOWS" = 0 ]; then DIRS+=(ffmpeg-deps mosquitto); fi
for d in "${DIRS[@]}"; do
  [ -e "$DEPS/$d" ] || { echo "!! $DEPS/$d missing after the build" >&2; exit 1; }
done

# Absolute symlinks under ffmpeg-deps/lib and absolute prefixes inside the
# installed .pc files would break on the machine that unpacks this.
for l in "$DEPS"/ffmpeg-deps/lib/*.so "$DEPS"/ffmpeg-deps/lib/*.so.*; do
  [ -L "$l" ] || continue
  t="$(readlink "$l")"
  case "$t" in /*) ;; *) continue ;; esac
  base="$(basename "$t")"
  [ -e "$DEPS/ffmpeg-deps/lib/$base" ] || cp -fL "$l" "$DEPS/ffmpeg-deps/lib/$base"
  ln -sfn "$base" "$l"
done
for d in "${DIRS[@]}"; do
  find "$DEPS/$d" -path '*/pkgconfig/*.pc' -print0
done | while IFS= read -r -d '' f; do
  depth="$(printf '%s' "${f#"$DEPS"/}" | tr -cd / | wc -c | tr -d ' ')"
  rel="$(printf '../%.0s' $(seq 1 "$depth"))"; rel="${rel%/}"
  sed -i "s|$DEPS|\\\${pcfiledir}/$rel|g" "$f"
done

if [ "$IS_WINDOWS" = 1 ]; then
  OUT="$DEPS/rm-deps-windows-x86_64.zip"
  ( cd "$DEPS" && rm -f "$OUT" && zip -qr "$OUT" "${DIRS[@]}" )
else
  OUT="$DEPS/rm-deps-linux-x86_64.tar.gz"
  tar czf "$OUT" -C "$DEPS" "${DIRS[@]}"
fi
echo "==> $OUT"
sha256sum "$OUT" | tee "$OUT.sha256"
