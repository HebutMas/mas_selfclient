// 通用指令模块：跨兵种的上行功能，通过 UserAction::robots 声明各自适用的兵种。
#include "core/Protocol.h"
#include "core/robots/RobotActions.h"

namespace rm {
namespace robot {

QVector<UserAction> commonActions()
{
    using namespace RobotType;
    QVector<UserAction> a;

    a << makeAction("common.buy17", "通用指令", "兑换17mm发弹量", "CommonCommand",
                    [](int p) { return proto::commonCommand(1, p); },
                    {Infantry3, Infantry4, Infantry5, Sentry}, Qt::Key_Q,
                    {QObject::tr("数量（10 的倍数）"), 10, 300, 10, 10}, "ammo");
    a << makeAction("common.buy42", "通用指令", "兑换42mm发弹量", "CommonCommand",
                    [](int p) { return proto::commonCommand(2, p); }, {Heavy}, Qt::Key_E,
                    {QObject::tr("数量"), 1, 10, 1, 1}, "ammo");
    a << makeAction("common.respawn", "通用指令", "确认复活", "CommonCommand",
                    [](int) { return proto::commonCommand(3); },
                    {Heavy, Infantry3, Infantry4, Infantry5, Sentry}, Qt::Key_R, {}, "revive");
    a << makeAction("common.instantRespawn", "通用指令", "兑换立即复活", "CommonCommand",
                    [](int) { return proto::commonCommand(4); },
                    {Heavy, Infantry3, Infantry4, Infantry5, Sentry}, Qt::Key_F, {}, "revive");
    a << makeAction("common.remoteAmmo", "通用指令", "远程兑换发弹量", "CommonCommand",
                    [](int p) { return proto::commonCommand(5, p); }, {Aerial, Sentry}, Qt::Key_G,
                    {QObject::tr("数量"), 1, 300, 1, 1}, "ammo");
    a << makeAction("common.remoteHeal", "通用指令", "远程兑换血量", "CommonCommand",
                    [](int) { return proto::commonCommand(6); }, {Aerial, Sentry}, Qt::Key_H, {},
                    "recover");
    a << makeAction("common.performance", "通用指令", "性能体系(冷却优先)",
                    "RobotPerformanceSelectionCommand",
                    [](int) {
                        robomaster::RobotPerformanceSelectionCommand m;
                        m.set_shooter(1);
                        return proto::pack(m);
                    },
                    {Heavy, Infantry3, Infantry4, Infantry5});
    return a;
}

} // namespace robot
} // namespace rm
