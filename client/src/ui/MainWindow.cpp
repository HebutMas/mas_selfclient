#include "ui/MainWindow.h"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QCursor>
#include <QDialog>
#include <QImage>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QSettings>
#include <QShortcut>
#include <QShowEvent>
#include <QThread>
#include <QTime>
#include <QTimer>
#include <QTransform>
#include <QWheelEvent>

#include "MqttClient.h"
#include "net/GameData.h"
#include "net/VideoReceiver.h"
#include "ui/ActionOverlay.h"
#include "ui/Minimap.h"
#include "ui/StartPanelOverlay.h"
#include "core/Protocol.h"
#include "core/UserActions.h"
#include "ui/VideoView.h"

#include <string>

namespace rm {
namespace {

// 文本输入控件获得焦点时，不拦截 Tab / M（否则无法输入地址、切换焦点）。
bool textInputHasFocus()
{
    QWidget* w = QApplication::focusWidget();
    if (!w)
        return false;
    return qobject_cast<QLineEdit*>(w) || qobject_cast<QAbstractSpinBox*>(w);
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("RoboMaster 自定义客户端"));
    resize(1600, 900);

    // 应用级事件过滤器：Tab / M 需要“按下显示、松手关闭”，且 Tab 会被焦点切换吃掉、
    // M 可能与地图标记快捷键冲突，故在事件到达焦点控件前统一处理。
    qApp->installEventFilter(this);

    createCentral();

    m_mqtt = new MqttClient(this);
    m_game = new GameData(this);
    connect(m_mqtt, &MqttClient::connected, this, &MainWindow::onMqttConnected);
    connect(m_mqtt, &MqttClient::disconnected, this, &MainWindow::onMqttDisconnected);
    connect(m_mqtt, &MqttClient::errorOccurred, this, &MainWindow::onMqttError);
    connect(m_mqtt, &MqttClient::messageReceived, this, &MainWindow::onMqttMessage);
    connect(m_game, &GameData::logMessage, this,
            [this](const QString& text) { appendLog(text, 1); });
    // 指令界面确认后真正发送。
    connect(m_video, &VideoView::commandConfirmed, this, &MainWindow::executeCommand);

    // 启动/控制面板。
    StartPanelOverlay* sp = m_video->startPanel();
    connect(sp, &StartPanelOverlay::loginRequested, this,
            [this](const QString& host, int port, const QString& clientId) {
                m_clientId = clientId;
                m_teamIsRed = clientId.toInt() < 100;
                m_robotType = clientId.toInt() % 100;
                m_game->setAllyIsRed(m_teamIsRed);
                m_game->setSelfRobotId(m_clientId.toUInt());
                connectMqtt(host, port, clientId);
            });
    connect(sp, &StartPanelOverlay::logoutRequested, this, [this] {
        m_mqtt->disconnectFromBroker();
        m_video->startPanel()->setConnectionState(0);
    });
    connect(sp, &StartPanelOverlay::enterGameRequested, this, [this] {
        if (!m_mqtt->isConnected()) {
            appendLog(tr("未连接服务器，无法进入比赛"));
            return;
        }
        appendLog(tr("进入比赛：%1 clientID=%2")
                               .arg(m_teamIsRed ? tr("红方") : tr("蓝方"))
                               .arg(m_clientId));
        m_gameMode = true;
        m_video->startPanel()->hideOverlay();
        refreshHud();
    });
    connect(sp, &StartPanelOverlay::commandRequested, this, &MainWindow::executeCommand);
    connect(sp, &StartPanelOverlay::exitRequested, this, &MainWindow::close);
    connect(sp, &StartPanelOverlay::displayOptionsChanged, this,
            [this](bool crosshair, bool minimap, bool fps) {
                m_showCrosshair = crosshair;
                m_showMinimap = minimap;
                m_showFps = fps;
                refreshHud();
            });
    connect(sp, &StartPanelOverlay::fullscreenChanged, this, &MainWindow::setFullscreen);
    connect(sp, &StartPanelOverlay::volumeChanged, this, [](int) {
        // 音量仅持久化；音频管线尚未接入。
    });

    // 载入持久化的显示设置到运行状态。
    {
        QSettings settings;
        m_showCrosshair = settings.value(QStringLiteral("ui/crosshair"), true).toBool();
        m_showMinimap = settings.value(QStringLiteral("ui/minimap"), true).toBool();
        m_showFps = settings.value(QStringLiteral("ui/fps"), false).toBool();
        sp->setDisplayOptions(m_showCrosshair, m_showMinimap, m_showFps);
    }

