#pragma once

#include <QPointF>
#include <QPixmap>
#include <QVector>
#include <QWidget>

#include "core/HudData.h"

namespace rm {

// 小地图：ACE 布局右下角（视频叠加层）或右侧 Dock 均可承载。
// 绘制 map.png（UV 裁剪）+ 雷达单位点 + 哨兵路径；标记模式下左键点击发出地图坐标。
class Minimap : public QWidget
{
    Q_OBJECT
public:
    explicit Minimap(QWidget* parent = nullptr);

    void setData(bool valid, bool allyIsRed, const QVector<HudRadarUnit>& units,
                 const QVector<QPointF>& path, int selfRobotId = 0, int runeStatus = 0,
                 int runeArms = 0, float runeRings = 0.0f,
                 const QVector<HudMapMarker>& markers = {});
    // 本车实测位姿（RobotPosition；x/y 单位 m，yaw 度）。有效时优先于雷达本车点。
    void setSelfPosition(bool valid, float x, float y, float yaw);
    void setMarkerMode(bool on) { m_markerMode = on; }
    bool markerMode() const { return m_markerMode; }

signals:
    void mapClicked(float mapX, float mapY);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    bool m_valid = false;
    bool m_allyIsRed = true;
    bool m_markerMode = false;
    int m_selfRobotId = 0;
    bool m_selfPosValid = false;
    float m_selfPosX = 0.0f;  // m
    float m_selfPosY = 0.0f;  // m
    float m_selfYaw = 0.0f;   // 度，正北 0°、顺时针为正
    int m_runeStatus = 0;
    int m_runeArms = 0;
    float m_runeRings = 0.0f;
    QVector<HudRadarUnit> m_units;
    QVector<QPointF> m_path;
    QVector<HudMapMarker> m_markers;
    QPixmap m_map;
};

} // namespace rm
