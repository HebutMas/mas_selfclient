#include "ui/StartPanelOverlay.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>

#include "core/Protocol.h"
#include "core/UserActions.h"

namespace rm {
namespace {

using proto::airSupport;
using proto::assemblyCommand;
using proto::commonCommand;
using proto::dartCommand;
using proto::pack;

// 队内编号（红 1-9 / 蓝 101-109 -> 1-9）。
int relId(int id) { return id > 100 ? id - 100 : id; }

QLabel* makeSectionTitle(const QString& text, const QString& color, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setStyleSheet(QStringLiteral("color:%1;font-weight:bold;").arg(color));
    return label;
}

QPushButton* makeButton(const QString& text, const QString& bg, QWidget* parent)
{
    auto* b = new QPushButton(text, parent);
    b->setStyleSheet(QStringLiteral(
        "QPushButton{background:%1;color:white;border:none;border-radius:4px;padding:5px 8px;}"
        "QPushButton:disabled{background:#3a3f46;color:#888;}"
        "QPushButton:hover{background:%1;}")
                         .arg(bg));
    return b;
}

// 面板容器：标题 + 内容，带圆角背景。
QFrame* makePanel(QWidget* content, const QString& title, const QString& color, QWidget* parent)
{
    auto* frame = new QFrame(parent);
    // 深色背景上显式指定浅色文字，避免系统浅色主题下 QLabel/QRadioButton 黑字看不清。
    frame->setStyleSheet(QStringLiteral(
        "QFrame{background:rgba(16,20,26,190);border-radius:6px;}"
        "QLabel{color:#e6e6e6;}"
        "QRadioButton{color:#e6e6e6;}"
        "QCheckBox{color:#e6e6e6;}"
        "QSpinBox{color:#e6e6e6;background:rgba(10,14,20,200);"
        "border:1px solid #3a4556;border-radius:3px;padding:1px 3px;}"
        "QSpinBox QLineEdit{background:transparent;color:#e6e6e6;}"));
    auto* v = new QVBoxLayout(frame);
    v->setContentsMargins(10, 8, 10, 10);
    v->setSpacing(6);
    v->addWidget(makeSectionTitle(title, color, frame));
    v->addWidget(content);
    return frame;
}

} // namespace

StartPanelOverlay::StartPanelOverlay(QWidget* parent)
    : QWidget(parent)
{
    // 整体透明、不拦截鼠标的空白区域；内部面板可交互。
    auto* outer = new QVBoxLayout(this);
    outer->addStretch(1);
    auto* row = new QHBoxLayout;
    row->addStretch(1);

    auto* panel = new QFrame(this);
    m_panel = panel;
    panel->setFixedWidth(750);
    panel->setStyleSheet(QStringLiteral(
        "QFrame#StartPanel{background:rgba(26,32,41,235);border-radius:10px;}"));
    panel->setObjectName(QStringLiteral("StartPanel"));
    auto* panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(0, 0, 0, 0);

    // 内容放进滚动区，窗口较矮时不会溢出到其它 Dock。
    auto* scroll = new QScrollArea(panel);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet(QStringLiteral(
        "QScrollArea{background:transparent;}QScrollArea>QWidget>QWidget{background:transparent;}"));
    auto* content = new QWidget(scroll);
    m_content = content;
    content->setAutoFillBackground(false);
    scroll->viewport()->setAutoFillBackground(false);
    auto* pv = new QVBoxLayout(content);
    pv->setContentsMargins(16, 16, 16, 16);
    pv->setSpacing(10);

    // Action 视图下显示"返回"提示。
    m_viewHint = new QLabel(tr("按 Esc 返回机器人配置"), panel);
    m_viewHint->setAlignment(Qt::AlignCenter);
    m_viewHint->setStyleSheet(QStringLiteral("color:#ffcc4d;font-weight:bold;"));
    m_viewHint->hide();
    pv->addWidget(m_viewHint);

    m_loginBox = buildLoginSection();
    pv->addWidget(m_loginBox);
    pv->addWidget(buildControlSection());
    pv->addStretch(1);

    // 鼠标灵敏度
    m_sensWidget = new QWidget(panel);
    auto* sensRow = new QHBoxLayout(m_sensWidget);
    sensRow->setContentsMargins(0, 0, 0, 0);
    sensRow->addWidget(makeSectionTitle(tr("鼠标灵敏度"), QStringLiteral("#ffcc4d"), panel));
    m_sensitivity = new QSlider(Qt::Horizontal, panel);
    m_sensitivity->setRange(10, 500); // 0.10 - 5.00
    m_sensitivity->setValue(100);
    connect(m_sensitivity, &QSlider::valueChanged, this,
            [this](int v) { emit mouseSensitivityChanged(v / 100.0); });
    sensRow->addWidget(m_sensitivity, 1);
    pv->addWidget(m_sensWidget);

    // 显示设置（准星/小地图/FPS/显示模式/音量）
    m_displayWidget = buildDisplaySection();
    pv->addWidget(m_displayWidget);

    // 进入比赛 / 登录
    m_primaryWidget = new QWidget(panel);
    auto* primaryRow = new QHBoxLayout(m_primaryWidget);
    primaryRow->setContentsMargins(0, 0, 0, 0);
    primaryRow->addStretch(1);
    m_primary = new QPushButton(tr("登录"), panel);
    m_primary->setFixedSize(200, 34);
    m_primary->setStyleSheet(QStringLiteral(
        "QPushButton{background:#00cc66;color:white;border:2px solid #4dff99;border-radius:8px;font-weight:bold;}"
        "QPushButton:hover{background:#00e673;}"));
    connect(m_primary, &QPushButton::clicked, this, [this] {
        if (m_connected) {
            emit enterGameRequested();
        } else {
            const QString text = m_endpoint->text().trimmed();
            const int colon = text.lastIndexOf(QLatin1Char(':'));
            const QString host = colon >= 0 ? text.left(colon) : text;
            const int port = colon >= 0 ? text.mid(colon + 1).toInt() : 3333;
            if (host.isEmpty() || port <= 0 || port > 65535) {
                setEndpointError(tr("请输入 MQTT 服务端地址，格式 主机:端口"));
                return;
            }
            setEndpointError(QString());
            // 端点/视频参数持久化（原"设置"对话框并入此处）。
            QSettings s;
            s.setValue(QStringLiteral("endpoint/mqtt_host"), host);
            s.setValue(QStringLiteral("endpoint/mqtt_port"), port);
            s.setValue(QStringLiteral("endpoint/bind_ip"), m_bindIp->text().trimmed());
            s.setValue(QStringLiteral("endpoint/video_port"), m_videoPort->value());
            s.setValue(QStringLiteral("endpoint/client_id"),
                       QString::number(m_teamOffset + m_robotOffset));
            emit loginRequested(host, port, QString::number(m_teamOffset + m_robotOffset));
        }
    });
    primaryRow->addWidget(m_primary);
    m_exitButton = new QPushButton(tr("退出"), panel);
    m_exitButton->setFixedSize(90, 34);
    m_exitButton->setStyleSheet(QStringLiteral(
        "QPushButton{background:#5a5f66;color:#e6e6e6;border:2px solid #7a8088;border-radius:8px;}"
        "QPushButton:hover{background:#6b7178;}"));
    connect(m_exitButton, &QPushButton::clicked, this, &StartPanelOverlay::exitRequested);
    primaryRow->addWidget(m_exitButton);
    primaryRow->addStretch(1);
    pv->addWidget(m_primaryWidget);

    scroll->setWidget(content);
    panelLayout->addWidget(scroll);

    row->addWidget(panel);
    row->addStretch(1);
    outer->addLayout(row);
    outer->addStretch(1);

    applyRobotVisibility();
}

void StartPanelOverlay::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updatePanelHeight();
}

