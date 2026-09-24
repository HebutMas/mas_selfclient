#include "ui/OverlayHud.h"

#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QTextDocument>

namespace rm {
namespace {

constexpr int kBaseMaxHealth = 5000;
constexpr int kOutpostMaxHealth = 1500;

const QColor kRed(0xE2, 0x3A, 0x3A);
const QColor kBlue(0x3A, 0x7B, 0xE2);
const QColor kGold(0xF2, 0xC0, 0x4D);
const QColor kPanelBg(0, 0, 0, 130);
const QColor kPanelBorder(255, 255, 255, 40);
const QColor kTrackBg(255, 255, 255, 36);
const QColor kText(235, 235, 240);
const QColor kTextDim(165, 165, 175);

// HUD 字体统一使用像素尺寸：上层字体若以像素定义，pointSizeF() 会返回 -1。
int basePxFor(int h)
{
    return qBound(12, h / 64, 20);
}

// 左下本车状态面板的行高/条高/总高（paintSelfPanel 与左右列布局共用，保证不越界）。
int selfPanelLineH(int basePx)
{
    return qMax(12, basePx * 6 / 5) + 2;
}

int selfPanelBarH(int basePx)
{
    return qMax(13, basePx * 6 / 5);
}

int selfPanelHeightFor(int basePx)
{
    const int lineH = selfPanelLineH(basePx);
    const int barH = selfPanelBarH(basePx);
    return 6 + lineH            // 标题
           + 3 * (barH + 4)    // 血量 / 能量 / 经验
           + lineH             // 允许发弹量 · 热量
           + 2 * lineH         // 指示灯（固定两行）
           + lineH             // 机载裁判系统
           + lineH             // 大符 / RFID / 科技
           + 2 * lineH         // 模块在线网格（6 列 × 2 行）
           + 6;
}

QString stageText(int stage)
{
    static const char* kStages[] = {"未开始", "准备阶段", "裁判系统自检", "五秒倒计时",
                                    "比赛中", "比赛结算"};
    if (stage >= 0 && stage < 6)
        return QString::fromUtf8(kStages[stage]);
    return QStringLiteral("--");
}

QString gameResultText(int r)
{
    switch (r) {
    case 0: return QStringLiteral("平局");
    case 1: return QStringLiteral("红方胜利");
    case 2: return QStringLiteral("蓝方胜利");
    default: return QString();
    }
}

QString outpostStatusText(int s)
{
    static const char* kTexts[] = {"无敌", "旋转", "停转", "已毁", "可重建", "重建中"};
    return (s >= 0 && s < 6) ? QString::fromUtf8(kTexts[s]) : QString();
}

QString runeStatusText(int s)
{
    static const char* kTexts[] = {"--", "未激活", "激活中", "已激活"};
    return (s >= 0 && s < 4) ? QString::fromUtf8(kTexts[s]) : QStringLiteral("--");
}

QString rfidText(int r)
{
    static const char* kTexts[] = {"离线", "在线", "安装不规范"};
    return (r >= 0 && r < 3) ? QString::fromUtf8(kTexts[r]) : QStringLiteral("--");
}

// 模块状态配色：1 在线绿 / 0 离线红 / 2 安装不规范金
QColor moduleColor(int state)
{
    switch (state) {
    case 1: return QColor(0x3A, 0xC0, 0x6A);
    case 2: return kGold;
    default: return QColor(0xE2, 0x3A, 0x3A);
    }
}

// Buff 名称与配色（buff_type 1-7，见协议 2.2.14）
QString buffName(int type, int level)
{
    switch (type) {
    case 1: return QStringLiteral("攻击 +%1%").arg(level);
    case 2: return level >= 0 ? QStringLiteral("防御 +%1%").arg(level)
                              : QStringLiteral("易伤 %1%").arg(level);
    case 3: return QStringLiteral("热冷却 +%1/s").arg(level);
    case 4: return QStringLiteral("底盘功率 +%1%").arg(level);
    case 5: return QStringLiteral("回血 +%1%").arg(level);
    case 6: return QStringLiteral("兑换发弹量");
    case 7: return QStringLiteral("地形跨越");
    default: return QStringLiteral("Buff %1").arg(type);
    }
}

QColor buffColor(int type)
{
    switch (type) {
    case 1: return QColor(0xE2, 0x5C, 0x3A);
    case 2: return QColor(0x3A, 0xA0, 0xE2);
    case 3: return QColor(0xE2, 0xA0, 0x3A);
    case 4: return QColor(0x9A, 0x5C, 0xE2);
    case 5: return QColor(0x3A, 0xC0, 0x6A);
    case 6: return kGold;
    case 7: return QColor(0x40, 0xC8, 0xC8);
    default: return kTextDim;
    }
}

QString timeText(int stage, int countdownSec, int elapsedSec)
{
    if (stage != 2 && stage != 3 && stage != 4)
        return QStringLiteral("--:--");
    const int secs = countdownSec > 0 ? countdownSec : elapsedSec;
    return QStringLiteral("%1:%2")
        .arg(secs / 60, 2, 10, QLatin1Char('0'))
        .arg(secs % 60, 2, 10, QLatin1Char('0'));
}

void drawPanel(QPainter& p, const QRect& r, int radius = 6, int alpha = 130)
{
    QColor bg = kPanelBg;
    bg.setAlpha(alpha);
    p.setPen(kPanelBorder);
    p.setBrush(bg);
    p.drawRoundedRect(r, radius, radius);
}

void drawBar(QPainter& p, const QRect& r, double ratio, const QColor& color, int radius = 3)
{
    ratio = qBound(0.0, ratio, 1.0);
    p.setPen(Qt::NoPen);
    p.setBrush(kTrackBg);
    p.drawRoundedRect(r, radius, radius);
    if (ratio > 0.0) {
        QRect fill = r;
        fill.setWidth(int(r.width() * ratio));
        p.setBrush(color);
        p.drawRoundedRect(fill, radius, radius);
    }
    p.setPen(QColor(255, 255, 255, 70));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(r, radius, radius);
}

void drawBarLabel(QPainter& p, const QRect& r, const QString& text, const QColor& color)
{
    QFont f = p.font();
    f.setBold(true);
    f.setPixelSize(qMax(9, f.pixelSize() - 2));
    p.setFont(f);
    p.setPen(color);
    p.drawText(r, Qt::AlignCenter, text);
}


const int kSlotIds[6] = {1, 3, 4, 6, 7, 8};
const char* const kSlotNames[9] = {"重装", "", "步兵", "步兵", "步兵", "无人机",
                                   "哨兵", "飞镖", "雷达"};

const HudRobot* findRobot(const HudTeam& t, int id)
{
    for (const HudRobot& r : t.robots) {
        if (r.id == id)
            return &r;
    }
    return nullptr;
}

bool robotHasHealth(int id)
{
    return id == 1 || id == 2 || id == 3 || id == 4 || id == 5 || id == 7;
}

// 飞镖击打目标（协议 DartSelectTargetStatusSync.target_id，手册 2.2.32）。
QString dartTargetText(int targetId)
{
    static const char* kTargets[] = {"未选定", "前哨站", "基地固定", "基地随机固定",
                                     "基地随机移动", "基地末端移动"};
    if (targetId >= 0 && targetId < 6)
        return QString::fromUtf8(kTargets[targetId]);
    return QStringLiteral("--");
}

// 飞镖闸门状态（协议 DartSelectTargetStatusSync.open：0 关闭 / 1 开启中 / 2 已开启）。
QString dartGateText(int open)
{
    static const char* kGates[] = {"闸门:关", "闸门:开启中", "闸门:已开"};
    if (open >= 0 && open < 3)
        return QString::fromUtf8(kGates[open]);
    return QStringLiteral("--");
}

// 哨兵姿态（协议 SentryStatusSync.posture_id：1 进攻 / 2 防御 / 3 移动，手册 2.2.30）。
QString sentryPostureText(int posture)
{
    static const char* kPostures[] = {"未知", "进攻", "防御", "移动"};
    if (posture >= 0 && posture < 4)
        return QString::fromUtf8(kPostures[posture]);
    return QStringLiteral("--");
}

// 哨兵弱化/强化（协议 SentryStatusSync.is_weakened / is_powered，手册 2.2.30）。
QString sentryFlagText(bool weakened, bool powered)
{
    if (weakened && powered)
        return QString::fromUtf8("弱化·强化");
    if (weakened)
        return QString::fromUtf8("弱化");
    if (powered)
        return QString::fromUtf8("强化");
    return QString();
}

// 单个机器人小血条（ACE：上行 "id 名称"，条内 "当前/最大" 或 "-"，下行 "弹药:N"）。
// 飞镖显示击打目标与闸门状态（dartInfo=true 时）。
// 哨兵有血量/弹药，姿态与弱化/强化（sentryInfo=true 时）。
void drawRobotMini(QPainter& p, const QRect& r, int id, const HudRobot* rb, const QColor& color,
                   bool dartInfo = false, int dartTarget = 0, int dartOpen = 0,
                   bool sentryInfo = false, int posture = 0, bool weakened = false,
                   bool powered = false)
{
    const bool has = rb && rb->valid;
    const bool withHealth = robotHasHealth(id) && has && rb->maxHealth > 0;

    QFont f = p.font();
    f.setBold(true);
    p.setFont(f);
    p.setPen(has && !rb->alive ? kTextDim : kText);
    p.drawText(QRect(r.left(), r.top(), r.width(), 11), Qt::AlignHCenter | Qt::AlignVCenter,
               QStringLiteral("%1 %2").arg(id).arg(QString::fromUtf8(kSlotNames[id - 1])));

    const QRect bar(r.left(), r.top() + 12, r.width(), 7);
    if (withHealth)
        drawBar(p, bar, double(rb->health) / rb->maxHealth, color, 3);
    else
        drawBar(p, bar, 0.0, color, 3);

    f.setBold(false);
    p.setFont(f);

    const QRect line1(r.left(), r.top() + 20, r.width(), 11);
    const QRect line2(r.left(), r.top() + 31, r.width(), 11);

    if (id == 8) {
        p.setPen(dartInfo ? kText : kTextDim);
        p.drawText(line1, Qt::AlignHCenter | Qt::AlignVCenter,
                   dartInfo ? dartTargetText(dartTarget) : QStringLiteral("-"));
        p.setPen(QColor(200, 200, 200));
        p.drawText(line2, Qt::AlignHCenter | Qt::AlignVCenter,
                   dartInfo ? dartGateText(dartOpen) : QStringLiteral("-"));
        return;
    }

    p.setPen(kText);
    p.drawText(line1, Qt::AlignHCenter | Qt::AlignVCenter,
               withHealth ? QStringLiteral("%1/%2").arg(rb->health).arg(rb->maxHealth)
                          : QStringLiteral("-"));

    p.setPen(QColor(200, 200, 200));
    p.drawText(line2, Qt::AlignHCenter | Qt::AlignVCenter,
               has && robotHasHealth(id) ? QStringLiteral("弹药:%1").arg(rb->ammo)
                                         : QStringLiteral("弹药:-"));

    // 哨兵
    if (id == 7 && sentryInfo) {
        QString text = sentryPostureText(posture);
        const QString flag = sentryFlagText(weakened, powered);
        if (!flag.isEmpty())
            text += QLatin1Char(' ') + flag;
        p.setPen(powered ? kGold : (weakened ? QColor(0xE2, 0x6A, 0x6A) : kTextDim));
        p.drawText(QRect(r.left(), r.top() + 42, r.width(), 11), Qt::AlignHCenter | Qt::AlignVCenter,
                   text);
    }
}

} // namespace

QRect OverlayHud::minimapRect(const QSize& size)
{
    const int w = qMax(240, size.width() / 5);
    const int h = w * 15 / 28; // 地图 28:15
    return QRect(size.width() - w, size.height() - h, w, h);
}

OverlayHud::OverlayHud(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
}

void OverlayHud::setData(const HudData& data)
{
    m_data = data;
    update();
}

void OverlayHud::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    QFont f0 = p.font();
    f0.setPixelSize(basePxFor(height()));
    p.setFont(f0);

