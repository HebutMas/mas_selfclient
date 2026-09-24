#pragma once

#include "core/UserActions.h"

namespace rm {
namespace robot {

// 在兵种模块文件中构造一条功能定义（实现见 core/UserActions.cpp）。
UserAction makeAction(const char* id, const char* group, const char* label, const char* topic,
                      std::function<QByteArray(int)> build, QVector<int> robots,
                      Qt::Key key = Qt::Key_unknown, ActionParam param = {},
                      const char* panel = nullptr);

// 每个兵种一个模块文件（core/robots/*.cpp），只放该兵种特有的上行功能。
// 新增/删除兵种：加/删对应 .cpp 并在核心汇总处注册即可。
QVector<UserAction> commonActions();   // 通用指令（跨兵种，按 robots 过滤）
QVector<UserAction> heavyActions();    // 重装（原英雄 + 工程）
QVector<UserAction> infantryActions(); // 步兵
QVector<UserAction> aerialActions();   // 空中
QVector<UserAction> sentryActions();   // 哨兵
QVector<UserAction> dartActions();     // 飞镖
QVector<UserAction> radarActions();    // 雷达（当前无上行功能，占位）

} // namespace robot
} // namespace rm