    // 官方 UDP 图传：独立线程接收 3334 端口并用 FFmpeg 解码，帧回到 UI 线程显示。
    m_videoReceiver = new VideoReceiver;
    m_videoThread = new QThread(this);
    m_videoReceiver->moveToThread(m_videoThread);
    connect(m_videoThread, &QThread::finished, m_videoReceiver, &QObject::deleteLater);
    connect(m_videoReceiver, &VideoReceiver::frameReady, this, &MainWindow::onVideoFrame);
    connect(m_videoReceiver, &VideoReceiver::signalActive, this, &MainWindow::onVideoSignal);
    connect(m_videoReceiver, &VideoReceiver::errorOccurred, this,
            [this](const QString& message) { appendLog(message, 2); });
    m_videoThread->start();
    {
        QSettings s;
        const QString bindIp = s.value(QStringLiteral("endpoint/bind_ip"), QStringLiteral("0.0.0.0")).toString();
        const quint16 videoPort =
            quint16(s.value(QStringLiteral("endpoint/video_port"), 3334).toInt());
        VideoReceiver* receiver = m_videoReceiver;
        const bool hw = !qEnvironmentVariableIsSet("RM_VIDEO_SW");
        QMetaObject::invokeMethod(receiver, [receiver, bindIp, videoPort, hw] {
            receiver->setHardwareDecode(hw);
            receiver->start(bindIp, videoPort);
        }, Qt::QueuedConnection);
        appendLog(tr("图传接收：UDP %1:%2（官方 HEVC 码流）").arg(bindIp).arg(videoPort));
    }

    // UI 以固定刷新率从 GameData 拉取，避免每条高频消息都触发重绘。
    m_hudTimer = new QTimer(this);
    m_hudTimer->setInterval(16); // ~60 Hz（与图传 60fps 对齐）
    connect(m_hudTimer, &QTimer::timeout, this, &MainWindow::refreshHud);
    m_hudTimer->start();

    // 键鼠 75Hz 上行：定时采样当前状态，避免每个输入事件都发包（官方上限 75Hz）。
    m_inputTimer = new QTimer(this);
    m_inputTimer->setTimerType(Qt::PreciseTimer);
    m_inputTimer->setInterval(13); // ~75 Hz
    connect(m_inputTimer, &QTimer::timeout, this, &MainWindow::sendKeyboardMouse);
    m_inputTimer->start();

    auto* esc = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(esc, &QShortcut::activated, this, [this] {
        if (m_video->startPanel()->allActionsVisible())
            m_video->startPanel()->hideOverlay();
        else if (m_video->actionOverlay()->isPrompting())
            m_video->actionOverlay()->cancel();
        else
            toggleGameMode();
        refreshHud();
    });

    // [I] 切换 UDP 图传 180° 反转（现场摄像头常倒装），ACE 同名快捷键。
    auto* invert = new QShortcut(QKeySequence(Qt::Key_I), this);
    connect(invert, &QShortcut::activated, this, [this] {
        m_udpInvert = !m_udpInvert;
        refreshHud();
    });

