#include "ui/Minimap.h"

#include <QMouseEvent>
#include <QPainter>
#include <QtMath>

namespace rm {
namespace {

// 场地逻辑尺寸（协议 RadarInfoToClient 单位 cm；RobotPathPlanInfo 起点/偏移单位 dm）。
// 场地 28m × 15m，雷达坐标 x∈[0,2800]cm、y∈[0,1500]cm。
constexpr float kMapWidth = 2800.0f;
constexpr float kMapHeight = 1500.0f;
// 小地图交互坐标（MapClickCmd）单位为米。
constexpr float kFieldWidthM = 28.0f;
constexpr float kFieldHeightM = 15.0f;

// map.png 内的有效地图区域
constexpr int kSrcX = 45;
constexpr int kSrcY = 31;
constexpr int kSrcW = 1181;
constexpr int kSrcH = 652;

const QColor kRed(0xE2, 0x3A, 0x3A);
const QColor kBlue(0x3A, 0x7B, 0xE2);
const QColor kUnknown(0xAA, 0xB2, 0xBE);
const QColor kPath(255, 153, 51);
const QColor kHighlight(0xFF, 0xD7, 0x00);
const QColor kMarkAttack(0xE2, 0x3A, 0x3A);
const QColor kMarkDefense(0x3A, 0xC0, 0x6A);
const QColor kMarkWarn(0xF2, 0xC0, 0x4D);
const QColor kMarkCustom(0x40, 0xC8, 0xC8);

// 场地固定点位（cm，红方在左 / 蓝方在右的俯视地图；己方按 m_allyIsRed 镜像）。
constexpr float kBaseX = 300.0f;
constexpr float kOutpostX = 720.0f;
constexpr float kRuneX = 1400.0f;
constexpr float kRuneY = 750.0f;

} // namespace

Minimap::Minimap(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(200, 120);
    m_map = QPixmap(QStringLiteral(":/rm/assets/map.png"));
}

void Minimap::setData(bool valid, bool allyIsRed, const QVector<HudRadarUnit>& units,
                      const QVector<QPointF>& path, int selfRobotId, int runeStatus, int runeArms,
                      float runeRings, const QVector<HudMapMarker>& markers)
{
    m_valid = valid;
    m_allyIsRed = allyIsRed;
    m_units = units;
    m_path = path;
    m_selfRobotId = selfRobotId;
    m_runeStatus = runeStatus;
    m_runeArms = runeArms;
    m_runeRings = runeRings;
    m_markers = markers;
    update();
}

void Minimap::setSelfPosition(bool valid, float x, float y, float yaw)
{
    m_selfPosValid = valid;
    m_selfPosX = x;
    m_selfPosY = y;
    m_selfYaw = yaw;
    update();
}

void Minimap::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRect r = rect();
    const QColor team = m_valid ? (m_allyIsRed ? kRed : kBlue) : kUnknown;
    const QColor enemyTeam = m_allyIsRed ? kBlue : kRed;
    // 放大成大地图时同步放大单位点/字号（以约 300px 宽的小地图为基准）。
    const qreal sc = qMax(qreal(1.0), r.width() / 300.0);

    // 背景 + 地图底图
    p.fillRect(r, QColor(24, 24, 28, 235));
    if (!m_map.isNull()) {
        const QRect src(kSrcX, kSrcY, kSrcW, kSrcH);
        p.drawPixmap(r, m_map, src);
    }
    p.fillRect(r, QColor(12, 22, 30, 72));

