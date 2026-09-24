#pragma once

// 官方上行指令的序列化小工具：把 protobuf 消息打包成 MQTT 载荷。各兵种模块与界面面板共用，避免重复定义。
#include <QByteArray>
#include <QtCore/qnamespace.h>

#include <string>

#include "robomaster.pb.h"

namespace rm {
namespace proto {

// 官方 KeyboardMouseControl.keyboard_value 的按键→bit 映射（协议 V2.0.0 2.2.1）：
// 0=W 1=S 2=A 3=D 4=Shift 5=Ctrl 6=Q 7=E 8=R 9=F 10=G 11=Z 12=X 13=C 14=V 15=B。
// 返回 bit 下标 0-15；不受支持的按键返回 -1。
inline int keyboardMouseBit(Qt::Key key)
{
    switch (key) {
    case Qt::Key_W: return 0;
    case Qt::Key_S: return 1;
    case Qt::Key_A: return 2;
    case Qt::Key_D: return 3;
    case Qt::Key_Shift: return 4;
    case Qt::Key_Control: return 5;
    case Qt::Key_Q: return 6;
    case Qt::Key_E: return 7;
    case Qt::Key_R: return 8;
    case Qt::Key_F: return 9;
    case Qt::Key_G: return 10;
    case Qt::Key_Z: return 11;
    case Qt::Key_X: return 12;
    case Qt::Key_C: return 13;
    case Qt::Key_V: return 14;
    case Qt::Key_B: return 15;
    default: return -1;
    }
}

// 序列化任意 protobuf 消息为字节流。
template <typename Msg>
QByteArray pack(const Msg& msg)
{
    std::string s;
    msg.SerializeToString(&s);
    return QByteArray(s.data(), static_cast<int>(s.size()));
}

inline QByteArray commonCommand(int cmd_type, int param = 0)
{
    robomaster::CommonCommand m;
    m.set_cmd_type(static_cast<uint32_t>(cmd_type));
    if (param) m.set_param(static_cast<uint32_t>(param));
    return pack(m);
}

inline QByteArray assemblyCommand(int operation, int difficulty = 0)
{
    robomaster::AssemblyCommand m;
    m.set_operation(static_cast<uint32_t>(operation));
    if (difficulty) m.set_difficulty(static_cast<uint32_t>(difficulty));
    return pack(m);
}

inline QByteArray sentryCtrl(int command_id)
{
    robomaster::SentryCtrlCommand m;
    m.set_command_id(static_cast<uint32_t>(command_id));
    return pack(m);
}

inline QByteArray airSupport(int command_id)
{
    robomaster::AirSupportCommand m;
    m.set_command_id(static_cast<uint32_t>(command_id));
    return pack(m);
}

inline QByteArray dartCommand(int target_id, bool open, bool launch)
{
    robomaster::DartCommand m;
    if (target_id) m.set_target_id(static_cast<uint32_t>(target_id));
    m.set_open(open);
    m.set_launch_confirm(launch);
    return pack(m);
}

} // namespace proto
} // namespace rm
