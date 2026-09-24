#pragma once

#include <QWidget>

#include "core/HudData.h"

namespace rm {

// 半透明叠加层，禁用鼠标事件，由 VideoView 在需要时显示。
// 数据模型见 core/HudData.h；本类只负责把 HudData 画到全屏图传之上。
class OverlayHud : public QWidget
{
    Q_OBJECT
public:
    explicit OverlayHud(QWidget* parent = nullptr);

    void setData(const HudData& data);

    // 小地图叠加区域（右下角，宽 0.2W，高 = 宽/28*15）；由 VideoView 用于摆放 Minimap。
    static QRect minimapRect(const QSize& size);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    // 顶部：基地/前哨站血条 + 居中阶段与倒计时，以及下方机器人小血条行
    void paintHeader(QPainter& p) const;
    // 左列上部：本车/副视频面板（含 UDP 反转状态）
    void paintVideoPanel(QPainter& p) const;
    // 左列下部：本车日志 + 系统日志（合并，按类型着色）
    void paintLogPanel(QPainter& p) const;
    // 左下：本车状态（血量/能量/经验/发弹量 + 指示灯 + 机载裁判系统）
    void paintSelfPanel(QPainter& p) const;
    // 右侧下部：增益
    void paintBuffPanel(QPainter& p) const;
    // 中心：准星 + 热量环
    void paintCrosshair(QPainter& p) const;
    // 底部居中：复活/装配/空中支援
    void paintBottomStatus(QPainter& p) const;
    // 顶部居中：战况横幅（Event 触发）
    void paintBanners(QPainter& p) const;
    // 居中：阶段弹窗（准备/自检/五秒倒计时/技术暂停/连接断开/违规）
    void paintPhasePopups(QPainter& p) const;
    // 居中：比赛结算面板（stage 5）
    void paintResult(QPainter& p) const;
    // 居中：本车受伤统计（按住 X）
    void paintDamagePanel(QPainter& p) const;
    // 居中：双方机器人数据表（按住 Tab）
    void paintStatsTable(QPainter& p) const;
    // 右下角：FPS（设置项）
    void paintFps(QPainter& p) const;

    HudData m_data;
};

} // namespace rm