    // 内边框
    QColor inner = team;
    inner.setAlpha(52);
    p.setPen(QPen(inner, 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(r.adjusted(0, 0, -1, -1));

    // 单位点。协议：小地图左下角为原点，+X 向右、+Y 向上 ⇒ 屏幕 Y 需翻转；越界按边界显示。
    auto toScreen = [&](float x, float y) {
        const qreal nx = qBound(qreal(0.0), qreal(x) / kMapWidth, qreal(1.0));
        const qreal ny = qBound(qreal(0.0), qreal(y) / kMapHeight, qreal(1.0));
        return QPointF(r.left() + nx * r.width(), r.top() + (1.0 - ny) * r.height());
    };

    if (m_valid) {
        // 哨兵路径（橙线）
        if (m_path.size() >= 2) {
            QPolygonF poly;
            for (const QPointF& pt : m_path)
                poly << toScreen(float(pt.x()), float(pt.y()));
            p.setPen(QPen(kPath, 2 * sc));
            p.setBrush(Qt::NoBrush);
            p.drawPolyline(poly);
        }

        // 基地 / 前哨站图标（固定点位；己方在左、对方在右，按 allyIsRed 镜像）。
        const float allyBaseX = m_allyIsRed ? kBaseX : kMapWidth - kBaseX;
        const float enemyBaseX = m_allyIsRed ? kMapWidth - kBaseX : kBaseX;
        const float allyOutpostX = m_allyIsRed ? kOutpostX : kMapWidth - kOutpostX;
        const float enemyOutpostX = m_allyIsRed ? kMapWidth - kOutpostX : kOutpostX;
        auto drawSite = [&](float x, float y, const QColor& color, bool base) {
            const QPointF c = toScreen(x, y);
            const double sz = (base ? 7.0 : 5.0) * sc;
            p.setBrush(color);
            p.setPen(QPen(Qt::white, 1 * sc));
            p.drawRect(QRectF(c.x() - sz, c.y() - sz, sz * 2, sz * 2));
        };
        drawSite(allyBaseX, kRuneY, team, true);
        drawSite(enemyBaseX, kRuneY, enemyTeam, true);
        drawSite(allyOutpostX, kRuneY, team, false);
        drawSite(enemyOutpostX, kRuneY, enemyTeam, false);

        // 能量机关（大符）：场地中央，按状态着色并标注臂/环。
        {
            const QColor runeColor = m_runeStatus == 3 ? QColor(0x3A, 0xC0, 0x6A)
                                     : m_runeStatus == 2 ? kHighlight
                                                         : kUnknown;
            const QPointF c = toScreen(kRuneX, kRuneY);
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(runeColor, 2 * sc));
            p.drawEllipse(c, 6.0 * sc, 6.0 * sc);
            p.drawLine(QPointF(c.x(), c.y() - 9 * sc), QPointF(c.x(), c.y() + 9 * sc));
            p.drawLine(QPointF(c.x() - 9 * sc, c.y()), QPointF(c.x() + 9 * sc, c.y()));
            if (m_runeStatus > 0) {
                QFont f = p.font();
                f.setPixelSize(int(9 * sc));
                p.setFont(f);
                p.setPen(runeColor);
                p.drawText(QRectF(c.x() - 20 * sc, c.y() + 7 * sc, 40 * sc, 12 * sc), Qt::AlignCenter,
                           QStringLiteral("%1/%2").arg(m_runeArms).arg(double(m_runeRings), 0, 'f', 0));
            }
        }

        // 本车位置：官方 RobotPosition（实测位姿）优先，缺失时回退到雷达己方点。
        bool haveSelf = m_selfPosValid;
        QPointF selfPt;
        if (haveSelf) {
            selfPt = toScreen(m_selfPosX * 100.0f, m_selfPosY * 100.0f);
        } else {
            for (const HudRadarUnit& u : m_units) {
                if (!u.enemy && u.robotId == m_selfRobotId) {
                    haveSelf = true;
                    selfPt = toScreen(u.x, u.y);
                    break;
                }
            }
        }

        // 敌方高亮：从本车到每个高亮敌单位连线（锁定线）。
        if (haveSelf) {
            for (const HudRadarUnit& u : m_units) {
                if (!u.enemy || u.highlight <= 0 || (u.x == 0.0f && u.y == 0.0f))
                    continue;
                p.setPen(QPen(kHighlight, 1.5 * sc, Qt::DashLine));
                p.drawLine(selfPt, toScreen(u.x, u.y));
            }
        }

        // 指令标记（MapClickInfo）：mode 1 攻击 / 2 防御 / 3 警戒 / 4 自定义。
        for (const HudMapMarker& m : m_markers) {
            const QColor color = m.mode == 1 ? kMarkAttack
                                 : m.mode == 2 ? kMarkDefense
                                 : m.mode == 3 ? kMarkWarn
                                               : kMarkCustom;
            const QPointF c = toScreen(m.x * 100.0f, m.y * 100.0f);
            p.setBrush(color);
            p.setPen(QPen(Qt::white, 1 * sc));
            p.drawEllipse(c, 4.0 * sc, 4.0 * sc);
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(color, 2 * sc));
            p.drawEllipse(c, 7.0 * sc, 7.0 * sc);
        }

        // 雷达单位点（对方用对方色，己方用己方色）
        for (const HudRadarUnit& u : m_units) {
            if (u.x == 0.0f && u.y == 0.0f)
                continue;
            // RobotPosition 有效时本车点由其单独绘制（带朝向），跳过雷达己方本车点避免重复。
            if (m_selfPosValid && !u.enemy && u.robotId == m_selfRobotId)
                continue;
            const QPointF c = toScreen(u.x, u.y);
            const int num = u.robotId < 100 ? u.robotId : u.robotId - 100;
            const QColor color = u.enemy ? enemyTeam : team;

            QColor outer = color;
            outer.setAlpha(72);
            p.setPen(Qt::NoPen);
            p.setBrush(outer);
            p.drawEllipse(c, 10.0 * sc, 10.0 * sc);

            p.setBrush(QColor(26, 28, 34));
            p.drawEllipse(c, 8.0 * sc, 8.0 * sc);

            if (u.highlight > 0) {
                p.setBrush(Qt::NoBrush);
                p.setPen(QPen(kHighlight, 2 * sc));
                p.drawEllipse(c, 13.0 * sc, 13.0 * sc);
            }

            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(color, 2 * sc));
            p.drawEllipse(c, 10.0 * sc, 10.0 * sc);

            // 特殊兵种标记：无人机(6) 三角 / 哨兵(7) 菱形。
            if (num == 6 || num == 7) {
                QPolygonF glyph;
                if (num == 6) {
                    glyph << QPointF(c.x(), c.y() - 15 * sc) << QPointF(c.x() - 5 * sc, c.y() - 9 * sc)
                          << QPointF(c.x() + 5 * sc, c.y() - 9 * sc);
                } else {
                    glyph << QPointF(c.x(), c.y() - 15 * sc) << QPointF(c.x() + 5 * sc, c.y() - 10 * sc)
                          << QPointF(c.x(), c.y() - 5 * sc) << QPointF(c.x() - 5 * sc, c.y() - 10 * sc);
                }
                p.setBrush(color);
                p.setPen(Qt::NoPen);
                p.drawPolygon(glyph);
                p.setBrush(Qt::NoBrush);
            }

            // 本车：白色脉冲环强调（雷达不提供朝向，故不画指向箭头）。
            if (!u.enemy && u.robotId == m_selfRobotId) {
                QColor own = Qt::white;
                own.setAlpha(200);
                p.setPen(QPen(own, 2 * sc));
                p.drawEllipse(c, 13.0 * sc, 13.0 * sc);
            }

            QFont f = p.font();
            f.setPixelSize(int(12 * sc));
            p.setFont(f);
            p.setPen(Qt::white);
            p.drawText(QRectF(c.x() - 10 * sc, c.y() - 8 * sc, 20 * sc, 16 * sc), Qt::AlignCenter,
                       QString::number(num));

            // 敌方单位：坐标直接显示在点下方（米），不另设面板。
            if (u.enemy) {
                f.setPixelSize(int(10 * sc));
                p.setFont(f);
                p.setPen(enemyTeam.lighter(140));
                const QString pos = QStringLiteral("%1,%2")
                                        .arg(qRound(u.x / 100.0f))
                                        .arg(qRound(u.y / 100.0f));
                p.drawText(QRectF(c.x() - 18 * sc, c.y() + 11 * sc, 36 * sc, 12 * sc),
                           Qt::AlignCenter, pos);
            }
        }

