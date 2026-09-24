// 重装机器人特有功能：装配控制、部署模式。
#include "core/Protocol.h"
#include "core/robots/RobotActions.h"

namespace rm {
namespace robot {

QVector<UserAction> heavyActions()
{
    using namespace RobotType;
    QVector<UserAction> a;

    a << makeAction("heavy.assembly", "重装", "装配控制", "AssemblyCommand",
                    [](int p) { return proto::assemblyCommand(1, p); }, {Heavy}, Qt::Key_Z,
                    {QObject::tr("难度"), 1, 4, 1, 1}, "assembly");
    a << makeAction("heavy.deploy", "重装", "部署模式", "HeroDeployModeEventCommand",
                    [](int) {
                        robomaster::HeroDeployModeEventCommand m;
                        m.set_mode(1);
                        return proto::pack(m);
                    },
                    {Heavy}, Qt::Key_D, {}, "deploy");
    return a;
}

} // namespace robot
} // namespace rm
