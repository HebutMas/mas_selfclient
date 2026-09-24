#!/usr/bin/env bash
# 本机联调启动器：MQTT Broker(mosquitto, 纯 C) + 自定义模拟器(rm_simulator)。
# 端口全部可配置，默认 = 本机 dev profile（见 docs/架构设计.md 2.6）。
# 不依赖 Python：broker 来自 third_party/mosquitto（由 setup-deps.sh 构建）。
#
# 用法: ./simulator/run-local-sim.sh        （Ctrl-C 退出）
# 覆盖: RM_MQTT_HOST=127.0.0.1 RM_MQTT_PORT=1883 ./simulator/run-local-sim.sh
set -euo pipefail

SIM_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SIM_DIR/.." && pwd)"
BIN="$SIM_DIR/build/rm_simulator"
MOSQUITTO="$ROOT/third_party/mosquitto/sbin/mosquitto"

# ---- 可配置端点 ----
MQTT_HOST="${RM_MQTT_HOST:-127.0.0.1}"
MQTT_PORT="${RM_MQTT_PORT:-1883}"        # 现场官方为 3333
ROBOT_ID="${RM_CLIENT_ROBOT_ID:-1}"      # 所连机器人 ID = MQTT clientID

[ -x "$MOSQUITTO" ] || {
  echo "缺少 mosquitto broker，运行: ./third_party/setup-deps.sh"; exit 1; }

# mosquitto listener 需要可绑定的地址；localhost 归一化为 127.0.0.1
BIND_ADDR="$MQTT_HOST"
[ "$BIND_ADDR" = "localhost" ] && BIND_ADDR=127.0.0.1
CONF="$(mktemp -t rm-mosquitto.XXXXXX.conf)"
cat >"$CONF" <<EOF
listener $MQTT_PORT $BIND_ADDR
allow_anonymous true
persistence false
EOF

if [ ! -x "$BIN" ]; then
  echo "构建 rm_simulator ..."
  cmake -S "$SIM_DIR" -B "$SIM_DIR/build" -G Ninja -DCMAKE_BUILD_TYPE=Debug >/dev/null
  cmake --build "$SIM_DIR/build" >/dev/null
fi

echo "Broker   : $BIND_ADDR:$MQTT_PORT (mosquitto)"
echo "Simulator: rm_simulator  clientId=rm_simulator  robotId=$ROBOT_ID"
echo "客户端    : 连 $MQTT_HOST:$MQTT_PORT，clientID 填 $ROBOT_ID"

"$MOSQUITTO" -c "$CONF" >/tmp/rm-broker.log 2>&1 &
BROKER_PID=$!
trap 'kill $BROKER_PID 2>/dev/null || true; rm -f "$CONF"' EXIT
sleep 1.0

exec "$BIN" --mqtt-host "$MQTT_HOST" --mqtt-port "$MQTT_PORT"
