#include "ui/VideoView.h"

#include <QPainter>
#include <QResizeEvent>

#include "ui/ActionOverlay.h"
#include "ui/Minimap.h"
#include "ui/StartPanelOverlay.h"

namespace rm {

VideoView::VideoView(QWidget* parent)
    : QWidget(parent)
    , m_status(tr("等待图传..."))
{
    setMinimumSize(640, 360);
    setAutoFillBackground(true);

    m_hud = new OverlayHud(this);
    m_hud->setGeometry(rect());
    m_hud->hide();

    m_actions = new ActionOverlay(this);
    m_actions->setGeometry(rect());
    connect(m_actions, &ActionOverlay::confirmed, this, &VideoView::commandConfirmed);

    m_startPanel = new StartPanelOverlay(this);
    m_startPanel->setGeometry(rect());

    // 小地图叠加层（HUD 可见时显示，位置与 HUD 的小地图区域一致）。
    m_minimap = new Minimap(this);
    connect(m_minimap, &Minimap::mapClicked, this, &VideoView::mapClicked);
    updateMinimapGeometry();
    m_minimap->hide();
}

void VideoView::showFrame(const QImage& frame)
{
    m_frame = frame;
    update();
}

void VideoView::setStatusText(const QString& text)
{
    m_status = text;
    update();
}

void VideoView::setHudVisible(bool visible)
{
    m_hudVisible = visible;
    m_hud->setVisible(visible);
    m_minimap->setVisible(visible && m_showMinimap);
    if (visible) {
        m_hud->raise();
        m_minimap->raise();
    }
}

void VideoView::setMinimapVisible(bool on)
{
    m_showMinimap = on;
    m_minimap->setVisible(m_hudVisible && on);
}

void VideoView::setHudData(const HudData& data)
{
    m_hud->setData(data);
    if (data.showMinimap != m_showMinimap)
        setMinimapVisible(data.showMinimap);
}

void VideoView::setActionData(const HudData& data)
{
    m_actions->setData(data);
    if (m_hud->isVisible())
        m_actions->raise();
}

void VideoView::setBigMapVisible(bool big)
{
    if (m_bigMap == big)
        return;
    m_bigMap = big;
    updateMinimapGeometry();
}

void VideoView::updateMinimapGeometry()
{
    if (!m_bigMap) {
        m_minimap->setGeometry(OverlayHud::minimapRect(size()));
        return;
    }
    // 大地图：居中，宽度 70%（保持 28:15），高度不超过屏幕 85%。
    int w = int(width() * 0.7);
    int h = w * 15 / 28;
    if (h > int(height() * 0.85)) {
        h = int(height() * 0.85);
        w = h * 28 / 15;
    }
    m_minimap->setGeometry((width() - w) / 2, (height() - h) / 2, w, h);
    m_minimap->raise();
}

void VideoView::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    m_hud->setGeometry(rect());
    m_actions->setGeometry(rect());
    m_startPanel->setGeometry(rect());
    updateMinimapGeometry();
}

void VideoView::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.fillRect(rect(), QColor(20, 20, 24));

    if (!m_frame.isNull()) {
        const QSize scaled = m_frame.size().scaled(size(), Qt::KeepAspectRatio);
        const QRect target(QPoint((width() - scaled.width()) / 2,
                                  (height() - scaled.height()) / 2),
                           scaled);
        painter.drawImage(target, m_frame);
        return;
    }

    painter.setPen(QColor(160, 160, 170));
    painter.drawText(rect(), Qt::AlignCenter, m_status);
}

} // namespace rm
