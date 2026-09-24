# AGENTS.md

RoboMaster 自定义客户端 + 裁判系统模拟器。

```
proto/  cmake/RmDeps.cmake  common/MqttClient  client/(→rm_client)  simulator/(→rm_simulator)  third_party/ 
```

## 准备与构建

- 依赖随项目携带。**首次必须先跑** `./third_party/setup-deps.sh`（源码构建 protobuf 3.19.6 + Paho + Mosquitto 到 `third_party/`；不链接系统库，本地联调不需要 Python）。缺依赖时 CMake 直接 FATAL_ERROR。
- Qt 需本地安装，CMake 自动探测 `~/Qt/6.11.2/gcc_64`。
- ```bash
  cmake -S client    -B client/build    -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build client/build
  cmake -S simulator -B simulator/build -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build simulator/build
  ctest --test-dir client/build          # 单测 user_actions；CMake 已设 QT_QPA_PLATFORM=offscreen
  ```

## 运行与端到端联调

- 启动模拟器：`./simulator/run-local-sim.sh`（Mosquitto `127.0.0.1:1883`）；可覆盖 `RM_MQTT_HOST/RM_MQTT_PORT/RM_CLIENT_ROBOT_ID`。
- 客户端 `./client/build/rm_client`：机器人配置面板；`Esc` 在 配置/比赛 间切换。
- 无头/自动化调试钩子（`MainWindow::showEvent`）：`RM_AUTOCONNECT=1`（读 QSettings 端点直接连）、`RM_ACTION_PANEL=<ammo|assembly|deploy|recover|revive|marker|dart|air>`、`RM_PROMPT_ACTION=<action id>`、`RM_SHOW_VIDEO_LOG=1`、`RM_BIG_MAP=1`。QSettings 落在 `~/.config/RM/RM Custom Client.conf`。
- 杀进程用 `pkill -x rm_client`。

## 架构不变量（改代码前必读）

- `proto/robomaster.proto` 是 client 与 simulator 的**唯一公用协议**；改动后两边 build 都会用 `third_party/protobuf3196/bin/protoc` 重新生成。**topic 名 = 消息名**。
- 分层：`core/`（纯数据/逻辑，不依赖 UI）← `net/`（GameData 解析，依赖 core）← `ui/`。**`net` 不得 include `ui`**。HUD 视图模型在 `core/HudData.h`。
- 兵种功能模块化：每兵种一个 `core/robots/<Robot>Actions.cpp`；增删兵种功能 = 加/删文件 + 在 `core/UserActions.cpp` 的 `makeActions()` 注册一行。
- `common/MqttClient` 由 client 与 simulator 共用（Paho 封装，QMetaObject 跨线程回 Qt 线程）。
- 官方协议只定义键位编码（`KeyboardMouseControl.keyboard_value` bit0-15），**功能→键的映射是客户端自定义**，默认键位在 `core/UserActions.cpp`（`~` 浮窗标题内仅作键位提示，不可改）。
- 键鼠 75Hz 上行在 `ui/MainWindow`（`m_inputTimer` ~13ms 采样，非逐事件发）。鼠标为**原始增量**：仅 `inputActive() && 无 Tab/标记` 时才回中取增量并隐藏光标（`mouseCaptured()`），**任何浮窗可见（含死亡/复活读条，即 `actionOverlay()->isVisible()`）都显示光标且只发 0 增量**，避免云台误转。

## 易踩的坑

- HUD 全部用 QPainter 像素字号：上层字体若以像素定义，`QFont::pointSizeF()` 返回 -1。一律 `setPixelSize()`，**不要** `setPointSizeF(pointSizeF()±n)`。
- 叠加层子控件的显隐判断用 `isHidden()`，**不能用 `isVisible()`**（父 overlay 隐藏时 `isVisible()` 返回 false，会静默跳过逻辑）。
- `README.md` 细节可能滞后于代码。冲突时以代码为准。

## 静态检查

见 README「代码规范与静态检查」。关键：**cppcheck 2.22 不会自动加载 `.cppcheck`，必须显式 `--suppressions-list=.cppcheck`**，且抑制项路径写相对仓库根（如 `third_party/*`，`*/third_party/*` 不匹配）。
