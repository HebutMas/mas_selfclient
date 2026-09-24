// 空中机器人特有功能：地图标记、空中支援。
#include "core/Protocol.h"
#include "core/robots/RobotActions.h"

namespace rm {
namespace robot {

QVector<UserAction> aerialActions()
{
    using namespace RobotType;
    QVector<UserAction> a;

    a << makeAction("marker.toggle", "地图标记", "地图标记模式", "MapClickCmd",
                    [](int) { return QByteArray(); }, {Aerial}, Qt::Key_M, {}, "marker");
    a << makeAction("air.free", "空中支援", "免费支援", "AirSupportCommand",
                    [](int) { return proto::airSupport(1); }, {Aerial}, Qt::Key_4, {}, "air");
    a << makeAction("air.cancel", "空中支援", "取消支援", "AirSupportCommand",
                    [](int) { return proto::airSupport(0); }, {Aerial}, Qt::Key_5, {}, "air");
    return a;
}

} // namespace robot
} // namespace rm