    // 所有 HUD 面板在比赛/操作两种模式都绘制；无数据时各面板显示占位。
    paintHeader(p);
    // 左列本车视频/日志：按住 Tab 才显示（默认隐藏，避免遮挡图传）。
    if (m_data.showVideoLog) {
        paintVideoPanel(p);
        paintLogPanel(p);
    }
    paintSelfPanel(p);
    paintBuffPanel(p);
    paintCrosshair(p);
    paintBottomStatus(p);
    paintFps(p);
    if (m_data.showDamagePanel)
        paintDamagePanel(p);
    if (m_data.showStatsTable)
        paintStatsTable(p);
    paintBanners(p);
    paintPhasePopups(p);
    paintResult(p);
}

void OverlayHud::paintHeader(QPainter& p) const
{
    const int margin = 14;
    const int top = 8;
    const int rowH = 28;
    const int centerW = 200;
    const int centerX = width() / 2 - centerW / 2;

    const QRect leftRegion(margin, top, centerX - 10 - margin, rowH);
    const QRect rightRegion(centerX + centerW + 10, top, width() - margin - (centerX + centerW + 10),
                            rowH);
    const QColor allyColor = m_data.allyIsRed ? kRed : kBlue;
    const QColor enemyColor = m_data.allyIsRed ? kBlue : kRed;

    auto paintSide = [&](const QRect& region, const HudTeam& team, const QColor& color, bool ally) {
        const int outpostW = int(region.width() * 0.20);
        const QRect outpost = ally ? QRect(region.left(), region.top(), outpostW, region.height())
                                   : QRect(region.right() - outpostW, region.top(), outpostW,
                                           region.height());
        const QRect base = ally ? QRect(outpost.right() + 6, region.top(),
                                        region.right() - outpost.right() - 6, region.height())
                                : QRect(region.left(), region.top(),
                                        outpost.left() - 6 - region.left(), region.height());
        const QColor baseColor = team.baseStatus == 0 ? color.darker(130) : color;
        drawBar(p, base, double(team.baseHealth + team.baseShield) / kBaseMaxHealth, baseColor, 12);
        drawBarLabel(p, base, tr("基地 %1/%2").arg(team.baseHealth + team.baseShield)
                                       .arg(kBaseMaxHealth), kText);
        drawBar(p, outpost, double(team.outpostHealth) / kOutpostMaxHealth, color, 12);
        QString outText = tr("前哨 %1").arg(team.outpostHealth);
        if (team.outpostStatus >= 3)
            outText = tr("前哨 %1").arg(outpostStatusText(team.outpostStatus));
        drawBarLabel(p, outpost, outText, team.outpostStatus >= 3 ? kTextDim : kText);
    };
    paintSide(leftRegion, m_data.ally, allyColor, true);
    paintSide(rightRegion, m_data.enemy, enemyColor, false);

    // 中央：阶段 / mm:ss / 比分 / 金币
    const int basePx = basePxFor(height());
    QFont f = p.font();
    f.setBold(false);
    f.setPixelSize(qMax(9, basePx - 3));
    p.setFont(f);
    p.setPen(m_data.isPaused ? kGold : kTextDim);
    QString stage = m_data.isPaused ? tr("技术暂停中") : stageText(m_data.stage);
    if (!m_data.isPaused && m_data.stage == 5) {
        const QString r = gameResultText(m_data.gameResult);
        if (!r.isEmpty())
            stage = tr("比赛结算: %1").arg(r);
    }
    p.drawText(QRect(centerX, top - 2, centerW, 14), Qt::AlignCenter, stage);

    f.setBold(true);
    f.setPixelSize(basePx + 14);
    p.setFont(f);
    p.setPen(kText);
    p.drawText(QRect(centerX, top + 8, centerW, 30), Qt::AlignCenter,
               timeText(m_data.stage, m_data.countdownSec, m_data.elapsedSec));

    f.setBold(false);
    f.setPixelSize(basePx - 2);
    p.setFont(f);
    p.setPen(kTextDim);
    p.drawText(QRect(centerX, top + 36, centerW, 13), Qt::AlignCenter,
               QStringLiteral("比分 %1 : %2").arg(m_data.redScore).arg(m_data.blueScore));

    f.setPixelSize(basePx - 1);
    p.setFont(f);
    p.setPen(kGold);
    p.drawText(QRect(centerX, top + 49, centerW, 14), Qt::AlignCenter,
               tr("金币 %1").arg(m_data.economy));

    // 堡垒占点计时（GlobalSpecialMechanism：己方/对方堡垒被占剩余秒数）。
    if (m_data.allyFortressTime >= 0 || m_data.enemyFortressTime >= 0) {
        QStringList occupied;
        if (m_data.allyFortressTime >= 0)
            occupied << tr("己方堡垒 %1s").arg(m_data.allyFortressTime);
        if (m_data.enemyFortressTime >= 0)
            occupied << tr("对方堡垒 %1s").arg(m_data.enemyFortressTime);
        f.setPixelSize(qMax(9, basePx - 4));
        p.setFont(f);
        p.setPen(kGold);
        p.drawText(QRect(centerX, top + 63, centerW, 13), Qt::AlignCenter, occupied.join("  "));
    }

    // 机器人小血条行：左 7 己方，右 7 敌方
    const int robotTop = top + rowH + 8;
    const int robotH = 44;
    auto paintRobots = [&](const QRect& region, const HudTeam& team, const QColor& color, bool ally) {
        const int n = 6;
        const int spacing = 6;
        const int cell = (region.width() - spacing * (n - 1)) / n;
        for (int i = 0; i < n; ++i) {
            const int x = region.left() + i * (cell + spacing);
            // 飞镖目标/闸门、哨兵姿态均仅有己方数据（本队消息）。
            drawRobotMini(p, QRect(x, robotTop, cell, robotH), kSlotIds[i],
                          findRobot(team, kSlotIds[i]), color, ally, m_data.bottom.dartTargetId,
                          m_data.bottom.dartOpen, ally, m_data.bottom.sentryPosture,
                          m_data.bottom.sentryWeakened, m_data.bottom.sentryPowered);
        }
    };
    paintRobots(leftRegion, m_data.ally, allyColor, true);
    paintRobots(rightRegion, m_data.enemy, enemyColor, false);
}

