// 步兵特有功能：激活能量机关。
#include "core/Protocol.h"
#include "core/robots/RobotActions.h"

namespace rm {
namespace robot {

QVector<UserAction> infantryActions()
{
    using namespace RobotType;
    QVector<UserAction> a;

    a << makeAction("rune.activate", "能量机关", "激活能量机关", "RuneActivateCommand",
                    [](int) {
                        robomaster::RuneActivateCommand m;
                        m.set_activate(true);
                        return proto::pack(m);
                    },
                    {Infantry3, Infantry4, Infantry5}, Qt::Key_3);
    return a;
}

} // namespace robot
} // namespace rm