    // 小地图点击标记（HUD 叠加层内的小地图）。
    connect(m_video->minimap(), &Minimap::mapClicked, this, &MainWindow::sendMapMarker);
    connect(sp, &StartPanelOverlay::markerModeChanged, this, [this](bool on) {
        m_video->minimap()->setMarkerMode(on);
        // ~ 面板为整块不透明控件，标记模式下把可点击的小地图抬到最上层。
        if (on)
            m_video->minimap()->raise();
    });
}

MainWindow::~MainWindow()
{
    if (m_videoThread) {
        m_videoThread->quit();
        m_videoThread->wait();
    }
}

bool MainWindow::eventFilter(QObject* obj, QEvent* event)
{
    const QEvent::Type t = event->type();

    auto* w = qobject_cast<QWidget*>(obj);
    const bool ownWindow = w && w->window() == this;

    // 鼠标按键 / 滚轮：更新 75Hz 上行状态，不拦截事件（界面仍能正常交互）。
    if (ownWindow && (t == QEvent::MouseButtonPress || t == QEvent::MouseButtonRelease)) {
        auto* me = static_cast<QMouseEvent*>(event);
        const bool down = (t == QEvent::MouseButtonPress);
        if (me->button() == Qt::LeftButton)
            m_leftDown = down;
        else if (me->button() == Qt::RightButton)
            m_rightDown = down;
        else if (me->button() == Qt::MiddleButton)
            m_midDown = down;
        return QMainWindow::eventFilter(obj, event);
    }
    if (ownWindow && t == QEvent::Wheel) {
        auto* we = static_cast<QWheelEvent*>(event);
        m_wheelTicks += we->angleDelta().y() / 120; // 一格 = 120，滚轮向前为正
        return QMainWindow::eventFilter(obj, event);
    }

    if (t != QEvent::KeyPress && t != QEvent::KeyRelease && t != QEvent::ShortcutOverride)
        return QMainWindow::eventFilter(obj, event);

    if (!ownWindow)
        return QMainWindow::eventFilter(obj, event);

    auto* ke = static_cast<QKeyEvent*>(event);
    if (ke->isAutoRepeat())
        return QMainWindow::eventFilter(obj, event);

    // 官方 16 个受支持按键：更新位掩码（不消费，配置面板输入框仍可正常输入）。
    if (t == QEvent::KeyPress || t == QEvent::KeyRelease) {
        const int bit = proto::keyboardMouseBit(static_cast<Qt::Key>(ke->key()));
        if (bit >= 0) {
            if (t == QEvent::KeyPress)
                m_keyMask |= (1u << bit);
            else
                m_keyMask &= ~(1u << bit);
        }
    }

    const bool isTab = ke->key() == Qt::Key_Tab;
    const bool isM = ke->key() == Qt::Key_M;
    // 反引号键（`/~）在不同布局下可能是 QuoteLeft 或 AsciiTilde，两者都认。
    const bool isTilde = ke->key() == Qt::Key_AsciiTilde || ke->key() == Qt::Key_QuoteLeft;
    const bool isT = ke->key() == Qt::Key_T;
    const bool isX = ke->key() == Qt::Key_X;
    if (!isTab && !isM && !isTilde && !isT && !isX)
        return QMainWindow::eventFilter(obj, event);

    // ~ 即使在输入框聚焦时也生效（端口/弹量输入框里 ~ 无意义），否则浮窗打开后无法用 ~ 关闭。
    if (textInputHasFocus() && !isTilde)
        return QMainWindow::eventFilter(obj, event);

    if (t == QEvent::ShortcutOverride) {
        // 拦下可能的快捷键绑定（如地图标记 M），改由下面的按下/松手逻辑处理。
        ke->accept();
        return true;
    }
    if (isTab) {
        setVideoLogVisible(t == QEvent::KeyPress);
        setStatsTableVisible(t == QEvent::KeyPress);
    } else if (isM) {
        setBigMapVisible(t == QEvent::KeyPress);
    } else if (isTilde) {
        // [~] 调出/收起特定机器人功能浮窗集合。
        if (t == QEvent::KeyPress)
            toggleActionPanel();
    } else if (isX) {
        setDamagePanelVisible(t == QEvent::KeyPress);
    } else {
        setStatsTableVisible(t == QEvent::KeyPress);
    }
    return true;
}

void MainWindow::setVideoLogVisible(bool on)
{
    if (m_showVideoLog == on)
        return;
    m_showVideoLog = on;
    refreshHud();
}

void MainWindow::setBigMapVisible(bool on)
{
    if (m_bigMap == on)
        return;
    m_bigMap = on;
    m_video->setBigMapVisible(on);
}

void MainWindow::setDamagePanelVisible(bool on)
{
    if (m_showDamagePanel == on)
        return;
    m_showDamagePanel = on;
    refreshHud();
}

void MainWindow::setStatsTableVisible(bool on)
{
    if (m_showStatsTable == on)
        return;
    m_showStatsTable = on;
    refreshHud();
}

void MainWindow::toggleActionPanel()
{
    StartPanelOverlay* sp = m_video->startPanel();
    if (sp->allActionsVisible())
        sp->hideOverlay();
    else
        sp->showAllActions();
    refreshHud();
}

void MainWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);
    if (!m_autoFullscreenDone) {
        m_autoFullscreenDone = true;
        // 启动即进入全屏，并显示机器人配置。
        enterFullscreen();
        QTimer::singleShot(0, this, [this] { m_video->startPanel()->showConfig(); });

        // RM_PROMPT_ACTION=<功能 id>：直接弹出该功能的指令界面（便于无输入工具时验证）。
        const QString promptId = qEnvironmentVariable("RM_PROMPT_ACTION");
        if (!promptId.isEmpty()) {
            QTimer::singleShot(0, this, [this, promptId] {
                if (const UserAction* a = userAction(promptId))
                    m_video->actionOverlay()->promptAction(*a);
            });
        }
        // RM_ACTION_PANEL=<ammo|assembly|...|all>：直接显示专用操作面板（便于无输入工具时验证）。
        const QString panelName = qEnvironmentVariable("RM_ACTION_PANEL");
        if (!panelName.isEmpty()) {
            QTimer::singleShot(0, this, [this, panelName] {
                if (panelName == QLatin1String("all"))
                    m_video->startPanel()->showAllActions();
                else
                    m_video->startPanel()->showActionPanel(panelName);
            });
        }
        // RM_AUTOCONNECT=1 用 QSettings 里保存的端点直接连接（配置面板仍显示连接状态）。
        if (qEnvironmentVariableIsSet("RM_AUTOCONNECT"))
            autoConnectFromSettings();

        // 验证用：直接置位“按住 Tab / M”的效果（无输入工具时截图）。
        if (qEnvironmentVariableIsSet("RM_SHOW_VIDEO_LOG"))
            QTimer::singleShot(0, this, [this] { setVideoLogVisible(true); });
        if (qEnvironmentVariableIsSet("RM_BIG_MAP"))
            QTimer::singleShot(0, this, [this] { setBigMapVisible(true); });
        // RM_GAME=1：直接切到比赛视图（HUD），便于无输入工具时验证伤害面板/统计表等。
        // 可选 RM_DAMAGE_PANEL / RM_STATS_TABLE 直接展开对应面板。
        if (qEnvironmentVariableIsSet("RM_GAME")) {
            QTimer::singleShot(0, this, [this] {
                m_gameMode = true;
                if (!qEnvironmentVariableIsSet("RM_ACTION_PANEL"))
                    m_video->startPanel()->hideOverlay();
                if (qEnvironmentVariableIsSet("RM_DAMAGE_PANEL"))
                    m_showDamagePanel = true;
                if (qEnvironmentVariableIsSet("RM_STATS_TABLE"))
                    m_showStatsTable = true;
                refreshHud();
            });
        }
    }
}