void OverlayHud::paintVideoPanel(QPainter& p) const
{
    const int leftW = qMax(240, width() / 5);
    const int top = height() * 15 / 100 + 8;
    const int selfH = selfPanelHeightFor(basePxFor(height()));
    const int colH = (height() - selfH - 16) - top;
    const QRect r(8, top, leftW - 16, (colH - 8) / 2);
    if (r.height() <= 0)
        return;
    drawPanel(p, r);

    const bool udpMain = m_data.videoSource == 0;
    const QFont origFont = p.font();
    QFont f = p.font();
    f.setBold(true);
    p.setFont(f);
    p.setPen(kText);
    p.drawText(QRect(r.left() + 8, r.top() + 4, r.width() - 16, 16), Qt::AlignLeft | Qt::AlignVCenter,
               udpMain ? tr("本车视频") : tr("UDP 视频流"));
    f.setBold(false);
    f.setPixelSize(qMax(9, f.pixelSize() - 3));
    p.setFont(f);
    p.setPen(kTextDim);
    p.drawText(QRect(r.left() + 8, r.top() + 4, r.width() - 16, 16), Qt::AlignRight | Qt::AlignVCenter,
               tr("[H 切换]"));

    const QRect btn(r.left() + 8, r.top() + 22, r.width() - 16, 18);
    p.setPen(Qt::NoPen);
    p.setBrush(m_data.udpInvert ? QColor(31, 115, 66) : QColor(41, 61, 79));
    p.drawRoundedRect(btn, 4, 4);
    p.setPen(kText);
    p.drawText(btn, Qt::AlignCenter, tr("UDP反转: %1 [I]").arg(m_data.udpInvert ? tr("开") : tr("关")));

    QString body;
    if (udpMain)
        body = m_data.customVideoReady ? tr("CustomByteBlock 视频流") : tr("CustomByteBlock 视频流不可用");
    else
        body = m_data.udpVideoReady ? tr("UDP 视频流") : tr("UDP 视频流当前不可用");
    if (!m_data.customVideoText.isEmpty())
        body = m_data.customVideoText;

    const QRect area(r.left() + 8, r.top() + 44, r.width() - 16, r.height() - 52);
    if (area.height() > 0) {
        p.setPen(QColor(60, 60, 70));
        p.setBrush(QColor(12, 12, 16, 120));
        p.drawRect(area);
        p.setPen(kTextDim);
        p.drawText(area, Qt::AlignCenter | Qt::TextWordWrap, body);
    }
    p.setFont(origFont);
}