// 面板高度贴近内容，超出视野则封顶（内部滚动区处理溢出）。
void StartPanelOverlay::updatePanelHeight()
{
    if (!m_panel || !m_content)
        return;
    const int cap = qMax(200, int(height() * 0.94));
    const int hint = m_content->sizeHint().height() + 4;
    const int h = qBound(200, hint, cap);
    if (m_panel->minimumHeight() == h && m_panel->maximumHeight() == h)
        return;
    m_panel->setFixedHeight(h);
}

QWidget* StartPanelOverlay::buildLoginSection()
{
    auto* box = new QWidget(this);
    auto* boxV = new QVBoxLayout(box);
    boxV->setContentsMargins(0, 0, 0, 0);
    boxV->setSpacing(6);

    // 连接设置（连接状态/MQTT/视频接收参数）常显在配置面板，登录后仍可查看/修改。
    auto* connSettings = new QWidget(box);
    auto* cgrid = new QGridLayout(connSettings);
    cgrid->setContentsMargins(0, 0, 0, 0);
    cgrid->setSpacing(6);

    m_connStatus = new QLabel(tr("未登录"), box);
    cgrid->addWidget(makeSectionTitle(tr("连接状态"), QStringLiteral("#e6e6e6"), box), 0, 0);
    cgrid->addWidget(m_connStatus, 0, 1, 1, 3);

    cgrid->addWidget(makeSectionTitle(tr("MQTT 服务端"), QStringLiteral("#e6e6e6"), box), 1, 0);
    m_endpoint = new QLineEdit(QStringLiteral("127.0.0.1:3333"), box);
    cgrid->addWidget(m_endpoint, 1, 1, 1, 3);
    m_endpointError = new QLabel(box);
    m_endpointError->setStyleSheet(QStringLiteral("color:#ff6b6b;"));
    m_endpointError->setWordWrap(true);
    cgrid->addWidget(m_endpointError, 2, 1, 1, 3);

    // 视频接收参数（原"设置"对话框并入机器人配置面板）。
    cgrid->addWidget(makeSectionTitle(tr("视频绑定地址"), QStringLiteral("#e6e6e6"), box), 3, 0);
    m_bindIp = new QLineEdit(QStringLiteral("0.0.0.0"), box);
    cgrid->addWidget(m_bindIp, 3, 1, 1, 3);
    cgrid->addWidget(makeSectionTitle(tr("视频端口"), QStringLiteral("#e6e6e6"), box), 4, 0);
    m_videoPort = new QSpinBox(box);
    m_videoPort->setRange(1, 65535);
    m_videoPort->setValue(3334);
    cgrid->addWidget(m_videoPort, 4, 1, 1, 3);

    // 登录凭证（队伍/机器人/登录目标）与"退出登录"分开承载：连接后只隐藏前者、保留后者。
    auto* loginControls = new QWidget(box);
    auto* grid = new QGridLayout(loginControls);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(6);

    grid->addWidget(makeSectionTitle(tr("队伍"), QStringLiteral("#e6e6e6"), box), 0, 0);
    m_teamRed = makeButton(tr("红队"), QStringLiteral("#e04747"), box);
    m_teamBlue = makeButton(tr("蓝队"), QStringLiteral("#2e85fa"), box);
    connect(m_teamRed, &QPushButton::clicked, this, [this] { m_teamOffset = 0; updateLoginTargetText(); });
    connect(m_teamBlue, &QPushButton::clicked, this, [this] { m_teamOffset = 100; updateLoginTargetText(); });
    grid->addWidget(m_teamRed, 0, 1);
    grid->addWidget(m_teamBlue, 0, 2);

    grid->addWidget(makeSectionTitle(tr("机器人"), QStringLiteral("#e6e6e6"), box), 1, 0);
    const char* names[] = {"重装", "3号步兵", "4号步兵", "云台手"};
    const int ids[] = {1, 3, 4, 6};
    auto* robotRow = new QHBoxLayout;
    for (int i = 0; i < 4; ++i) {
        auto* b = makeButton(tr(names[i]), QStringLiteral("#3a6ea5"), box);
        const int id = ids[i];
        connect(b, &QPushButton::clicked, this, [this, id] { m_robotOffset = id; updateLoginTargetText(); });
        m_robotButtons.push_back(b);
        robotRow->addWidget(b);
    }
    grid->addLayout(robotRow, 1, 1, 1, 3);

    m_loginTarget = new QLabel(box);
    grid->addWidget(makeSectionTitle(tr("登录目标"), QStringLiteral("#e6e6e6"), box), 2, 0);
    grid->addWidget(m_loginTarget, 2, 1, 1, 3);

    m_logoutButton = makeButton(tr("退出登录"), QStringLiteral("#c0392b"), box);
    m_logoutButton->hide();
    connect(m_logoutButton, &QPushButton::clicked, this, [this] { emit logoutRequested(); });

    boxV->addWidget(connSettings);
    boxV->addWidget(loginControls);
    boxV->addWidget(m_logoutButton);

    // 载入上次保存的端点/视频参数（QSettings，与 autoConnectFromSettings 同键）。
    QSettings s;
    const QString host = s.value(QStringLiteral("endpoint/mqtt_host"), QStringLiteral("127.0.0.1")).toString();
    const int port = s.value(QStringLiteral("endpoint/mqtt_port"), 3333).toInt();
    m_endpoint->setText(QStringLiteral("%1:%2").arg(host).arg(port));
    m_bindIp->setText(s.value(QStringLiteral("endpoint/bind_ip"), QStringLiteral("0.0.0.0")).toString());
    m_videoPort->setValue(s.value(QStringLiteral("endpoint/video_port"), 3334).toInt());

    m_loginControls = loginControls;
    updateLoginTargetText();
    return box;
}

