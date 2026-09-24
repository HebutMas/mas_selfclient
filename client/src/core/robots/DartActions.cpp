// 飞镖特有功能：开闸、选择目标并发射。
#include "core/Protocol.h"
#include "core/robots/RobotActions.h"

namespace rm {
namespace robot {

QVector<UserAction> dartActions()
{
    using namespace RobotType;
    QVector<UserAction> a;

    a << makeAction("dart.open", "飞镖", "开闸", "DartCommand",
                    [](int) { return proto::dartCommand(0, true, false); }, {Dart}, Qt::Key_1, {},
                    "dart");
    a << makeAction("dart.launch", "飞镖", "发射", "DartCommand",
                    [](int p) { return proto::dartCommand(p, true, true); }, {Dart}, Qt::Key_2,
                    {QObject::tr("目标"), 1, 5, 1, 1,
                     {QObject::tr("1 前哨站"), QObject::tr("2 基地固定"), QObject::tr("3 基地随机固定"),
                      QObject::tr("4 基地随机移动"), QObject::tr("5 基地末端移动")}},
                    "dart");
    return a;
}

} // namespace robot
} // namespace rm