void OverlayHud::paintLogPanel(QPainter& p) const
{
    const int leftW = qMax(240, width() / 5);
    const int top = height() * 15 / 100 + 8;
    const int selfH = selfPanelHeightFor(basePxFor(height()));
    const int colH = (height() - selfH - 16) - top;
    const int cvH = (colH - 8) / 2;
    const QRect r(8, top + cvH + 8, leftW - 16, colH - cvH - 8);
    if (r.height() <= 0)
        return;
    drawPanel(p, r);

    QFont f = p.font();
    f.setBold(true);
    p.setFont(f);
    p.setPen(kText);
    p.drawText(QRect(r.left() + 8, r.top() + 4, r.width() - 16, 16), Qt::AlignLeft | Qt::AlignVCenter,
               tr("本车日志 + 系统日志"));
    f.setBold(false);
    p.setFont(f);
    const int lineH = QFontMetrics(f).height() + 1;
    const int bodyTop = r.top() + 22;
    const int maxLines = (r.bottom() - 6 - bodyTop) / lineH;
    if (maxLines <= 0)
        return;

    if (m_data.logs.isEmpty()) {
        p.setPen(kTextDim);
        p.drawText(QRect(r.left() + 8, bodyTop, r.width() - 16, lineH),
                   Qt::AlignLeft | Qt::AlignVCenter, tr("暂无日志"));
        return;
    }

    // 自动滚到底：显示最新的 maxLines 条
    const int n = m_data.logs.size();
    const int start = qMax(0, n - maxLines);
    int y = bodyTop;
    for (int i = start; i < n; ++i) {
        const HudLogEntry& e = m_data.logs[i];
        QColor c(221, 221, 221);
        if (e.type == 1)
            c = QColor(230, 162, 60);
        else if (e.type == 2)
            c = QColor(245, 108, 108);
        p.setPen(c);
        const QString text = QStringLiteral("[%1][%2] %3").arg(e.time, e.source, e.text);
        p.drawText(QRect(r.left() + 8, y, r.width() - 16, lineH), Qt::AlignLeft | Qt::AlignVCenter,
                   QFontMetrics(f).elidedText(text, Qt::ElideRight, r.width() - 16));
        y += lineH;
    }
}