QWidget* StartPanelOverlay::buildDisplaySection()
{
    auto* w = new QWidget(this);
    // 深色背景上显式指定浅色文字，否则系统浅色主题下 QCheckBox 黑字看不清。
    w->setStyleSheet(QStringLiteral("QLabel{color:#e6e6e6;}QCheckBox{color:#e6e6e6;}"));
    auto* grid = new QGridLayout(w);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(6);

    grid->addWidget(makeSectionTitle(tr("显示设置"), QStringLiteral("#e6e6e6"), this), 0, 0);
    auto* optRow = new QHBoxLayout;
    m_crosshairBox = new QCheckBox(tr("显示准星"), w);
    m_minimapBox = new QCheckBox(tr("显示小地图"), w);
    m_fpsBox = new QCheckBox(tr("显示FPS"), w);
    optRow->addWidget(m_crosshairBox);
    optRow->addWidget(m_minimapBox);
    optRow->addWidget(m_fpsBox);
    optRow->addStretch(1);
    grid->addLayout(optRow, 0, 1, 1, 3);

    auto emitOpts = [this] {
        QSettings s;
        s.setValue(QStringLiteral("ui/crosshair"), m_crosshairBox->isChecked());
        s.setValue(QStringLiteral("ui/minimap"), m_minimapBox->isChecked());
        s.setValue(QStringLiteral("ui/fps"), m_fpsBox->isChecked());
        emit displayOptionsChanged(m_crosshairBox->isChecked(), m_minimapBox->isChecked(),
                                   m_fpsBox->isChecked());
    };
    connect(m_crosshairBox, &QCheckBox::toggled, this, [emitOpts](bool) { emitOpts(); });
    connect(m_minimapBox, &QCheckBox::toggled, this, [emitOpts](bool) { emitOpts(); });
    connect(m_fpsBox, &QCheckBox::toggled, this, [emitOpts](bool) { emitOpts(); });

    grid->addWidget(makeSectionTitle(tr("显示模式"), QStringLiteral("#e6e6e6"), this), 1, 0);
    m_fullscreenButton = makeButton(tr("全屏"), QStringLiteral("#3a6ea5"), w);
    m_fullscreenButton->setCheckable(true);
    m_fullscreenButton->setChecked(true);
    grid->addWidget(m_fullscreenButton, 1, 1, 1, 3);
    connect(m_fullscreenButton, &QPushButton::toggled, this, [this](bool on) {
        m_fullscreen = on;
        m_fullscreenButton->setText(on ? tr("全屏") : tr("窗口化"));
        QSettings s;
        s.setValue(QStringLiteral("ui/fullscreen"), on);
        emit fullscreenChanged(on);
    });

    grid->addWidget(makeSectionTitle(tr("音量"), QStringLiteral("#e6e6e6"), this), 2, 0);
    m_volume = new QSlider(Qt::Horizontal, w);
    m_volume->setRange(0, 100);
    grid->addWidget(m_volume, 2, 1, 1, 3);
    connect(m_volume, &QSlider::valueChanged, this, [this](int v) {
        QSettings s;
        s.setValue(QStringLiteral("ui/volume"), v);
        emit volumeChanged(v);
    });

    // 载入上次保存的显示设置（屏蔽信号，避免逐个 setChecked 时相互覆盖）。
    QSettings s;
    {
        const QSignalBlocker b1(m_crosshairBox);
        const QSignalBlocker b2(m_minimapBox);
        const QSignalBlocker b3(m_fpsBox);
        m_crosshairBox->setChecked(s.value(QStringLiteral("ui/crosshair"), true).toBool());
        m_minimapBox->setChecked(s.value(QStringLiteral("ui/minimap"), true).toBool());
        m_fpsBox->setChecked(s.value(QStringLiteral("ui/fps"), false).toBool());
    }
    m_fullscreen = s.value(QStringLiteral("ui/fullscreen"), true).toBool();
    m_fullscreenButton->setChecked(m_fullscreen);
    m_fullscreenButton->setText(m_fullscreen ? tr("全屏") : tr("窗口化"));
    m_volume->setValue(s.value(QStringLiteral("ui/volume"), 60).toInt());
    return w;
}

