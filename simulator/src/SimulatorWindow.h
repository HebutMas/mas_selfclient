#pragma once

#include <QWidget>

class QLabel;
class QComboBox;
class QSpinBox;
class QPushButton;
class QAction;

namespace rm {

class Simulator;

/// 裁判系统模拟器的窗口化控制台，参考 RM26 web 控制台布局：
/// 比赛状态 / 基本控制 / 得分控制 / 事件发送（分类菜单）/ Buff 控制。
class SimulatorWindow : public QWidget {
  Q_OBJECT
public:
  explicit SimulatorWindow(Simulator* sim, QWidget* parent = nullptr);

private slots:
  void refresh();

private:
  QWidget* buildMatchStatus();
  QWidget* buildBasicControl();
  QWidget* buildScoreControl();
  QWidget* buildEconomy();
  QWidget* buildEventSender();
  QWidget* buildBuffControl();
  QWidget* buildVideoControl();
  void activateRune(bool big);
  void updateRuneTeam();

  Simulator* m_sim = nullptr;
  QLabel* m_connLabel = nullptr;
  QLabel* m_stageLabel = nullptr;
  QLabel* m_roundLabel = nullptr;
  QLabel* m_timeLabel = nullptr;
  QLabel* m_redScoreLabel = nullptr;
  QLabel* m_blueScoreLabel = nullptr;
  QLabel* m_selectLabel = nullptr;
  QLabel* m_economyLabel = nullptr;
  QComboBox* m_robotCombo = nullptr;
  QSpinBox* m_roundSpin = nullptr;
  QSpinBox* m_healthSpin = nullptr;
  QSpinBox* m_energySpin = nullptr;
  QSpinBox* m_expSpin = nullptr;
  QPushButton* m_autoButton = nullptr;
  QPushButton* m_pauseButton = nullptr;
  QWidget* m_economyWidget = nullptr;
  QAction* m_runeRedAction = nullptr;
  QAction* m_runeBlueAction = nullptr;
  QPushButton* m_videoButton = nullptr;
  QLabel* m_videoLabel = nullptr;
  int m_runeTeam = 0;   // 0=红方 1=蓝方（proto 的 Event 无阵营字段，仅界面标注）
};

} // namespace rm