void OverlayHud::paintSelfPanel(QPainter& p) const
{
    const int basePx = basePxFor(height());
    const int lineH = selfPanelLineH(basePx);
    const int barH = selfPanelBarH(basePx);
    const int w = qMax(240, width() / 5) - 16;
    const int h = selfPanelHeightFor(basePx);
    const QRect r(8, height() - h - 8, w, h);
    drawPanel(p, r);

    const HudSelfStatus& s = m_data.self;
    const int left = 10;
    const int innerW = w - 2 * left;
    int y = r.top() + 6;

    QFont f = p.font();
    f.setPixelSize(basePx);
    f.setBold(true);
    p.setFont(f);
    p.setPen(s.valid ? kText : kTextDim);
    p.drawText(QRect(r.left() + left, y, innerW, lineH), Qt::AlignLeft | Qt::AlignVCenter,
               s.valid ? tr("机器人状态  %1 Lv%2").arg(s.name).arg(s.level) : tr("机器人状态"));
    f.setBold(false);
    p.setFont(f);
    y += lineH;

    // 血量 / 能量 / 经验 三条进度条（文字画在条内）
    const QRect hp(r.left() + left, y, innerW, barH);
    drawBar(p, hp, s.maxHealth > 0 ? double(s.health) / s.maxHealth : 0.0, QColor(0xE2, 0x3A, 0x3A), 3);
    drawBarLabel(p, hp, tr("血量 %1").arg(s.valid ? QStringLiteral("%1/%2").arg(s.health).arg(s.maxHealth) : QStringLiteral("-")), kText);
    y += barH + 4;

    const QRect en(r.left() + left, y, innerW, barH);
    drawBar(p, en, s.maxChassisEnergy > 0 ? double(s.chassisEnergy) / s.maxChassisEnergy : 0.0,
            QColor(0x3A, 0x7B, 0xE2), 3);
    drawBarLabel(p, en, tr("能量 %1").arg(s.valid ? QStringLiteral("%1/%2").arg(s.chassisEnergy).arg(s.maxChassisEnergy) : QStringLiteral("-")), kText);
    y += barH + 4;

    const QRect ex(r.left() + left, y, innerW, barH);
    drawBar(p, ex, s.experienceForUpgrade > 0 ? double(s.experience) / s.experienceForUpgrade : 0.0,
            QColor(0xE2, 0xC0, 0x4D), 3);
    drawBarLabel(p, ex, tr("经验 %1 (等级 %2)").arg(s.experience).arg(s.level), kText);
    y += barH + 4;

    p.setPen(QColor(0x7A, 0xC8, 0xE2));
    p.drawText(QRect(r.left() + left, y, innerW, lineH), Qt::AlignLeft | Qt::AlignVCenter,
               tr("允许发弹量 %1    热量 %2/%3")
                   .arg(s.remainingAmmo)
                   .arg(double(s.heat), 0, 'f', 0)
                   .arg(double(s.maxHeat), 0, 'f', 0));
    y += lineH;

    // 指示灯：固定两行（每行 3 项），避免单行超出面板宽度。自瞄/小陀螺暂无数据源。
    {
        QString names[6] = {tr("自瞄"), tr("小陀螺"), tr("部署模式"),
                            tr("脱战"), tr("远程补血"), tr("远程补弹")};
        if (s.outOfCombat && s.outOfCombatCountdown > 0)
            names[3] = tr("脱战 %1s").arg(s.outOfCombatCountdown);
        const bool on[6] = {false, false, s.deployMode,
                            s.outOfCombat, s.remoteHealAvailable, s.remoteAmmoAvailable};
        for (int row = 0; row < 2; ++row) {
            int x = r.left() + left;
            if (row == 0) {
                p.setPen(kTextDim);
                const int lw = p.fontMetrics().horizontalAdvance(tr("指示灯"));
                p.drawText(QRect(x, y, lw + 2, lineH), Qt::AlignLeft | Qt::AlignVCenter, tr("指示灯"));
                x += lw + 10;
            }
            for (int i = row * 3; i < row * 3 + 3; ++i) {
                p.setPen(on[i] ? QColor(0x3A, 0xC0, 0x6A) : QColor(0x80, 0x80, 0x80));
                const int tw = p.fontMetrics().horizontalAdvance(names[i]);
                p.drawText(QRect(x, y, tw + 2, lineH), Qt::AlignLeft | Qt::AlignVCenter, names[i]);
                x += tw + 16;
            }
            y += lineH;
        }
    }

    // 机载裁判系统状态（来自 RobotStaticStatus.connection_state / field_state）
    const QString conn = s.connectionState < 0 ? tr("--")
                         : (s.connectionState == 1 ? tr("已连接") : tr("未连接"));
    const QString field = s.fieldState < 0 ? tr("--")
                          : (s.fieldState == 0 ? tr("已上场") : tr("未上场"));
    p.setPen(kTextDim);
    p.drawText(QRect(r.left() + left, y, innerW, lineH), Qt::AlignLeft | Qt::AlignVCenter,
               tr("机载裁判系统 %1 · %2   黄牌 %3   红牌 %4")
                   .arg(conn, field)
                   .arg(s.yellowCards)
                   .arg(s.redCards));
    y += lineH;

    p.setPen(kTextDim);
    p.drawText(QRect(r.left() + left, y, innerW, lineH), Qt::AlignLeft | Qt::AlignVCenter,
               tr("大符 %1 臂%2 环%3   RFID %4   科技 %5")
                   .arg(runeStatusText(m_data.runeStatus)).arg(m_data.runeArms)
                   .arg(double(m_data.runeRings), 0, 'f', 1)
                   .arg(rfidText(m_data.rfid)).arg(m_data.techLevel));
    y += lineH;

    // 模块在线/离线网格（RobotModuleStatus，11 模块；绿在线 / 红离线 / 金安装不规范）。
    {
        const int cols = 6;
        const int cellW = qMax(1, innerW / cols);
        const int n = qMin(s.modules.size(), cols * 2);
        for (int i = 0; i < n; ++i) {
            const QRect cell(r.left() + left + (i % cols) * cellW, y + (i / cols) * lineH,
                             cellW, lineH);
            p.setPen(moduleColor(s.modules[i].state));
            p.drawText(cell, Qt::AlignLeft | Qt::AlignVCenter, s.modules[i].name);
        }
    }
}

void OverlayHud::paintBuffPanel(QPainter& p) const
{
    const auto& buffs = m_data.self.buffs;
    const int leftW = qMax(240, width() / 5);
    const int top = height() * 15 / 100 + 8;
    const int mmTop = minimapRect(size()).top();
    const int w = leftW - 16;
    const int rowH = 30;
    const int h = qMin(26 + buffs.size() * rowH + (buffs.isEmpty() ? 26 : 0), mmTop - top - 8);
    const QRect r(width() - leftW + 8, top, w, h);
    if (r.height() <= 0)
        return;
    drawPanel(p, r);

    QFont f = p.font();
    f.setBold(true);
    p.setFont(f);
    p.setPen(kText);
    p.drawText(QRect(r.left() + 10, r.top() + 4, w - 20, 16), Qt::AlignLeft | Qt::AlignVCenter,
               tr("Buff 状态"));
    f.setBold(false);
    f.setPixelSize(qMax(9, f.pixelSize() - 2));
    p.setFont(f);

    if (buffs.isEmpty()) {
        p.setPen(kTextDim);
        p.drawText(QRect(r.left() + 10, r.top() + 22, w - 20, 16), Qt::AlignLeft | Qt::AlignVCenter,
                   tr("无激活的 Buff"));
        return;
    }

    for (int i = 0; i < buffs.size(); ++i) {
        const HudBuff& b = buffs[i];
        const int y = r.top() + 24 + i * rowH;
        if (y + rowH > r.bottom())
            break;
        p.setPen(buffColor(b.type));
        p.drawText(QRect(r.left() + 10, y, w - 20, 14), Qt::AlignLeft | Qt::AlignVCenter,
                   buffName(b.type, b.level));
        drawBar(p, QRect(r.left() + 10, y + 15, w - 84, 7),
                b.maxTime > 0 ? double(b.leftTime) / b.maxTime : 0.0, buffColor(b.type), 3);
        p.setPen(kTextDim);
        p.drawText(QRect(r.right() - 72, y + 11, 62, 13), Qt::AlignRight | Qt::AlignVCenter,
                   tr("%1/%2s").arg(b.leftTime).arg(b.maxTime));
    }
}