void StartPanelOverlay::setDisplayOptions(bool crosshair, bool minimap, bool fps)
{
    const QSignalBlocker b1(m_crosshairBox);
    const QSignalBlocker b2(m_minimapBox);
    const QSignalBlocker b3(m_fpsBox);
    if (m_crosshairBox)
        m_crosshairBox->setChecked(crosshair);
    if (m_minimapBox)
        m_minimapBox->setChecked(minimap);
    if (m_fpsBox)
        m_fpsBox->setChecked(fps);
}

QWidget* StartPanelOverlay::buildControlSection()
{
    auto* box = new QWidget(this);
    auto* grid = new QGridLayout(box);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(8);

    // 记住整块面板（标题+内容）的指针：applyRobotVisibility 按兵种隐藏整块。
    auto place = [&grid](QWidget* w, int r, int c) { grid->addWidget(w, r, c); };
    // 面板标题附带对应按键提示（按键已统一由 ~ 浮窗触发，此处仅作提示）。
    auto panelTitle = [](const char* panel, const QString& base) {
        QStringList keys;
        for (const UserAction& a : userActions()) {
            if (a.panel != QLatin1String(panel))
                continue;
            const QKeySequence k = actionKey(a.id);
            if (!k.isEmpty())
                keys << k.toString(QKeySequence::NativeText);
        }
        return keys.isEmpty() ? base : QStringLiteral("%1 [%2]").arg(base, keys.join(QLatin1Char('/')));
    };
    m_ammoPanel = makePanel(buildAmmoPanel(), panelTitle("ammo", tr("弹药兑换")), QStringLiteral("#4dff4d"), this);
    place(m_ammoPanel, 0, 0);
    m_deployPanel = makePanel(buildDeployPanel(), panelTitle("deploy", tr("部署模式")), QStringLiteral("#b34dff"), this);
    place(m_deployPanel, 0, 1);
    m_recoverPanel = makePanel(buildRecoverPanel(), panelTitle("recover", tr("远程回血")), QStringLiteral("#ff4d4d"), this);
    place(m_recoverPanel, 1, 0);
    m_revivePanel = makePanel(buildRevivePanel(), panelTitle("revive", tr("复活控制")), QStringLiteral("#ffff4d"), this);
    place(m_revivePanel, 1, 1);
    m_markerPanel = makePanel(buildMarkerPanel(), panelTitle("marker", tr("地图标记")), QStringLiteral("#ff9933"), this);
    place(m_markerPanel, 2, 0);
    m_perfPanel = makePanel(buildPerformancePanel(), panelTitle("perf", tr("性能选择")), QStringLiteral("#4dccff"), this);
    place(m_perfPanel, 2, 1);
    m_dartPanel = makePanel(buildDartPanel(), panelTitle("dart", tr("飞镖控制")), QStringLiteral("#ff66cc"), this);
    place(m_dartPanel, 3, 0);
    m_assemblyPanel = makePanel(buildAssemblyPanel(), panelTitle("assembly", tr("装配控制")), QStringLiteral("#4dccff"), this);
    place(m_assemblyPanel, 3, 1);
    m_airPanel = makePanel(buildAirSupportPanel(), panelTitle("air", tr("空中支援")), QStringLiteral("#33d9ff"), this);
    grid->addWidget(m_airPanel, 4, 0, 1, 2);
    return box;
}

QWidget* StartPanelOverlay::buildAmmoPanel()
{
    auto* w = new QWidget(this);
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(5);

    auto* row = new QHBoxLayout;
    m_ammoCaliber = new QLabel(tr("允许发弹量(17mm)："), w);
    row->addWidget(m_ammoCaliber);
    m_ammoSpin = new QSpinBox(w);
    m_ammoSpin->setRange(0, 3000);
    m_ammoSpin->setSingleStep(10);
    m_ammoSpin->setValue(10);
    row->addWidget(m_ammoSpin);
    row->addStretch(1);
    v->addLayout(row);

    m_ammoLocal = makeButton(tr("兑换允许发弹量"), QStringLiteral("#3399ff"), w);
    m_ammoRemote = makeButton(tr("远程兑换"), QStringLiteral("#3399ff"), w);
    v->addWidget(m_ammoLocal);
    v->addWidget(m_ammoRemote);
    m_ammoInfo = new QLabel(w);
    m_ammoInfo->setStyleSheet(QStringLiteral("color:#ffb0b0;"));
    m_ammoInfo->setWordWrap(true);
    v->addWidget(m_ammoInfo);

    connect(m_ammoSpin, &QSpinBox::valueChanged, this, [this] { refreshAmmoFeedback(); });
    connect(m_ammoLocal, &QPushButton::clicked, this, [this] {
        const int ammo = m_ammoSpin->value();
        // 重装兑换 42mm(cmd_type=2)，步兵/哨兵兑换 17mm(cmd_type=1)。
        emit commandRequested(QStringLiteral("CommonCommand"),
                              commonCommand(relId(m_data.selfRobotId) == 1 ? 2 : 1, ammo));
    });
    connect(m_ammoRemote, &QPushButton::clicked, this, [this] {
        const int ammo = m_ammoSpin->value();
        emit commandRequested(QStringLiteral("CommonCommand"), commonCommand(5, ammo));
    });
    return w;
}