        // 本车点与朝向箭头：来自 RobotPosition（yaw 正北 0°、顺时针为正）。
        // 约定场地 +Y 为屏幕上方（令 0° 朝上）：方向 = (sin yaw, -cos yaw)。
        if (m_selfPosValid) {
            p.setBrush(team);
            p.setPen(QPen(Qt::white, 1 * sc));
            p.drawEllipse(selfPt, 6.0 * sc, 6.0 * sc);

            const qreal rad = qDegreesToRadians(double(m_selfYaw));
            const QPointF tip(selfPt.x() + 18.0 * sc * qSin(rad),
                              selfPt.y() - 18.0 * sc * qCos(rad));
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(Qt::white, 2 * sc));
            p.drawLine(selfPt, tip);
            // 箭头两翼
            const QPointF wing1(tip.x() - 5 * sc * qSin(rad + 0.5),
                                tip.y() + 5 * sc * qCos(rad + 0.5));
            const QPointF wing2(tip.x() - 5 * sc * qSin(rad - 0.5),
                                tip.y() + 5 * sc * qCos(rad - 0.5));
            p.drawLine(tip, wing1);
            p.drawLine(tip, wing2);
        }
    } else {
        p.setPen(QColor(170, 178, 190, 160));
        QFont f = p.font();
        f.setPixelSize(13);
        p.setFont(f);
        p.drawText(r, Qt::AlignCenter, tr("等待雷达数据..."));
    }

    // 外边框（标记模式下用橙色提示）
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(m_markerMode ? kPath : team, 3));
    p.drawRect(QRectF(r).adjusted(1.5, 1.5, -1.5, -1.5));
}

void Minimap::mousePressEvent(QMouseEvent* event)
{
    if (!m_markerMode || event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    const QRectF r = rect();
    // 协议 MapClickCmd 的 map_x/map_y 单位为米，原点左下角（+Y 向上）。
    const float mapX = float((event->position().x() - r.left()) / r.width() * kFieldWidthM);
    const float mapY = float((1.0 - (event->position().y() - r.top()) / r.height()) * kFieldHeightM);
    emit mapClicked(mapX, mapY);
}

} // namespace rm