void OverlayHud::paintCrosshair(QPainter& p) const
{
    if (!m_data.showCrosshair || m_data.stage != 4)
        return;
    const QPoint c = rect().center();
    const int size = 10;

    p.setPen(QPen(QColor(255, 255, 255, 200), 2));
    p.drawLine(c.x() - size, c.y(), c.x() + size, c.y());
    p.drawLine(c.x(), c.y() - size, c.x(), c.y() + size);

    // 告警文字（热量超限 / 速度锁定 / 超射速），叠在准星下方。
    const bool overheat = m_data.self.overheatLocked
                          || (m_data.self.maxHeat > 0.0f && m_data.self.heat >= m_data.self.maxHeat);
    QStringList warns;
    if (overheat)
        warns << tr("热量超限");
    if (m_data.self.speedLocked)
        warns << tr("速度锁定");
    if (m_data.self.fireRateLocked)
        warns << tr("超射速");
    {
        QFont wf = p.font();
        wf.setBold(true);
        p.setFont(wf);
        int wy = c.y() + size + 8;
        for (const QString& w : warns) {
            p.setPen(QColor(0xE2, 0x3A, 0x3A));
            p.drawText(QRect(c.x() - 120, wy, 240, 18), Qt::AlignCenter, w);
            wy += 18;
        }
    }

    // 热量环：current_heat / max_heat，绿 -> 黄 -> 红
    const float maxHeat = m_data.self.maxHeat;
    if (maxHeat <= 0.0f)
        return;
    const double ratio = qBound(0.0, double(m_data.self.heat) / maxHeat, 1.0);
    if (ratio <= 0.01)
        return;
    QColor c1(0x3A, 0xD0, 0x6A), c2(0xE2, 0xC0, 0x3A), c3(0xE2, 0x3A, 0x3A);
    QColor ring = ratio < 0.5 ? QColor(int(c1.red() + (c2.red() - c1.red()) * ratio * 2),
                                       int(c1.green() + (c2.green() - c1.green()) * ratio * 2),
                                       int(c1.blue() + (c2.blue() - c1.blue()) * ratio * 2))
                              : QColor(int(c2.red() + (c3.red() - c2.red()) * (ratio - 0.5) * 2),
                                       int(c2.green() + (c3.green() - c2.green()) * (ratio - 0.5) * 2),
                                       int(c2.blue() + (c3.blue() - c2.blue()) * (ratio - 0.5) * 2));
    const int radius = 40;
    const QRect ringRect(c.x() - radius, c.y() - radius, radius * 2, radius * 2);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(255, 255, 255, 40), 4));
    p.drawEllipse(ringRect);
    p.setPen(QPen(ring, 4));
    // 12 点方向起，顺时针；Qt 角度单位 1/16 度，正值为逆时针
    p.drawArc(ringRect, 90 * 16, int(-360 * 16 * ratio));
}

void OverlayHud::paintBottomStatus(QPainter& p) const
{
    const HudBottomStatus& b = m_data.bottom;
    QStringList parts;

    if (b.respawnPending)
        parts << (b.respawnTotal > 0
                      ? tr("复活进度: %1/%2").arg(b.respawnProgress).arg(b.respawnTotal)
                      : tr("待复活"));
    if (!b.assemblyText.isEmpty())
        parts << b.assemblyText;
    if (b.airSupportActive || b.airSupportLeftTime > 0)
        parts << tr("空中支援: %1 | 免费剩余 %2s")
                     .arg(b.airSupportActive ? tr("进行中") : tr("未进行"))
                     .arg(b.airSupportLeftTime);
    if (b.airSupportCounterCooldown >= 0)
        parts << tr("空中支援反制倒计时: %1s").arg(b.airSupportCounterCooldown);
    if (parts.isEmpty())
        return;

    const QString text = parts.join(QStringLiteral("    |    "));
    QFont f = p.font();
    f.setBold(true);
    p.setFont(f);
    QFontMetrics fm(f);
    const int w = fm.horizontalAdvance(text) + 40;
    const int h = 28;
    const QRect r((width() - w) / 2, height() - h - 10, w, h);
    drawPanel(p, r, 8, 150);
    p.setPen(kText);
    p.drawText(r, Qt::AlignCenter, text);
}

// 顶部居中战况横幅（Event 触发，约 4s 存活；红=对方 / 绿=己方 / 金=中性）。
void OverlayHud::paintBanners(QPainter& p) const
{
    if (m_data.banners.isEmpty())
        return;
    const int basePx = basePxFor(height());
    QFont f = p.font();
    f.setBold(true);
    f.setPixelSize(qMax(10, basePx));
    p.setFont(f);
    QFontMetrics fm(f);

    int y = 8 + 28 + 8 + 44 + 8;
    for (const HudBanner& b : m_data.banners) {
        const QColor color = b.type == 1 ? kRed : (b.type == 2 ? QColor(0x3A, 0xC0, 0x6A) : kGold);
        if (!b.rich.isEmpty()) {
            // 富文本横幅（击杀事件：双方按阵营色着色）。
            QTextDocument doc;
            doc.setDefaultFont(f);
            doc.setHtml(b.rich);
            const QSize sz = doc.size().toSize();
            const int w = sz.width() + 44;
            const int h = qMax(26, sz.height() + 6);
            const QRect r((width() - w) / 2, y, w, h);
            drawPanel(p, r, 6, 150);
            p.save();
            p.translate(r.left() + 22, r.top() + (h - sz.height()) / 2);
            doc.drawContents(&p);
            p.restore();
            y += h + 4;
            continue;
        }
        const int w = fm.horizontalAdvance(b.text) + 44;
        const QRect r((width() - w) / 2, y, w, 26);
        drawPanel(p, r, 6, 150);
        p.setPen(color);
        p.drawText(r, Qt::AlignCenter, b.text);
        y += 30;
    }
}

