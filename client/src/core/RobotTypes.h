#pragma once

namespace rm {

// 兵种编号（官方附录二的队内编号，也是 MQTT clientID 的个位/后两位）。
// 2027 赛季起原英雄与工程合并为"重装机器人"，沿用编号 1。
namespace RobotType {
constexpr int Heavy = 1; // 重装（原英雄 + 工程）
constexpr int Infantry3 = 3;
constexpr int Infantry4 = 4;
constexpr int Infantry5 = 5;
constexpr int Aerial = 6;
constexpr int Sentry = 7;
constexpr int Dart = 8;
constexpr int Radar = 9;
} // namespace RobotType

} // namespace rm
