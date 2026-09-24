#pragma once

#include <QByteArray>
#include <QKeySequence>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

#include "core/RobotTypes.h"

namespace rm {

// 可选参数：label 非空时，触发该功能会弹出选择界面（如兑换弹量）。
struct ActionParam {
    QString label;
    int min = 0;
    int max = 0;
    int step = 1;
    int def = 0;
    QStringList choices; // 非空时用下拉框（取值 min..min+size-1）
};

// 一个"上行触发功能"：按钮与快捷键共用同一份定义（单一真相源）。
// 官方通信协议只定义 keyboard_value 的按键位，不规定功能到键的映射，
// 因此这里的默认键位属于客户端自定义，可在"按键设置"中修改。
struct UserAction {
    QString id;                          // 稳定 id，用于 QSettings 持久化键位，勿修改
    QString group;                       // 分组名（按钮分栏）
    QString label;                       // 显示名
    QString topic;                       // 官方 MQTT topic（= 消息名）
    std::function<QByteArray(int)> build; // 由参数值序列化出 protobuf 载荷
    QKeySequence defaultKey;             // 默认快捷键（可为空）
    QVector<int> robots;                 // 适用的兵种（RobotType）；空 = 全部
    ActionParam param;                   // 需要选参数时设置 label
    QString panel;                       // 专用面板名（ammo/assembly/...）；空 = 用确认弹窗
};

// 全部上行触发功能，顺序即界面顺序（由各兵种模块 + 通用模块汇总）。
const QVector<UserAction>& userActions();

// 某兵种适用的功能子集（robotType<=0 时返回全部）。
QVector<UserAction> userActionsForRobot(int robotType);

// 按 id 查找功能；找不到返回 nullptr。
const UserAction* userAction(const QString& id);

// 该功能是否需要参数界面（有参数选择或需确认）。
QByteArray buildActionPayload(const UserAction& a, int param);

// 键位持久化（QSettings "keys/<id>"）；未设置过时返回 defaultKey。
QKeySequence actionKey(const QString& id);
void setActionKey(const QString& id, const QKeySequence& seq);
void resetActionKeys();

} // namespace rm
