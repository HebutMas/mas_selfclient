#pragma once

#include <QByteArray>
#include <QWidget>

#include "core/HudData.h"

class QCheckBox;
class QFrame;
class QLabel;
class QLineEdit;
class QPushButton;
class QResizeEvent;
class QSlider;
class QSpinBox;
class QButtonGroup;

namespace rm {

// 启动/控制面板（居中叠加层，非模态）。
class StartPanelOverlay : public QWidget
{
    Q_OBJECT
public:
    explicit StartPanelOverlay(QWidget* parent = nullptr);

    // 实时状态（由 MainWindow 从 GameData 拉取后注入）。
    void setData(const HudData& data);
    // 连接状态：0 未登录 / 1 连接中 / 2 已登录 / 3 连接失败。
    void setConnectionState(int state, const QString& detail = QString());
    // 局域网参数（由 MainWindow 在断连时更新错误提示）。
    void setEndpointError(const QString& text);

    double mouseSensitivity() const;
    void setMouseSensitivity(double value);
    // 显示设置（准星/小地图/FPS）回填到配置面板。
    void setDisplayOptions(bool crosshair, bool minimap, bool fps);

    bool markerMode() const;
    bool markerIncludeSentry() const;
    void setMarkerMode(bool on);

    // 视图：Config = 机器人配置（登录/灵敏度/性能体系）；Action = 按键触发的专用面板。
    void showConfig();
    void showActionPanel(const QString& panel);
    // 浮窗集合：一次性显示本兵种全部专用面板（~ 键调出，参考裁判手册功能集合）。
    void showAllActions();
    bool allActionsVisible() const;
    void hideOverlay();
    bool overlayVisible() const;

    bool teamIsRed() const { return m_teamOffset == 0; }
    int robotType() const { return m_robotOffset; }

protected:
    void resizeEvent(QResizeEvent* event) override;

signals:
    void loginRequested(const QString& host, int port, const QString& clientId);
    void logoutRequested();
    void enterGameRequested();
    void exitRequested();
    void commandRequested(const QString& topic, const QByteArray& payload);
    void mouseSensitivityChanged(double value);
    void markerModeChanged(bool on);
    // 显示设置变化：准星 / 小地图 / FPS。
    void displayOptionsChanged(bool crosshair, bool minimap, bool fps);
    void fullscreenChanged(bool fullscreen);
    void volumeChanged(int value);

private:
    QWidget* buildLoginSection();
    QWidget* buildControlSection();
    QWidget* buildDisplaySection();
    QWidget* buildAmmoPanel();
    QWidget* buildAssemblyPanel();
    QWidget* buildDeployPanel();
    QWidget* buildRecoverPanel();
    QWidget* buildRevivePanel();
    QWidget* buildMarkerPanel();
    QWidget* buildPerformancePanel();
    QWidget* buildDartPanel();
    QWidget* buildAirSupportPanel();

    void applyRobotVisibility();
    void updatePanelHeight();
    void updateLoginTargetText();
    void refreshAmmoFeedback();
    void refreshAssemblyFeedback();
    void refreshReviveFeedback();
    void refreshAirSupportFeedback();
    void updatePrimaryButton();

    int m_teamOffset = 0;      // 0 红 / 100 蓝
    int m_robotOffset = 1;     // 1..9
    bool m_connected = false;
    int m_connState = 0;
    HudData m_data;

    enum class View { Config, Action, Hidden };
    View m_view = View::Config;
    QString m_actionPanel;     // Action 视图下显示的专用面板名
    bool m_allActions = false; // Action 视图：显示全部专用面板（浮窗集合）
    QWidget* m_loginBox = nullptr;   // 配置视图：登录块
    QWidget* m_sensWidget = nullptr; // 配置视图：鼠标灵敏度
    QWidget* m_primaryWidget = nullptr; // 配置视图：主按钮
    QLabel* m_viewHint = nullptr;    // Action 视图：返回提示
    QLabel* m_ammoCaliber = nullptr; // 弹药兑换口径提示（17mm/42mm）

    // 登录块
    QLabel* m_connStatus = nullptr;
    QLineEdit* m_endpoint = nullptr;
    QLabel* m_endpointError = nullptr;
    QLineEdit* m_bindIp = nullptr;
    QSpinBox* m_videoPort = nullptr;
    QPushButton* m_teamRed = nullptr;
    QPushButton* m_teamBlue = nullptr;
    QVector<QPushButton*> m_robotButtons;
    QLabel* m_loginTarget = nullptr;
    QPushButton* m_logoutButton = nullptr;
    QWidget* m_loginControls = nullptr;   // 未连接时显示的控件组
    QPushButton* m_primary = nullptr;
    QPushButton* m_exitButton = nullptr;

    // 控制面板
    QWidget* m_ammoPanel = nullptr;
    QSpinBox* m_ammoSpin = nullptr;
    QPushButton* m_ammoLocal = nullptr;
    QPushButton* m_ammoRemote = nullptr;
    QLabel* m_ammoInfo = nullptr;

    QWidget* m_assemblyPanel = nullptr;
    QSpinBox* m_assemblySpin = nullptr;
    QPushButton* m_assemblyCancel = nullptr;
    QPushButton* m_assemblyConfirm = nullptr;
    QLabel* m_assemblyInfo = nullptr;

    QWidget* m_deployPanel = nullptr;
    QPushButton* m_deployButton = nullptr;

    QWidget* m_recoverPanel = nullptr;
    QPushButton* m_recoverButton = nullptr;

    QWidget* m_revivePanel = nullptr;
    QPushButton* m_reviveFree = nullptr;
    QPushButton* m_revivePay = nullptr;
    QLabel* m_reviveInfo = nullptr;

    QWidget* m_markerPanel = nullptr;
    QPushButton* m_markerToggle = nullptr;
    QCheckBox* m_markerSentry = nullptr;

    QWidget* m_perfPanel = nullptr;
    QButtonGroup* m_shooterGroup = nullptr;
    QButtonGroup* m_chassisGroup = nullptr;

    QWidget* m_dartPanel = nullptr;
    QButtonGroup* m_dartGroup = nullptr;
    QPushButton* m_dartGate = nullptr;
    QPushButton* m_dartFire = nullptr;

    QWidget* m_airPanel = nullptr;
    QLabel* m_airStatus = nullptr;
    QPushButton* m_airFree = nullptr;
    QPushButton* m_airPaid = nullptr;
    QPushButton* m_airCancel = nullptr;
    QPushButton* m_airRelease = nullptr;

    QSlider* m_sensitivity = nullptr;
    QFrame* m_panel = nullptr;
    QWidget* m_content = nullptr;    // 面板滚动区内容（用于按内容自适应高度）

    // 显示设置
    QWidget* m_displayWidget = nullptr;
    QCheckBox* m_crosshairBox = nullptr;
    QCheckBox* m_minimapBox = nullptr;
    QCheckBox* m_fpsBox = nullptr;
    QPushButton* m_fullscreenButton = nullptr;
    QSlider* m_volume = nullptr;
    bool m_fullscreen = true;
};

} // namespace rm