QWidget* StartPanelOverlay::buildAssemblyPanel()
{
    auto* w = new QWidget(this);
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(5);

    auto* row = new QHBoxLayout;
    row->addWidget(new QLabel(tr("装配难度："), w));
    m_assemblySpin = new QSpinBox(w);
    m_assemblySpin->setRange(1, 4);
    m_assemblySpin->setValue(3);
    row->addWidget(m_assemblySpin);
    row->addStretch(1);
    v->addLayout(row);

    m_assemblyCancel = makeButton(tr("取消装配"), QStringLiteral("#4dccff"), w);
    m_assemblyConfirm = makeButton(tr("确认装配"), QStringLiteral("#4dccff"), w);
    v->addWidget(m_assemblyCancel);
    v->addWidget(m_assemblyConfirm);
    m_assemblyInfo = new QLabel(w);
    m_assemblyInfo->setStyleSheet(QStringLiteral("color:#ffb0b0;"));
    m_assemblyInfo->setWordWrap(true);
    v->addWidget(m_assemblyInfo);

    connect(m_assemblyCancel, &QPushButton::clicked, this, [this] {
        emit commandRequested(QStringLiteral("AssemblyCommand"), assemblyCommand(0, 0));
    });
    connect(m_assemblyConfirm, &QPushButton::clicked, this, [this] {
        emit commandRequested(QStringLiteral("AssemblyCommand"),
                              assemblyCommand(1, m_assemblySpin->value()));
    });
    return w;
}

QWidget* StartPanelOverlay::buildDeployPanel()
{
    auto* w = new QWidget(this);
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(5);
    m_deployButton = makeButton(tr("进入/退出部署模式"), QStringLiteral("#b34dff"), w);
    v->addWidget(m_deployButton);
    connect(m_deployButton, &QPushButton::clicked, this, [this] {
        robomaster::HeroDeployModeEventCommand m;
        m.set_mode(m_data.self.deployMode ? 0 : 1);
        emit commandRequested(QStringLiteral("HeroDeployModeEventCommand"), pack(m));
    });
    return w;
}

QWidget* StartPanelOverlay::buildRecoverPanel()
{
    auto* w = new QWidget(this);
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(5);
    m_recoverButton = makeButton(tr("远程回血"), QStringLiteral("#ff4d4d"), w);
    v->addWidget(m_recoverButton);
    connect(m_recoverButton, &QPushButton::clicked, this, [this] {
        emit commandRequested(QStringLiteral("CommonCommand"), commonCommand(6));
    });
    return w;
}

QWidget* StartPanelOverlay::buildRevivePanel()
{
    auto* w = new QWidget(this);
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(5);
    m_reviveFree = makeButton(tr("确认复活"), QStringLiteral("#ffff4d"), w);
    m_revivePay = makeButton(tr("兑换立即复活"), QStringLiteral("#ffff4d"), w);
    v->addWidget(m_reviveFree);
    v->addWidget(m_revivePay);
    m_reviveInfo = new QLabel(w);
    m_reviveInfo->setStyleSheet(QStringLiteral("color:#ffe08a;"));
    m_reviveInfo->setWordWrap(true);
    v->addWidget(m_reviveInfo);
    connect(m_reviveFree, &QPushButton::clicked, this, [this] {
        emit commandRequested(QStringLiteral("CommonCommand"), commonCommand(3));
    });
    connect(m_revivePay, &QPushButton::clicked, this, [this] {
        emit commandRequested(QStringLiteral("CommonCommand"), commonCommand(4));
    });
    return w;
}

QWidget* StartPanelOverlay::buildMarkerPanel()
{
    auto* w = new QWidget(this);
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(5);
    m_markerToggle = makeButton(tr("开始标记地图点位"), QStringLiteral("#ff9933"), w);
    m_markerSentry = new QCheckBox(tr("包含哨兵"), w);
    m_markerSentry->setChecked(true);
    v->addWidget(m_markerToggle);
    v->addWidget(m_markerSentry);
    connect(m_markerToggle, &QPushButton::clicked, this, [this] {
        const bool on = m_markerToggle->property("markerOn").toBool();
        m_markerToggle->setProperty("markerOn", !on);
        m_markerToggle->setText(!on ? tr("退出地图点位标记模式") : tr("开始标记地图点位"));
        emit markerModeChanged(!on);
    });
    return w;
}

