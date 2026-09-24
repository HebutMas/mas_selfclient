#pragma once

#include <QElapsedTimer>
#include <QMainWindow>
#include <QPoint>
#include <QVector>

#include "core/HudData.h"

class QShortcut;
class QShowEvent;
class QTimer;
class QThread;
class QImage;

namespace rm {

class VideoView;
class MqttClient;
class GameData;
class VideoReceiver;

// 纯全屏客户端：启动即进入全屏，显示机器人配置（连接配置 + 连接状态）；
// 进入比赛后隐藏配置、显示 HUD，Esc 在配置与比赛视图间切换。
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private slots:
    void onControlCommand(const QString& actionId);
    void onMqttConnected();
    void onMqttDisconnected(const QString& cause);
    void onMqttError(const QString& message);
    void onMqttMessage(const QString& topic, const QByteArray& payload);
    void onVideoFrame(const QImage& image);
    void onVideoSignal(bool active);

private:
    void createCentral();
    void enterFullscreen();
    void toggleGameMode();
    void setVideoLogVisible(bool on);
    void setBigMapVisible(bool on);
    void setDamagePanelVisible(bool on);
    void setStatsTableVisible(bool on);
    void toggleActionPanel();
    void setFullscreen(bool on);
    void appendLog(const QString& text, int type = 0);
    void sendMapMarker(float mapX, float mapY);
    // 75Hz 键鼠上行：采样当前键鼠状态并发布 KeyboardMouseControl。
    void sendKeyboardMouse();
    void publishKeyboardMouse(int mouseX, int mouseY, int mouseZ);
    bool inputActive() const;
    // 无浮窗时捕获原始鼠标增量并隐藏光标（参考 ACE）。
    bool mouseCaptured() const;
    void setCursorCaptured(bool on);
    void autoConnectFromSettings();
    void connectMqtt(const QString& host, int port, const QString& clientId);
    void refreshHud();
    void executeCommand(const QString& topic, const QByteArray& payload);

    VideoView* m_video = nullptr;

    bool m_fullscreen = false;
    bool m_autoFullscreenDone = false;

    QString m_clientId;
    QString m_robotName;
    QString m_lastEndpoint; // 形如 127.0.0.1:1883，用于连接状态提示
    int m_robotType = 0;    // 兵种编号（RobotType），决定控制指令面板显示哪些按钮
    bool m_teamIsRed = true;

    // 比赛模式 = 隐藏配置面板 + 显示准星；配置模式 = 显示配置面板。
    bool m_gameMode = false;
    QVector<HudLogEntry> m_logs;
    bool m_udpInvert = false; // 图传 180° 反转（[I] 切换）
    bool m_showVideoLog = false; // 按住 Tab 显示左列本车视频/日志
    bool m_bigMap = false;       // 按住 M 显示大地图
    bool m_showDamagePanel = false; // 按住 X 显示本车受伤统计
    bool m_showStatsTable = false;  // 按住 Tab 显示双方机器人数据表
    bool m_showMinimap = true;      // 设置项：显示小地图
    bool m_showFps = false;         // 设置项：显示 FPS
    bool m_showCrosshair = true;    // 设置项：显示准星
    int m_fps = 0;                  // 由 HUD 刷新定时器统计
    int m_fpsFrames = 0;
    QElapsedTimer m_fpsClock;

    // 键鼠 75Hz 上行状态。
    QTimer* m_inputTimer = nullptr;
    quint32 m_keyMask = 0;   // keyboard_value 位掩码
    bool m_leftDown = false;
    bool m_rightDown = false;
    bool m_midDown = false;
    int m_wheelTicks = 0;    // 累加的滚轮格数，发送后清零
    bool m_mouseTracked = false;
    QPoint m_lastMousePos;
    bool m_inputActive = false;
    bool m_cursorHidden = false;

    MqttClient* m_mqtt = nullptr;
    GameData* m_game = nullptr;
    QTimer* m_hudTimer = nullptr;
    QVector<QShortcut*> m_actionShortcuts;

    // 官方 UDP 图传（端口 3334）在独立线程接收 + FFmpeg 解码。
    VideoReceiver* m_videoReceiver = nullptr;
    QThread* m_videoThread = nullptr;
    bool m_udpVideoReady = false;
};

} // namespace rm
