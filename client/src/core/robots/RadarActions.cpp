// 雷达模块：当前没有上行触发功能（雷达只提供敌方位置/信息，下行消费）。
// 保留此文件作为占位，后续若增加雷达相关上行指令（如雷达自主决策）在此实现。
#include "core/robots/RobotActions.h"

namespace rm {
namespace robot {

QVector<UserAction> radarActions()
{
    return {};
}

} // namespace robot
} // namespace rm