// 居中阶段弹窗：连接断开 / 准备 / 自检 / 五秒倒计时 / 技术暂停 / 违规锁定。
void OverlayHud::paintPhasePopups(QPainter& p) const
{
    struct Item
    {
        QString text;
        QColor color;
        bool big;
    };
    QVector<Item> items;
    if (m_data.connectionLost)
        items.append({tr("连接已断开"), kRed, true});
    if (m_data.isPaused)
        items.append({tr("技术暂停中"), kGold, true});
    if (m_data.stage == 1)
        items.append({tr("准备阶段"), kGold, true});
    else if (m_data.stage == 2)
        items.append({tr("裁判系统自检，请勿抢跑"), kGold, false});
    else if (m_data.stage == 3)
        items.append({tr("%1").arg(m_data.countdownSec), kGold, true});
    if (m_data.self.overheatLocked)
        items.append({tr("违规：超热量"), QColor(0xE2, 0x3A, 0x3A), false});
    if (m_data.self.speedLocked)
        items.append({tr("违规：超功率"), QColor(0xE2, 0x3A, 0x3A), false});
    if (m_data.self.fireRateLocked)
        items.append({tr("违规：超射速"), QColor(0xE2, 0x3A, 0x3A), false});
    if (items.isEmpty())
        return;

    const int bigPx = qBound(24, height() / 22, 56);
    int y = height() * 34 / 100;
    for (const Item& it : items) {
        QFont f = p.font();
        f.setBold(true);
        f.setPixelSize(it.big ? bigPx : qMax(14, bigPx / 2));
        p.setFont(f);
        QFontMetrics fm(f);
        const int w = fm.horizontalAdvance(it.text) + 56;
        const int h = it.big ? bigPx + 18 : bigPx / 2 + 16;
        const QRect r((width() - w) / 2, y, w, h);
        drawPanel(p, r, 8, 150);
        p.setPen(it.color);
        p.drawText(r, Qt::AlignCenter, it.text);
        y += h + 8;
    }
}

// 比赛结算面板（stage 5）：胜方 + 原因 + 比分 + 关键对比。
void OverlayHud::paintResult(QPainter& p) const
{
    if (m_data.stage != 5)
        return;
    const QString result = gameResultText(m_data.gameResult);
    if (result.isEmpty())
        return;

    const int w = qMin(560, width() * 2 / 3);
    const int basePx = basePxFor(height());
    const int lineH = qMax(20, basePx + 8);
    const int rows = 5;
    const int h = 60 + rows * lineH + 12;
    const QRect r((width() - w) / 2, (height() - h) / 2, w, h);
    drawPanel(p, r, 10, 190);

    QFont f = p.font();
    f.setBold(true);
    f.setPixelSize(qMax(22, basePx * 2));
    p.setFont(f);
    p.setPen(m_data.gameResult == 1 ? kRed : kBlue);
    p.drawText(QRect(r.left(), r.top() + 12, w, 36), Qt::AlignCenter, result);

    f.setBold(false);
    f.setPixelSize(qMax(11, basePx - 1));
    p.setFont(f);
    p.setPen(kTextDim);
    p.drawText(QRect(r.left(), r.top() + 48, w, 16), Qt::AlignCenter,
               tr("结算原因 %1").arg(m_data.endReason));

    const QVector<QPair<QString, QString>> rowsData = {
        {tr("比分"), QStringLiteral("%1 : %2").arg(m_data.redScore).arg(m_data.blueScore)},
        {tr("基地血量"),
         QStringLiteral("%1 / %2")
             .arg(m_data.ally.baseHealth + m_data.ally.baseShield)
             .arg(m_data.enemy.baseHealth + m_data.enemy.baseShield)},
        {tr("前哨站血量"),
         QStringLiteral("%1 / %2").arg(m_data.ally.outpostHealth).arg(m_data.enemy.outpostHealth)},
        {tr("总伤害"),
         QStringLiteral("%1 / %2").arg(m_data.ally.totalDamage).arg(m_data.enemy.totalDamage)},
        {tr("能量机关"), QStringLiteral("臂%1 环%2")
                             .arg(m_data.runeArms)
                             .arg(double(m_data.runeRings), 0, 'f', 1)},
    };
    int y = r.top() + 60 + 6;
    f.setPixelSize(qMax(11, basePx));
    p.setFont(f);
    for (const auto& row : rowsData) {
        p.setPen(kTextDim);
        p.drawText(QRect(r.left() + 20, y, w / 2 - 30, lineH), Qt::AlignLeft | Qt::AlignVCenter,
                   row.first);
        p.setPen(kText);
        p.drawText(QRect(r.left() + w / 2, y, w / 2 - 20, lineH), Qt::AlignRight | Qt::AlignVCenter,
                   row.second);
        y += lineH;
    }
}

// 本车受伤统计（RobotInjuryStat，按住 X）。
void OverlayHud::paintDamagePanel(QPainter& p) const
{
    const HudInjuryStat& s = m_data.injury;
    const int w = qMin(360, width() * 2 / 5);
    const int basePx = basePxFor(height());
    const int lineH = qMax(18, basePx + 6);
    const int rows = 8;
    const int h = 40 + rows * lineH + 10;
    const QRect r(width() - w - 16, minimapRect(size()).top() - h - 12, w, h);
    drawPanel(p, r, 8, 160);

    QFont f = p.font();
    f.setBold(true);
    f.setPixelSize(qMax(12, basePx));
    p.setFont(f);
    p.setPen(kText);
    p.drawText(QRect(r.left() + 12, r.top() + 6, w - 24, lineH), Qt::AlignLeft | Qt::AlignVCenter,
               tr("本车受伤统计"));

    if (!s.valid) {
        f.setBold(false);
        p.setFont(f);
        p.setPen(kTextDim);
        p.drawText(QRect(r.left() + 12, r.top() + 6 + lineH, w - 24, lineH),
                   Qt::AlignLeft | Qt::AlignVCenter, tr("暂无数据"));
        return;
    }

    const QVector<QPair<QString, int>> rowsData = {
        {tr("总伤害"), s.totalDamage},
        {tr("17mm"), s.smallProjectileDamage},
        {tr("42mm"), s.largeProjectileDamage},
        {tr("碰撞"), s.collisionDamage},
        {tr("飞镖溅射"), s.dartSplashDamage},
        {tr("模块离线"), s.moduleOfflineDamage},
        {tr("判罚"), s.penaltyDamage},
    };
    int y = r.top() + 6 + lineH;
    f.setBold(false);
    f.setPixelSize(qMax(11, basePx - 1));
    p.setFont(f);
    const int total = qMax(1, s.totalDamage);
    for (const auto& row : rowsData) {
        p.setPen(kTextDim);
        p.drawText(QRect(r.left() + 12, y, w - 120, lineH), Qt::AlignLeft | Qt::AlignVCenter,
                   row.first);
        p.setPen(kText);
        p.drawText(QRect(r.left() + w - 110, y, 98, lineH), Qt::AlignRight | Qt::AlignVCenter,
                   tr("%1  (%2%)").arg(row.second).arg(row.second * 100 / total));
        y += lineH;
    }
    p.setPen(kTextDim);
    p.drawText(QRect(r.left() + 12, y, w - 24, lineH), Qt::AlignLeft | Qt::AlignVCenter,
               tr("击杀者 ID %1").arg(s.killerId));
}

