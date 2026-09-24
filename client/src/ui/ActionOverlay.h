#pragma once

#include <QWidget>

#include "core/HudData.h"

class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QSpinBox;

namespace rm {

struct UserAction;

// 全屏图传上的交互叠加层：
//  - 触发行令时显示"指令界面"（可选参数 + 确认/取消）；
//  - 本车被击毁时显示"死亡界面"（复活进度条 + 复活选项）。
class ActionOverlay : public QWidget
{
    Q_OBJECT
public:
    explicit ActionOverlay(QWidget* parent = nullptr);

    // 显示某功能的确认/参数界面。
    void promptAction(const UserAction& action);
    // 刷新死亡界面（由 HUD 数据驱动；未死亡则隐藏）。
    void setData(const HudData& data);

    bool isPrompting() const { return m_action != nullptr; }
    void confirm(); // Enter / 确认按钮
    void cancel();  // Esc / 取消按钮

signals:
    void confirmed(const QString& topic, const QByteArray& payload);

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    void updateVisibility();
    void doRespawn(int cmdType);

    // 指令界面
    QWidget* m_actionPanel = nullptr;
    QLabel* m_actionTitle = nullptr;
    QLabel* m_paramLabel = nullptr;
    QSpinBox* m_paramSpin = nullptr;
    QComboBox* m_paramCombo = nullptr;
    const UserAction* m_action = nullptr;

    // 死亡界面
    QWidget* m_deathPanel = nullptr;
    QLabel* m_deathInfo = nullptr;
    QProgressBar* m_deathBar = nullptr;
    QPushButton* m_respawnFree = nullptr;
    QPushButton* m_respawnPay = nullptr;
    bool m_dead = false;
};

} // namespace rm