QWidget* StartPanelOverlay::buildPerformancePanel()
{
    auto* w = new QWidget(this);
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(4);

    auto addRadioRow = [&](const QString& label, QButtonGroup*& group,
                           const QVector<QPair<QString, int>>& options) {
        auto* row = new QHBoxLayout;
        row->addWidget(new QLabel(label, w));
        group = new QButtonGroup(w);
        for (const auto& opt : options) {
            auto* r = new QRadioButton(opt.first, w);
            group->addButton(r, opt.second);
            row->addWidget(r);
        }
        row->addStretch(1);
        v->addLayout(row);
    };

    addRadioRow(tr("发射机构:"), m_shooterGroup, {{tr("近战优先"), 3}, {tr("远程优先"), 4}});
    addRadioRow(tr("底盘:"), m_chassisGroup, {{tr("近战优先"), 3}, {tr("远程优先"), 4}});

    auto emitPerf = [this] {
        robomaster::RobotPerformanceSelectionCommand m;
        if (auto* b = m_shooterGroup->checkedButton())
            m.set_shooter(static_cast<uint32_t>(m_shooterGroup->id(b)));
        if (auto* b = m_chassisGroup->checkedButton())
            m.set_chassis(static_cast<uint32_t>(m_chassisGroup->id(b)));
        emit commandRequested(QStringLiteral("RobotPerformanceSelectionCommand"), pack(m));
    };
    for (QButtonGroup* g : {m_shooterGroup, m_chassisGroup}) {
        connect(g, &QButtonGroup::idClicked, this, [emitPerf](int) { emitPerf(); });
    }
    return w;
}

QWidget* StartPanelOverlay::buildDartPanel()
{
    auto* w = new QWidget(this);
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(4);

    const char* targets[] = {"前哨站", "基地固定目标", "基地随机固定目标", "基地随机移动目标", "基地末端移动目标"};
    auto* row = new QHBoxLayout;
    row->addWidget(new QLabel(tr("目标:"), w));
    m_dartGroup = new QButtonGroup(w);
    auto* grid = new QGridLayout;
    for (int i = 0; i < 5; ++i) {
        auto* r = new QRadioButton(tr(targets[i]), w);
        m_dartGroup->addButton(r, i + 1);
        grid->addWidget(r, i / 3, i % 3);
    }
    v->addLayout(row);
    v->addLayout(grid);

    auto* btnRow = new QHBoxLayout;
    m_dartGate = makeButton(tr("开启闸门"), QStringLiteral("#ff66cc"), w);
    m_dartFire = makeButton(tr("确认发射"), QStringLiteral("#ff66cc"), w);
    btnRow->addWidget(m_dartGate);
    btnRow->addWidget(m_dartFire);
    v->addLayout(btnRow);

    auto currentTarget = [this] {
        return m_dartGroup->checkedButton() ? m_dartGroup->id(m_dartGroup->checkedButton()) : 0;
    };
    connect(m_dartGroup, &QButtonGroup::idClicked, this, [this, currentTarget](int id) {
        emit commandRequested(QStringLiteral("DartCommand"),
                              dartCommand(id, m_data.bottom.dartOpen >= 1, false));
    });
    connect(m_dartGate, &QPushButton::clicked, this, [this, currentTarget] {
        const bool open = m_data.bottom.dartOpen >= 1;
        emit commandRequested(QStringLiteral("DartCommand"), dartCommand(currentTarget(), !open, false));
    });
    connect(m_dartFire, &QPushButton::clicked, this, [this, currentTarget] {
        emit commandRequested(QStringLiteral("DartCommand"), dartCommand(currentTarget(), true, true));
        emit enterGameRequested();
    });
    return w;
}

QWidget* StartPanelOverlay::buildAirSupportPanel()
{
    auto* w = new QWidget(this);
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(5);

    m_airStatus = new QLabel(w);
    m_airStatus->setStyleSheet(QStringLiteral("color:#cfe6f5;"));
    m_airStatus->setWordWrap(true);
    v->addWidget(m_airStatus);

    auto* row = new QHBoxLayout;
    m_airFree = makeButton(tr("免费呼叫"), QStringLiteral("#33d9ff"), w);
    m_airPaid = makeButton(tr("付费呼叫"), QStringLiteral("#33d9ff"), w);
    m_airCancel = makeButton(tr("取消呼叫"), QStringLiteral("#33d9ff"), w);
    m_airRelease = makeButton(tr("解除反制"), QStringLiteral("#33d9ff"), w);
    row->addWidget(m_airFree);
    row->addWidget(m_airPaid);
    row->addWidget(m_airCancel);
    row->addWidget(m_airRelease);
    v->addLayout(row);

    auto send = [this](int id) {
        emit commandRequested(QStringLiteral("AirSupportCommand"), airSupport(id));
        emit enterGameRequested();
    };
    connect(m_airFree, &QPushButton::clicked, this, [send] { send(1); });
    connect(m_airPaid, &QPushButton::clicked, this, [send] { send(2); });
    connect(m_airCancel, &QPushButton::clicked, this, [send] { send(0); });
    connect(m_airRelease, &QPushButton::clicked, this, [send] { send(3); });
    return w;
}