// 双方机器人数据表（按住 Tab）：HP/等级/弹药 + 队伍合计。
void OverlayHud::paintStatsTable(QPainter& p) const
{
    const int w = qMin(640, width() * 3 / 5);
    const int basePx = basePxFor(height());
    const int lineH = qMax(18, basePx + 6);
    const int h = 56 + (6 + 2) * lineH + 10;
    const QRect r((width() - w) / 2, (height() - h) / 2, w, h);
    drawPanel(p, r, 8, 180);

    QFont f = p.font();
    f.setBold(true);
    f.setPixelSize(qMax(12, basePx));
    p.setFont(f);
    p.setPen(kText);
    p.drawText(QRect(r.left(), r.top() + 6, w, lineH), Qt::AlignCenter, tr("机器人数据"));

    const int leftX = r.left() + 12;
    const int colW = (w - 24) / 2;
    f.setPixelSize(qMax(11, basePx - 1));
    p.setFont(f);
    p.setPen(m_data.allyIsRed ? kRed : kBlue);
    p.drawText(QRect(leftX, r.top() + 6 + lineH, colW, lineH), Qt::AlignLeft | Qt::AlignVCenter,
               tr("己方"));
    p.setPen(m_data.allyIsRed ? kBlue : kRed);
    p.drawText(QRect(leftX + colW, r.top() + 6 + lineH, colW, lineH),
               Qt::AlignLeft | Qt::AlignVCenter, tr("对方"));

    int y = r.top() + 6 + 2 * lineH;
    f.setBold(false);
    p.setFont(f);
    const int ammoW = 48;
    auto drawRow = [&](const QRect& rr, const HudRobot* rb, int fallbackMax) {
        if (!rb) {
            p.setPen(kTextDim);
            p.drawText(rr, Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("-"));
            return;
        }
        const int nameW = rr.width() * 2 / 5;
        p.setPen(kText);
        p.drawText(QRect(rr.left(), rr.top(), nameW, rr.height()), Qt::AlignLeft | Qt::AlignVCenter,
                   QStringLiteral("%1 %2 Lv%3")
                       .arg(rb->id)
                       .arg(rb->name.isEmpty() ? QStringLiteral("-") : rb->name)
                       .arg(rb->level));
        const int mx = rb->maxHealth > 0 ? rb->maxHealth : fallbackMax;
        const QRect barR(rr.left() + nameW, rr.top() + rr.height() / 4,
                         rr.width() - nameW - ammoW, rr.height() / 2);
        if (barR.width() > 8) {
            const double ratio = mx > 0 ? qBound(0.0, double(rb->health) / mx, 1.0) : 0.0;
            const QColor c = ratio > 0.6   ? QColor(0x3A, 0xC0, 0x6A)
                             : ratio > 0.3 ? kGold
                                           : QColor(0xE2, 0x3A, 0x3A);
            drawBar(p, barR, ratio, c, 3);
            if (barR.width() >= 54) {
                QFont sf = f;
                sf.setPixelSize(qMax(9, basePx - 3));
                p.setFont(sf);
                p.setPen(kText);
                p.drawText(barR, Qt::AlignCenter, QStringLiteral("%1/%2").arg(rb->health).arg(mx));
                p.setFont(f);
            }
        }
        p.setPen(kTextDim);
        p.drawText(QRect(rr.right() - ammoW, rr.top(), ammoW, rr.height()),
                   Qt::AlignRight | Qt::AlignVCenter, tr("弹%1").arg(rb->ammo));
    };
    for (int i = 0; i < 6; ++i) {
        const HudRobot* a = i < m_data.ally.robots.size() ? &m_data.ally.robots[i] : nullptr;
        const HudRobot* e = i < m_data.enemy.robots.size() ? &m_data.enemy.robots[i] : nullptr;
        const int allyMax = a ? a->maxHealth : 0;
        drawRow(QRect(leftX, y, colW - 8, lineH), a, allyMax);
        drawRow(QRect(leftX + colW, y, colW - 8, lineH), e, allyMax);
        y += lineH;
    }
    int allyHp = 0, enemyHp = 0;
    for (const HudRobot& rb : m_data.ally.robots)
        allyHp += rb.health;
    for (const HudRobot& rb : m_data.enemy.robots)
        enemyHp += rb.health;
    p.setPen(kTextDim);
    p.drawText(QRect(leftX, y, colW - 8, lineH), Qt::AlignLeft | Qt::AlignVCenter,
               tr("总血量 %1  总伤害 %2").arg(allyHp).arg(m_data.ally.totalDamage));
    p.drawText(QRect(leftX + colW, y, colW - 8, lineH), Qt::AlignLeft | Qt::AlignVCenter,
               tr("总血量 %1  总伤害 %2").arg(enemyHp).arg(m_data.enemy.totalDamage));
}

// 右下角 FPS（设置项）。
void OverlayHud::paintFps(QPainter& p) const
{
    if (!m_data.showFps)
        return;
    QFont f = p.font();
    f.setBold(true);
    f.setPixelSize(qMax(11, basePxFor(height()) - 2));
    p.setFont(f);
    p.setPen(m_data.fps >= 50 ? QColor(0x3A, 0xC0, 0x6A) : kGold);
    p.drawText(QRect(width() - 120, 88, 108, 18), Qt::AlignRight | Qt::AlignVCenter,
               tr("FPS %1").arg(m_data.fps));
}

} // namespace rm
