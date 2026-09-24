// 哨兵（编号 7）特有功能：姿态切换、确认复活。
#include "core/Protocol.h"
#include "core/robots/RobotActions.h"

namespace rm {
namespace robot {

QVector<UserAction> sentryActions()
{
    using namespace RobotType;
    QVector<UserAction> a;

    a << makeAction("sentry.attack", "哨兵", "进攻", "SentryCtrlCommand",
                    [](int) { return proto::sentryCtrl(7); }, {Sentry}, Qt::Key_V);
    a << makeAction("sentry.defend", "哨兵", "防御", "SentryCtrlCommand",
                    [](int) { return proto::sentryCtrl(8); }, {Sentry}, Qt::Key_B);
    a << makeAction("sentry.move", "哨兵", "移动", "SentryCtrlCommand",
                    [](int) { return proto::sentryCtrl(9); }, {Sentry}, Qt::Key_N);
    a << makeAction("sentry.respawn", "哨兵", "确认复活", "SentryCtrlCommand",
                    [](int) { return proto::sentryCtrl(5); }, {Sentry});
    return a;
}

} // namespace robot
} // namespace rm