void StartPanelOverlay::applyRobotVisibility()
{
    const int rel = relId(m_data.selfRobotId);
    const bool known = m_data.valid && m_data.selfRobotId != 0;
    const bool heavy = rel == 1;
    const bool config = (m_view == View::Config);
    const bool action = (m_view == View::Action);
    const bool all = action && m_allActions;

    // 配置视图：登录块 + 鼠标灵敏度 + 性能体系（其余专用面板仅按键触发时显示）。
    // 浮窗集合模式：只显示专用面板 + 鼠标灵敏度。
    m_loginBox->setVisible(config);
    m_sensWidget->setVisible(config || all);
    m_displayWidget->setVisible(config);
    m_primaryWidget->setVisible(config);

    auto allowed = [&](std::initializer_list<int> ids) {
        return known && std::find(ids.begin(), ids.end(), rel) != ids.end();
    };
    // 浮窗集合：通用面板全显，装配/空中支援仍限本兵种（与裁判手册一致）。
    auto panel = [&](QWidget* w, const char* name, std::initializer_list<int> ids) {
        bool show = false;
        if (all) {
            const bool robotSpecific = QLatin1String(name) == QLatin1String("assembly") ||
                                       QLatin1String(name) == QLatin1String("air");
            show = robotSpecific ? allowed(ids) : known;
        } else {
            show = action && m_actionPanel == QLatin1String(name) && allowed(ids);
        }
        w->setVisible(show);
    };
    panel(m_ammoPanel, "ammo", {1, 3, 4, 5, 7});
    panel(m_assemblyPanel, "assembly", {1});
    panel(m_deployPanel, "deploy", {1});
    panel(m_recoverPanel, "recover", {6, 7});
    panel(m_revivePanel, "revive", {1, 3, 4, 5, 7});
    panel(m_markerPanel, "marker", {6});
    panel(m_dartPanel, "dart", {8});
    panel(m_airPanel, "air", {6});

    m_perfPanel->setVisible(all ? known : (config && allowed({1, 3, 4, 5, 6})));
    if (!m_perfPanel->isHidden()) {
        // 重装用"近战/远程优先"，其余用"冷却/爆发优先"。
        for (auto* b : m_shooterGroup->buttons())
            b->setVisible((m_shooterGroup->id(b) >= 3) == heavy);
        for (auto* b : m_chassisGroup->buttons())
            b->setVisible((m_chassisGroup->id(b) >= 3) == heavy);
    }

    if (m_viewHint) {
        m_viewHint->setVisible(action);
        m_viewHint->setText(all ? tr("按 ~ 或 Esc 关闭") : tr("按 Esc 返回机器人配置"));
    }
    if (m_ammoCaliber)
        m_ammoCaliber->setText(heavy ? tr("允许发弹量(42mm)：") : tr("允许发弹量(17mm)："));
}

void StartPanelOverlay::showConfig()
{
    m_view = View::Config;
    m_actionPanel.clear();
    m_allActions = false;
    show();
    raise();
    applyRobotVisibility();
    updatePanelHeight();
}

void StartPanelOverlay::showActionPanel(const QString& panel)
{
    m_view = View::Action;
    m_actionPanel = panel;
    m_allActions = false;
    show();
    raise();
    applyRobotVisibility();
    updatePanelHeight();
}

void StartPanelOverlay::showAllActions()
{
    m_view = View::Action;
    m_actionPanel.clear();
    m_allActions = true;
    show();
    raise();
    applyRobotVisibility();
    updatePanelHeight();
}

bool StartPanelOverlay::allActionsVisible() const
{
    return m_view == View::Action && m_allActions && isVisible();
}

void StartPanelOverlay::hideOverlay()
{
    m_view = View::Hidden;
    m_allActions = false;
    hide();
}

bool StartPanelOverlay::overlayVisible() const
{
    return m_view != View::Hidden && isVisible();
}

void StartPanelOverlay::updateLoginTargetText()
{
    const char* names[] = {"重装", "3号步兵", "4号步兵", "云台手"};
    const int ids[] = {1, 3, 4, 6};
    QString name;
    for (int i = 0; i < 4; ++i) {
        if (ids[i] == m_robotOffset) {
            name = tr(names[i]);
            break;
        }
    }
    m_loginTarget->setText(tr("%1 / %2 (ID %3)")
                               .arg(m_teamOffset == 0 ? tr("红队") : tr("蓝队"))
                               .arg(name)
                               .arg(m_teamOffset + m_robotOffset));
    for (int i = 0; i < m_robotButtons.size(); ++i) {
        const bool sel = ids[i] == m_robotOffset;
        m_robotButtons[i]->setStyleSheet(
            sel ? QStringLiteral("QPushButton{background:#ffcc33;color:#1a1f28;border-radius:4px;padding:5px 8px;font-weight:bold;}")
                : QStringLiteral("QPushButton{background:#3a6ea5;color:white;border-radius:4px;padding:5px 8px;}"));
    }
    m_teamRed->setStyleSheet(m_teamOffset == 0
                                 ? QStringLiteral("QPushButton{background:#e04747;color:white;border-radius:4px;padding:5px 8px;font-weight:bold;}")
                                 : QStringLiteral("QPushButton{background:#5a2a2a;color:#ddd;border-radius:4px;padding:5px 8px;}"));
    m_teamBlue->setStyleSheet(m_teamOffset == 100
                                  ? QStringLiteral("QPushButton{background:#2e85fa;color:white;border-radius:4px;padding:5px 8px;font-weight:bold;}")
                                  : QStringLiteral("QPushButton{background:#24405f;color:#ddd;border-radius:4px;padding:5px 8px;}"));
}

void StartPanelOverlay::setConnectionState(int state, const QString& detail)
{
    m_connState = state;
    m_connected = state == 2;
    const QStringList texts = {tr("未登录"), tr("连接中"), tr("已登录"), tr("连接失败，重试中")};
    const QStringList colors = {"#b3b3bf", "#ffcc59", "#59ff8c", "#ff7373"};
    m_connStatus->setText(detail.isEmpty() ? texts.value(state, tr("未知状态")) : detail);
    m_connStatus->setStyleSheet(QStringLiteral("color:%1;font-weight:bold;")
                                    .arg(colors.value(state, QStringLiteral("#ffffff"))));

    m_loginControls->setVisible(!m_connected);
    m_logoutButton->setVisible(m_connected);
    updatePrimaryButton();
}

void StartPanelOverlay::setEndpointError(const QString& text)
{
    m_endpointError->setText(text);
}