void MainWindow::autoConnectFromSettings()
{
    QSettings s;
    m_clientId = s.value(QStringLiteral("endpoint/client_id"), QStringLiteral("1")).toString();
    m_teamIsRed = s.value(QStringLiteral("start/team"), 0).toInt() == 0;
    m_robotName = s.value(QStringLiteral("start/robot_name"), QStringLiteral("机器人")).toString();
    m_robotType = s.value(QStringLiteral("start/robot"), 0).toInt();
    m_game->setAllyIsRed(m_teamIsRed);
    m_game->setSelfRobotId(m_clientId.toUInt());
    m_game->setSelfName(m_robotName);
    appendLog(tr("自动连接（RM_AUTOCONNECT）：clientID=%1").arg(m_clientId));
    connectMqtt(s.value(QStringLiteral("endpoint/mqtt_host"), QStringLiteral("127.0.0.1")).toString(),
                s.value(QStringLiteral("endpoint/mqtt_port"), 1883).toInt(), m_clientId);
}

void MainWindow::connectMqtt(const QString& host, int port, const QString& clientId)
{
    if (m_mqtt->isConnected())
        m_mqtt->disconnectFromBroker();
    m_lastEndpoint = QStringLiteral("%1:%2").arg(host).arg(port);
    appendLog(tr("正在连接 MQTT %1 clientID=%2 ...").arg(m_lastEndpoint, clientId));
    m_mqtt->connectToBroker(host, static_cast<quint16>(port), clientId);
}

