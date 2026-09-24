#!/usr/bin/env bash
# Builds the C++ third-party deps used by client/ and simulator/ into third_party/.
# Idempotent: skips steps whose output already exists.
#
#   protobuf : protoc 3.19.6 binary + C++ headers (source tree) + libprotobuf
#   paho     : Eclipse Paho MQTT C v1.3.16 + C++ v1.6.0, SSL disabled
#   mosquitto: Eclipse Mosquitto broker v2.0.20 (local test broker, no Python)
#
# Requires: git, curl, cmake, ninja, a C++17 compiler (and a C compiler).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"          # third_party/
PROTOC_VER=3.19.6
PAHO_C_VER=v1.3.16
PAHO_CPP_VER=v1.6.0
MOSQ_VER=2.0.20

PB_DIR="$ROOT/protobuf3196"
SRC_DIR="$ROOT/protobuf-$PROTOC_VER"
PAHO="$ROOT/paho"
MOSQ="$ROOT/mosquitto"

# --- protobuf: build from source (protoc + C++ headers + libprotobuf) --------
# Self-contained: no system libprotobuf required.
if [ ! -e "$PB_DIR/lib64/libprotobuf.so" ]; then
  if [ ! -d "$SRC_DIR/src/google/protobuf" ]; then
    echo "==> protobuf $PROTOC_VER source"
    curl -fL -o "$ROOT/pb-cpp.tar.gz" \
      "https://github.com/protocolbuffers/protobuf/releases/download/v$PROTOC_VER/protobuf-cpp-$PROTOC_VER.tar.gz"
    tar xzf "$ROOT/pb-cpp.tar.gz" -C "$ROOT"
  fi
  echo "==> building protobuf $PROTOC_VER (protoc + libprotobuf)"
  cmake -S "$SRC_DIR/cmake" -B "$ROOT/pb-build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PB_DIR" \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -Dprotobuf_BUILD_TESTS=OFF -Dprotobuf_BUILD_SHARED_LIBS=ON \
    -Dprotobuf_WITH_ZLIB=OFF -Dprotobuf_BUILD_CONFORMANCE=OFF \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
  cmake --build "$ROOT/pb-build" --target install
fi

# --- Eclipse Paho C ----------------------------------------------------------
if [ ! -e "$PAHO/lib64/libpaho-mqtt3a.so" ]; then
  echo "==> paho.mqtt.c $PAHO_C_VER"
  git clone --depth 1 -b "$PAHO_C_VER" https://github.com/eclipse-paho/paho.mqtt.c "$ROOT/paho.mqtt.c"
  cmake -S "$ROOT/paho.mqtt.c" -B "$ROOT/paho-c-build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PAHO" \
    -DPAHO_WITH_SSL=OFF -DPAHO_BUILD_STATIC=ON -DPAHO_BUILD_SHARED=ON \
    -DPAHO_BUILD_SAMPLES=OFF -DPAHO_ENABLE_TESTING=OFF
  cmake --build "$ROOT/paho-c-build" --target install
fi

# --- Eclipse Paho C++ --------------------------------------------------------
if [ ! -e "$PAHO/lib64/libpaho-mqttpp3.so" ]; then
  echo "==> paho.mqtt.cpp $PAHO_CPP_VER"
  git clone --depth 1 -b "$PAHO_CPP_VER" https://github.com/eclipse-paho/paho.mqtt.cpp "$ROOT/paho.mqtt.cpp"
  cmake -S "$ROOT/paho.mqtt.cpp" -B "$ROOT/paho-cpp-build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PAHO" \
    -DCMAKE_PREFIX_PATH="$PAHO" -DPAHO_WITH_SSL=OFF \
    -DPAHO_BUILD_SAMPLES=OFF -DPAHO_BUILD_TESTS=OFF
  cmake --build "$ROOT/paho-cpp-build" --target install
fi

# --- Eclipse Mosquitto broker (local test broker; replaces the Python amqtt) --
# Broker only: apps/clients/plugins/TLS/cJSON off, so the binary needs only libc.
if [ ! -x "$MOSQ/sbin/mosquitto" ]; then
  echo "==> mosquitto $MOSQ_VER"
  if [ ! -d "$ROOT/mosquitto-$MOSQ_VER" ]; then
    curl -fL -o "$ROOT/mosquitto.tar.gz" \
      "https://github.com/eclipse/mosquitto/archive/refs/tags/v$MOSQ_VER.tar.gz"
    tar xzf "$ROOT/mosquitto.tar.gz" -C "$ROOT"
  fi
  cmake -S "$ROOT/mosquitto-$MOSQ_VER" -B "$ROOT/mosq-build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$MOSQ" \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DWITH_BROKER=ON -DWITH_CLIENTS=OFF -DWITH_APPS=OFF -DWITH_PLUGINS=OFF \
    -DWITH_TLS=OFF -DWITH_CJSON=OFF -DWITH_SYSTEMD=OFF -DWITH_SOCKS=OFF
  cmake --build "$ROOT/mosq-build" --target install
fi

# --- FFmpeg (slim static: software + VAAPI + NVDEC/NVENC) --------------------
"$ROOT/build-ffmpeg.sh"

echo "==> deps ready under $ROOT"
