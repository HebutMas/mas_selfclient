#pragma once

#include <QWidget>
#include <QByteArray>
#include <QImage>

#include "ui/OverlayHud.h"

namespace rm {

class ActionOverlay;
class StartPanelOverlay;
class Minimap;

class VideoView : public QWidget
{
    Q_OBJECT
public:
    explicit VideoView(QWidget* parent = nullptr);

    void showFrame(const QImage& frame);
    void setStatusText(const QString& text);

    void setHudVisible(bool visible);
    void setHudData(const HudData& data);
    void setActionData(const HudData& data);
    // 设置项：是否显示小地图。
    void setMinimapVisible(bool on);
    // 按住 M 时把小地图放大到屏幕中央，松手恢复右下角小地图。
    void setBigMapVisible(bool big);

    ActionOverlay* actionOverlay() const { return m_actions; }
    StartPanelOverlay* startPanel() const { return m_startPanel; }
    Minimap* minimap() const { return m_minimap; }

signals:
    // 指令界面确认后请求发送。
    void commandConfirmed(const QString& topic, const QByteArray& payload);
    // 小地图标记模式下的点击（地图坐标 cm）。
    void mapClicked(float mapX, float mapY);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void updateMinimapGeometry();

    QImage m_frame;
    QString m_status;
    OverlayHud* m_hud = nullptr;
    ActionOverlay* m_actions = nullptr;
    StartPanelOverlay* m_startPanel = nullptr;
    Minimap* m_minimap = nullptr;
    bool m_bigMap = false;
    bool m_showMinimap = true;
    bool m_hudVisible = false;
};

} // namespace rm