void MainWindow::refreshHud()
{
    HudData hud = m_game->hudData();
    hud.logs = m_logs;
    hud.udpInvert = m_udpInvert;
    hud.showCrosshair = m_gameMode && m_showCrosshair;
    hud.showVideoLog = m_showVideoLog;
    hud.showDamagePanel = m_showDamagePanel;
    hud.showStatsTable = m_showStatsTable;
    hud.showMinimap = m_showMinimap;
    hud.showFps = m_showFps;
    hud.fps = m_fps;
    hud.connectionLost = !m_mqtt->isConnected();
    // 图传：UDP（官方 HEVC 码流）为主源；custom 源未接入。
    hud.videoSource = 0;
    hud.udpVideoReady = m_udpVideoReady;
    hud.customVideoReady = false;
    m_video->setHudData(hud);
    m_video->setActionData(hud);
    m_video->startPanel()->setData(hud);

    // FPS 由 HUD 刷新频率统计（约 60Hz 定时器）。
    if (!m_fpsClock.isValid())
        m_fpsClock.start();
    ++m_fpsFrames;
    if (m_fpsClock.elapsed() >= 1000) {
        m_fps = int(qint64(m_fpsFrames) * 1000 / qMax<qint64>(1, m_fpsClock.elapsed()));
        m_fpsFrames = 0;
        m_fpsClock.restart();
    }

    // HUD 叠加层内的小地图使用雷达/路径数据 + 基地/符文/指令标记。
    m_video->minimap()->setData(hud.radarValid, hud.allyIsRed, hud.radarUnits, hud.pathPoints,
                                hud.selfRobotId, hud.runeStatus, hud.runeArms, hud.runeRings,
                                hud.mapMarkers);
    m_video->minimap()->setSelfPosition(hud.selfPosValid, hud.selfPosX, hud.selfPosY, hud.selfYaw);
}

void MainWindow::appendLog(const QString& text, int type)
{
    HudLogEntry e;
    e.time = QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
    e.source = tr("系统");
    e.text = text;
    e.type = type;
    m_logs.append(e);
    while (m_logs.size() > 50)
        m_logs.removeFirst();
}

void MainWindow::onVideoFrame(const QImage& image)
{
    // 摄像头倒装：按 [I] 的 UDP 反转开关旋转 180°。
    m_video->showFrame(m_udpInvert ? image.transformed(QTransform().rotate(180)) : image);
}

void MainWindow::onVideoSignal(bool active)
{
    if (m_udpVideoReady == active)
        return;
    m_udpVideoReady = active;
    refreshHud();
}

void MainWindow::toggleGameMode()
{
    if (m_video->startPanel()->overlayVisible()) {
        // 面板已显示 -> 收起，回到比赛视图。
        m_gameMode = true;
        m_video->startPanel()->hideOverlay();
    } else {
        // 比赛视图 -> 显示机器人配置（登录 / 灵敏度 / 性能体系）。
        m_gameMode = false;
        m_video->startPanel()->showConfig();
    }
    refreshHud();
}

void MainWindow::sendMapMarker(float mapX, float mapY)
{
    robomaster::MapClickCmd m;
    m.set_is_send_all(m_video->startPanel()->markerIncludeSentry() ? 2 : 1);
    m.set_robot_id(std::string(7, '\0'));
    m.set_mode(1); // 标记类型：地图
    m.set_enemy_id(0);
    m.set_ascii(0);
    m.set_type(1); // 标记模式：攻击
    m.set_map_x(mapX);
    m.set_map_y(mapY);
    std::string s;
    m.SerializeToString(&s);
    executeCommand(QStringLiteral("MapClickCmd"), QByteArray(s.data(), int(s.size())));

    // 发完自动退出标记模式
    m_video->startPanel()->setMarkerMode(false);
}

bool MainWindow::inputActive() const
{
    // 仅比赛视图（配置面板/操作面板/参数弹窗均收起）下才向上行键鼠。
    // ActionOverlay 可见 = 指令界面或死亡/复活界面，两者都要放开鼠标。
    return m_gameMode && m_mqtt->isConnected()
           && !m_video->startPanel()->overlayVisible()
           && !m_video->actionOverlay()->isVisible();
}

bool MainWindow::mouseCaptured() const
{
    // 无任何浮窗（含 Tab 左列/数据表、残留的标记模式）时才捕获原始增量并隐藏光标。
    return inputActive() && !m_showVideoLog && !m_showStatsTable
           && !m_video->startPanel()->markerMode();
}

void MainWindow::setCursorCaptured(bool on)
{
    if (m_cursorHidden == on)
        return;
    m_cursorHidden = on;
    m_video->setCursor(on ? Qt::BlankCursor : Qt::ArrowCursor);
}

void MainWindow::publishKeyboardMouse(int mouseX, int mouseY, int mouseZ)
{
    robomaster::KeyboardMouseControl m;
    m.set_mouse_x(mouseX);
    m.set_mouse_y(mouseY);
    m.set_mouse_z(mouseZ);
    m.set_left_button_down(m_leftDown);
    m.set_right_button_down(m_rightDown);
    m.set_mid_button_down(m_midDown);
    m.set_keyboard_value(m_keyMask);
    std::string s;
    m.SerializeToString(&s);
    m_mqtt->publish(QStringLiteral("KeyboardMouseControl"), QByteArray(s.data(), int(s.size())));
}

