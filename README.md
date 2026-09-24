# RoboMaster 自定义客户端

```
selfcontrol/
├─ proto/robomaster.proto   # 共享协议（官方2026年 V2.0.0协议版本）
├─ cmake/RmDeps.cmake       # 共享的依赖解析 + proto/paho 集成
├─ client/                  # 自定义客户端（Qt6 Widgets）→ rm_client
├─ simulator/               # 裁判系统模拟器（Qt6 Core）→ rm_simulator
├─ third_party/             # 第三方依赖
```

## 环境要求

| 项 | 版本 / 说明 |
|---|---|
| 系统 | Linux（Fedora 44 验证）/ Windows |
| 编译器 | 支持 C++17 |
| CMake | ≥ 3.21 |
| Qt | **6.11.2**，默认探测 `~/Qt/6.11.2/gcc_64` |


## 构建

### 1. 准备依赖（首次）

```bash
./third_party/setup-deps.sh
```

### 2. 构建客户端

```bash
cmake -S client -B client/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build client/build
```

### 3. 构建模拟器

```bash
cmake -S simulator -B simulator/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build simulator/build
```

---

## CI 与分发

`.github/workflows/build.yml` 在 push / PR / tag 时构建两个平台的**独立版**（自包含，无需预装 Qt）：

| Job | 运行器 | 产物 |
|---|---|---|
| Linux x86_64 | ubuntu-22.04 + Qt 6.11.2 | `rm_client.AppImage` / `rm_simulator.AppImage` |
| Windows x86_64 | windows-latest + MSYS2/MinGW-w64 + Qt6 | `rm_client.zip` / `rm_simulator.zip` |

依赖（protobuf / paho / ffmpeg）由 `third_party/setup-deps.sh` 从源码构建，CI 按脚本哈希缓存。AppImage 由 `packaging/linux-appimage.sh`（linuxdeploy + Qt 插件）打包；Windows zip 由 `packaging/bundle-windows.sh`（`windeployqt` + MinGW 运行时）打包。

两个平台构建成功后，`release` job 自动创建 GitHub Release 并附带全部产物：
- **push tag（`v*`）**：版本号取 tag 名，如 `v1.0.0`；
- **push `main`/`master`**：自动版本号 `v0.1.<run_number>`（构建号单调递增）。

产物按 `rm_client-<version>-linux-x86_64.AppImage` 之类命名。PR 只构建、不发布；`workflow_dispatch` 只上传 Artifact。

> 模拟器的「导入视频」功能在运行时调用外部 `ffmpeg` 命令（把本地视频转成 HEVC）。该 CLI **已随独立包附带**（由 `setup-deps.sh` 下载 BtbN 预编译静态 GPL 构建，含 libx265），运行时优先使用程序同目录下的 `ffmpeg`，找不到时回退到系统 `PATH`。

---

## 运行

### 模拟器

```bash
./simulator/run-local-sim.sh
```

### 启动客户端

```bash
./client/build/rm_client
```

## 端点与配置

| Profile | MQTT | 视频 UDP | 自定义客户端 IP |
|---|---|---|---|
| 裁判系统 | `192.168.12.1:3333` | `3334` | `192.168.12.2` |
| 模拟器 | `127.0.0.1:1883` | `3334` | — |


## 技术栈

| 层 | 选型 |
|---|---|
| UI | Qt6 Widgets（全屏 HUD 用 QPainter 自绘） |
| MQTT | Eclipse Paho C++ |
| 序列化 | Google protobuf（libprotobuf 3.19.6） |
| 视频 | FFmpeg（随项目静态构建：软解 + VAAPI/NVDEC 硬解，硬解失败自动回退软解） |
| 构建 | CMake + Ninja |

---

## 代码规范与静态检查

配置文件：`.gitignore`、`.clang-format`、`.clang-tidy`、`.cppcheck`。

```bash
# 格式化（仓库根目录）
clang-format -i $(find client simulator common -name '*.h' -o -name '*.cpp')

# clang-tidy（需编译数据库，CMake 已默认导出）
cmake -S client -B client/build -G Ninja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
clang-tidy -p client/build client/src/**/*.cpp

# cppcheck
cppcheck --project=client/build/compile_commands.json \
         --enable=warning,style,performance,portability \
         --inline-suppr --suppressions-list=.cppcheck \
         -i third_party -i reference -i client/build -i simulator/build
```


## 参考

- `RM26_Client/` — 复旦星云 EGA，协议与模拟器底层方案的参考实现。
- `ACE_RM26_CustomClient/` — 东莞理工 ACE，UI 布局与交互参考（Dear ImGui + Vulkan）。