void StartPanelOverlay::updatePrimaryButton()
{
    m_primary->setText(m_connected ? tr("进入比赛") : tr("登录"));
}

void StartPanelOverlay::setData(const HudData& data)
{
    m_data = data;
    // 兵种可见性依赖 (valid && robotId!=0)，valid 可能晚于 robotId 到齐，
    // 故每次刷新都重算（setVisible 幂等，代价可忽略）。
    applyRobotVisibility();
    updatePanelHeight();

    refreshAmmoFeedback();
    refreshAssemblyFeedback();
    refreshReviveFeedback();
    refreshAirSupportFeedback();

    // 部署模式按钮文案
    if (!m_deployButton->isHidden())
        m_deployButton->setText(m_data.self.deployMode ? tr("退出部署模式") : tr("进入部署模式"));

    // 飞镖闸门按钮
    if (!m_dartGate->isHidden())
        m_dartGate->setText(m_data.bottom.dartOpen >= 1 ? tr("关闭闸门") : tr("开启闸门"));
    m_dartFire->setEnabled(m_data.bottom.dartOpen == 2);
}

void StartPanelOverlay::refreshAmmoFeedback()
{
    if (m_ammoPanel->isHidden())
        return;
    const int rel = relId(m_data.selfRobotId);
    const bool hero = rel == 1;
    const int ammo = m_ammoSpin->value();
    m_ammoLocal->setEnabled(m_data.bottom.ammoBuffActive);
    m_ammoLocal->setToolTip(m_data.bottom.ammoBuffActive ? tr("在补给区内可兑换") : tr("不在补给区内"));
    m_ammoRemote->setEnabled(true);
    m_ammoInfo->setText(m_ammoLocal->isEnabled() ? QString() : tr("本地兑换需在补给区内（存在发弹量增益）"));
    Q_UNUSED(hero);
    Q_UNUSED(ammo);
}

void StartPanelOverlay::refreshAssemblyFeedback()
{
    if (m_assemblyPanel->isHidden())
        return;
    const int basic = m_data.bottom.techCoreBasicState;
    const bool hasSync = basic > 0;
    m_assemblyCancel->setEnabled(hasSync && basic != 1);
    m_assemblyConfirm->setEnabled(hasSync && basic <= 1);
    m_assemblyInfo->setText(m_assemblyCancel->isEnabled() || m_assemblyConfirm->isEnabled()
                                ? QString()
                                : tr("未进入装配流程"));
}

void StartPanelOverlay::refreshReviveFeedback()
{
    if (m_revivePanel->isHidden())
        return;
    const HudBottomStatus& b = m_data.bottom;
    const bool barDone = b.respawnTotal <= 0 || b.respawnProgress >= b.respawnTotal;
    m_reviveFree->setEnabled(b.respawnPending && b.canFreeRespawn && barDone);
    m_revivePay->setEnabled(b.canPayForRespawn);
    m_revivePay->setText(b.goldCostForRespawn > 0
                             ? tr("兑换立即复活（%1 金币）").arg(b.goldCostForRespawn)
                             : tr("兑换立即复活"));
    if (b.respawnPending && !barDone)
        m_reviveInfo->setText(tr("复活读条中 %1 / %2 …").arg(b.respawnProgress).arg(b.respawnTotal));
    else if (b.respawnPending)
        m_reviveInfo->setText(tr("复活读条已完成，可确认复活"));
    else
        m_reviveInfo->setText(b.canPayForRespawn ? tr("可花费金币立即复活") : QString());
}

void StartPanelOverlay::refreshAirSupportFeedback()
{
    if (m_airPanel->isHidden())
        return;
    const HudBottomStatus& b = m_data.bottom;
    static const char* statusNames[] = {"未进行空中支援", "正在空中支援", "未知空中支援状态"};
    static const char* shooterNames[] = {"已锁定(不可解除)", "正常", "已锁定(可解除)"};
    m_airStatus->setText(tr("当前状态: %1\n免费剩余: %2秒    已花费: %3\n照射状态: %4    发射机构: %5")
                             .arg(tr(statusNames[b.airSupportActive ? 1 : 0]))
                             .arg(b.airSupportLeftTime)
                             .arg(b.airSupportCostCoins)
                             .arg(b.airSupportBeingTargeted ? tr("被照射") : tr("未被照射"))
                             .arg(tr(shooterNames[qBound(1, b.airSupportShooterStatus, 2)])));
    m_airFree->setEnabled(!b.airSupportActive && b.airSupportLeftTime > 0);
    m_airPaid->setEnabled(!b.airSupportActive);
    m_airCancel->setEnabled(b.airSupportActive);
    m_airRelease->setEnabled(b.airSupportShooterStatus == 2);
}

double StartPanelOverlay::mouseSensitivity() const
{
    return m_sensitivity->value() / 100.0;
}

void StartPanelOverlay::setMouseSensitivity(double value)
{
    m_sensitivity->setValue(qBound(10, static_cast<int>(value * 100 + 0.5), 500));
}

bool StartPanelOverlay::markerMode() const
{
    return m_markerToggle->property("markerOn").toBool();
}

bool StartPanelOverlay::markerIncludeSentry() const
{
    return m_markerSentry->isChecked();
}

void StartPanelOverlay::setMarkerMode(bool on)
{
    if (m_markerToggle->property("markerOn").toBool() == on)
        return;
    m_markerToggle->setProperty("markerOn", on);
    m_markerToggle->setText(on ? tr("退出地图点位标记模式") : tr("开始标记地图点位"));
    emit markerModeChanged(on);
}

} // namespace rm