void MainWindow::sendKeyboardMouse()
{
    if (!inputActive()) {
        // 非激活期间（配置面板/弹窗）累积的滚轮不进上行。
        m_wheelTicks = 0;
        setCursorCaptured(false);
        if (m_inputActive) {
            // 退出比赛 / 弹出界面：补发一帧归零，松开全部按键与鼠标。
            m_inputActive = false;
            m_mouseTracked = false;
            m_keyMask = 0;
            m_leftDown = m_rightDown = m_midDown = false;
            publishKeyboardMouse(0, 0, 0);
        }
        return;
    }

    int mouseX = 0;
    int mouseY = 0;
    if (mouseCaptured()) {
        // 捕获：每帧回中取原始增量（负 Y 已按协议翻转），光标保持隐藏。
        const QPoint center = mapToGlobal(rect().center());
        const QPoint pos = QCursor::pos();
        if (m_mouseTracked) {
            const QPoint d = pos - m_lastMousePos;
            const double sens = m_video->startPanel()->mouseSensitivity();
            mouseX = int(d.x() * sens);
            mouseY = int(-d.y() * sens); // 协议：mouse_y 负值向下
        }
        QCursor::setPos(center);
        m_lastMousePos = center;
        m_mouseTracked = true;
        setCursorCaptured(true);
    } else {
        // 浮窗/标记模式：显示光标，不转云台，重新捕获时重取基准。
        setCursorCaptured(false);
        m_mouseTracked = false;
    }

    const int wheel = m_wheelTicks;
    m_wheelTicks = 0;
    publishKeyboardMouse(mouseX, mouseY, wheel);
    m_inputActive = true;
}

void MainWindow::onMqttConnected()
{
    for (const QString& topic : GameData::downlinkTopics())
        m_mqtt->subscribe(topic);
    m_video->startPanel()->setConnectionState(2);
    appendLog(tr("MQTT 已连接 %1 clientID=%2，已订阅 %3 个下行 topic")
                           .arg(m_lastEndpoint, m_clientId)
                           .arg(GameData::downlinkTopics().size()));
}

void MainWindow::onMqttDisconnected(const QString& cause)
{
    const QString reason = cause.isEmpty() ? tr("服务器主动断开或网络中断") : cause;
    m_video->startPanel()->setConnectionState(m_clientId.isEmpty() ? 0 : 3, reason);
    appendLog(tr("MQTT 连接断开：%1").arg(reason));
}

void MainWindow::onMqttError(const QString& message)
{
    m_video->startPanel()->setConnectionState(3, tr("连接失败：%1").arg(message));
    appendLog(tr("连接失败：%1").arg(message));
}

void MainWindow::onMqttMessage(const QString& topic, const QByteArray& payload)
{
    m_game->apply(topic, payload);
}

void MainWindow::createCentral()
{
    m_video = new VideoView(this);
    setCentralWidget(m_video);
}

void MainWindow::onControlCommand(const QString& actionId)
{
    const UserAction* action = userAction(actionId);
    if (!action)
        return;
    // 带专用面板的功能：先在启动面板里显示对应操作面板。
    if (!action->panel.isEmpty()) {
        m_video->startPanel()->showActionPanel(action->panel);
        refreshHud();
        return;
    }
    // 其余功能弹指令界面（可选参数 + 确认），确认后再发送。
    m_video->actionOverlay()->promptAction(*action);
}

void MainWindow::executeCommand(const QString& topic, const QByteArray& payload)
{
    if (!m_mqtt->isConnected()) {
        appendLog(tr("未连接服务器，指令未发送: %1").arg(topic));
        return;
    }
    m_mqtt->publish(topic, payload);
    appendLog(tr("发送指令: %1（%2 字节）").arg(topic).arg(payload.size()));
}

void MainWindow::enterFullscreen()
{
    if (m_fullscreen)
        return;
    m_fullscreen = true;
    m_video->setHudVisible(true);
    showFullScreen();
}

void MainWindow::setFullscreen(bool on)
{
    m_fullscreen = on;
    m_video->setHudVisible(true);
    if (on)
        showFullScreen();
    else
        showNormal();
}

} // namespace rm
