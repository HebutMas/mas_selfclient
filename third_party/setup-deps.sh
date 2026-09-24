#!/usr/bin/env bash
# Builds the C++ third-party deps used by client/ and simulator/ into third_party/.
# Idempotent: skips steps whose output already exists.
#
#   protobuf : protoc 3.19.6 binary + C++ headers (source tree) + libprotobuf
#   paho     : Eclipse Paho MQTT C v1.3.16 + C++ v1.6.0, SSL disabled
#   mosquitto: Eclipse Mosquitto broker v2.0.20 (local test broker; Linux only)
#   ffmpeg-cli: prebuilt static ffmpeg executable for the simulator's video
#               import (downloaded, not built)
#
# Runs on Linux and in an MSYS2 MINGW64 shell. On Windows protobuf/paho are
# built static (libprotobuf.a / libpaho-mqttpp3.a) and the mosquitto broker is
# skipped (only needed for local Linux dev).
#
# Requires: git, curl, cmake, ninja, a C++17 compiler (and a C compiler).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"          # third_party/
PROTOC_VER=3.19.6
PAHO_C_VER=v1.3.16
PAHO_CPP_VER=v1.6.0
MOSQ_VER=2.0.20

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

PB_DIR="$ROOT/protobuf3196"
SRC_DIR="$ROOT/protobuf-$PROTOC_VER"
PAHO="$ROOT/paho"
MOSQ="$ROOT/mosquitto"

# --- protobuf: build from source (protoc + C++ headers + libprotobuf) --------
# Self-contained: no system libprotobuf required.
if [ ! -e "$PB_DIR/$LIBDIR/libprotobuf.$LIBEXT" ]; then
  if [ ! -d "$SRC_DIR/src/google/protobuf" ]; then
    echo "==> protobuf $PROTOC_VER source"
    curl -fL -o "$ROOT/pb-cpp.tar.gz" \
      "https://github.com/protocolbuffers/protobuf/releases/download/v$PROTOC_VER/protobuf-cpp-$PROTOC_VER.tar.gz"
    tar xzf "$ROOT/pb-cpp.tar.gz" -C "$ROOT"
  fi
  echo "==> building protobuf $PROTOC_VER (protoc + libprotobuf)"
  cmake -S "$SRC_DIR/cmake" -B "$ROOT/pb-build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PB_DIR" \
    -DCMAKE_INSTALL_LIBDIR="$LIBDIR" \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -Dprotobuf_BUILD_TESTS=OFF -Dprotobuf_BUILD_SHARED_LIBS="$PB_SHARED" \
    -Dprotobuf_WITH_ZLIB=OFF -Dprotobuf_BUILD_CONFORMANCE=OFF \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
  cmake --build "$ROOT/pb-build" --target install
fi

# --- Eclipse Paho C ----------------------------------------------------------
if [ ! -e "$PAHO/$LIBDIR/lib$PAHO_C_LIBBASE.$LIBEXT" ] || \
   [ ! -e "$PAHO/$LIBDIR/cmake/eclipse-paho-mqtt-c/eclipse-paho-mqtt-cConfig.cmake" ]; then
  echo "==> paho.mqtt.c $PAHO_C_VER"
  git clone --depth 1 -b "$PAHO_C_VER" https://github.com/eclipse-paho/paho.mqtt.c "$ROOT/paho.mqtt.c"
  # v1.3.16 bug (also in master): MQTTAsync.c uses `DWORD rc` with
  # Paho_thread_create_mutex(int*); GCC 14+ (MSYS2 MINGW64) errors on the
  # incompatible pointer. int and DWORD are both 32-bit, so just retype it.
  sed -i 's/DWORD rc = 0;/int rc = 0;/' "$ROOT/paho.mqtt.c/src/MQTTAsync.c"
  cmake -S "$ROOT/paho.mqtt.c" -B "$ROOT/paho-c-build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PAHO" \
    -DCMAKE_INSTALL_LIBDIR="$LIBDIR" \
    -DPAHO_WITH_SSL=OFF -DPAHO_BUILD_STATIC=ON -DPAHO_BUILD_SHARED="$PAHO_SHARED" \
    -DPAHO_BUILD_SAMPLES=OFF -DPAHO_ENABLE_TESTING=OFF
  cmake --build "$ROOT/paho-c-build" --target install
fi

# --- Eclipse Paho C++ --------------------------------------------------------
if [ ! -e "$PAHO/$LIBDIR/lib$PAHO_CPP_LIBBASE.$LIBEXT" ] || \
   [ ! -e "$PAHO/$LIBDIR/cmake/PahoMqttCpp/PahoMqttCppConfig.cmake" ]; then
  echo "==> paho.mqtt.cpp $PAHO_CPP_VER"
  git clone --depth 1 -b "$PAHO_CPP_VER" https://github.com/eclipse-paho/paho.mqtt.cpp "$ROOT/paho.mqtt.cpp"
  cmake -S "$ROOT/paho.mqtt.cpp" -B "$ROOT/paho-cpp-build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PAHO" \
    -DCMAKE_INSTALL_LIBDIR="$LIBDIR" \
    -DCMAKE_PREFIX_PATH="$PAHO" \
    -Declipse-paho-mqtt-c_DIR="$PAHO/$LIBDIR/cmake/eclipse-paho-mqtt-c" \
    -DPAHO_WITH_SSL=OFF \
    -DPAHO_BUILD_STATIC=ON -DPAHO_BUILD_SHARED="$PAHO_SHARED" \
    -DPAHO_BUILD_SAMPLES=OFF -DPAHO_BUILD_TESTS=OFF
  cmake --build "$ROOT/paho-cpp-build" --target install
fi

# --- Eclipse Mosquitto broker (local test broker; Linux only) ----------------
# Broker only: apps/clients/plugins/TLS/cJSON/docs off, so the binary needs only libc.
# (DOCUMENTATION=OFF avoids the hard xsltproc requirement in man/CMakeLists.txt.)
if [ "$IS_WINDOWS" = 0 ] && [ ! -x "$MOSQ/sbin/mosquitto" ]; then
  echo "==> mosquitto $MOSQ_VER"
  if [ ! -d "$ROOT/mosquitto-$MOSQ_VER" ]; then
    curl -fL -o "$ROOT/mosquitto.tar.gz" \
      "https://github.com/eclipse/mosquitto/archive/refs/tags/v$MOSQ_VER.tar.gz"
    tar xzf "$ROOT/mosquitto.tar.gz" -C "$ROOT"
  fi
  cmake -S "$ROOT/mosquitto-$MOSQ_VER" -B "$ROOT/mosq-build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$MOSQ" \
    -DCMAKE_INSTALL_LIBDIR="$LIBDIR" \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DWITH_BROKER=ON -DWITH_CLIENTS=OFF -DWITH_APPS=OFF -DWITH_PLUGINS=OFF \
    -DWITH_TLS=OFF -DWITH_CJSON=OFF -DWITH_SYSTEMD=OFF -DWITH_SOCKS=OFF \
    -DDOCUMENTATION=OFF
  cmake --build "$ROOT/mosq-build" --target install
fi

# --- FFmpeg (slim static: software + VAAPI + NVDEC/NVENC) --------------------
"$ROOT/build-ffmpeg.sh"

# --- Static ffmpeg CLI (simulator "import video") ----------------------------
# The simulator spawns an external `ffmpeg` to transcode to HEVC via libx265,
# which the slim libavcodec above does not provide. Fetch a prebuilt, static
# (BtbN GPL build, includes libx265) so the bundled app needs no system ffmpeg.
FFCLI="$ROOT/ffmpeg-cli"
if [ "$IS_WINDOWS" = 1 ]; then
  FFCLI_BIN="$FFCLI/bin/ffmpeg.exe"
  FFCLI_URL="https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-gpl.zip"
else
  FFCLI_BIN="$FFCLI/bin/ffmpeg"
  FFCLI_URL="https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-linux64-gpl.tar.xz"
fi
if [ ! -e "$FFCLI_BIN" ]; then
  echo "==> static ffmpeg CLI (simulator video import)"
  DL="$ROOT/ffmpeg-cli-dl"
  rm -rf "$DL"; mkdir -p "$DL" "$FFCLI/bin"
  curl -fL --retry 3 -o "$DL/$(basename "$FFCLI_URL")" "$FFCLI_URL"
  if [ "$IS_WINDOWS" = 1 ]; then
    unzip -j -o "$DL"/*.zip '*/bin/ffmpeg.exe' -d "$FFCLI/bin" >/dev/null
  else
    tar xf "$DL"/*.tar.xz -C "$DL"
    cp "$(find "$DL" -type f -name ffmpeg -print -quit)" "$FFCLI_BIN"
    chmod +x "$FFCLI_BIN"
  fi
  rm -rf "$DL"
fi

echo "==> deps ready under $ROOT"
