#include "SimulatorWindow.h"

#include "Simulator.h"

#include <QAction>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

#include <functional>

namespace rm {

namespace {

QString stageName(int s)
{
  switch (s) {
  case 0: return QStringLiteral("未开始");
  case 1: return QStringLiteral("准备阶段");
  case 2: return QStringLiteral("自检阶段");
  case 3: return QStringLiteral("倒计时");
  case 4: return QStringLiteral("比赛中");
  case 5: return QStringLiteral("结算中");
  default: return QStringLiteral("未知");
  }
}

QString mmss(int sec)
{
  sec = qMax(0, sec);
  return QStringLiteral("%1:%2")
      .arg(sec / 60, 2, 10, QChar('0'))
      .arg(sec % 60, 2, 10, QChar('0'));
}

QGroupBox* group(const QString& title, QWidget* parent)
{
  auto* g = new QGroupBox(title, parent);
  g->setStyleSheet(QStringLiteral("QGroupBox{font-weight:bold;}"));
  return g;
}

QPushButton* button(const QString& text, QWidget* parent)
{
  auto* b = new QPushButton(text, parent);
  b->setCursor(Qt::PointingHandCursor);
  return b;
}

const QString kRobotNames[10] = {QString(),      QStringLiteral("1 重装"),
                                 QStringLiteral("2 工程"), QStringLiteral("3 步兵"),
                                 QStringLiteral("4 步兵"), QStringLiteral("5 步兵"),
                                 QStringLiteral("6 无人机"), QStringLiteral("7 哨兵"),
                                 QStringLiteral("8 飞镖"), QStringLiteral("9 雷达")};

const char* const kBuffNames[8] = {"", "攻击增益", "防御·易伤", "射击热量冷却",
                                   "底盘功率", "回血", "可兑换发弹量", "地形跨越"};
// 按裁判手册：制高点 50% 防御；隧道穿越 1.5×热量冷却持续 120s；跨越隧道 25% 防御 30s。
const int kBuffLevel[8] = {0, 30, 50, 50, 30, 30, 30, 25};
const int kBuffTime[8] = {0, 30, 30, 120, 30, 30, 30, 30};

} // namespace

SimulatorWindow::SimulatorWindow(Simulator* sim, QWidget* parent)
    : QWidget(parent), m_sim(sim)
{
  setWindowTitle(QStringLiteral("RoboMaster 比赛模拟器"));
  setStyleSheet(QStringLiteral(R"(
QWidget{background:#0e1116;color:#e6e6e6;font-size:13px;}
QGroupBox{border:1px solid #2a3442;border-radius:8px;margin-top:14px;padding:10px;}
QGroupBox::title{subcontrol-origin:margin;left:12px;padding:0 4px;color:#9fb0c6;}
QPushButton{background:#1b2330;border:1px solid #33415a;border-radius:5px;padding:5px 8px;color:#dbe6f5;}
QPushButton:hover{background:#243044;}
QPushButton:checked{background:#2b6cff;border-color:#2b6cff;color:#fff;}
QComboBox{background:#141b24;border:1px solid #33415a;border-radius:4px;padding:3px 6px;color:#dbe6f5;}
QSpinBox{background:#141b24;border:1px solid #33415a;border-radius:4px;color:#dbe6f5;}
QMenu{background:#141b24;border:1px solid #33415a;color:#dbe6f5;padding:4px;}
QMenu::item{padding:5px 18px;border-radius:4px;}
QMenu::item:selected{background:#2b6cff;color:#fff;}
QMenu::separator{height:1px;background:#33415a;margin:4px 8px;}
QLabel#scoreRed{color:#ff6b6b;font-size:34px;font-weight:bold;}
QLabel#scoreBlue{color:#5b9dff;font-size:34px;font-weight:bold;}
QLabel#conn{font-size:14px;}
)"));

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(18, 14, 18, 14);
  root->setSpacing(12);

  auto* header = new QHBoxLayout;
  auto* title = new QLabel(QStringLiteral("🎮 RoboMaster 比赛模拟器"), this);
  title->setStyleSheet(QStringLiteral("font-size:20px;font-weight:bold;"));
  m_connLabel = new QLabel(this);
  m_connLabel->setObjectName(QStringLiteral("conn"));
  m_connLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  header->addWidget(title);
  header->addStretch(1);
  header->addWidget(m_connLabel);
  root->addLayout(header);

  auto* grid = new QGridLayout;
  grid->setSpacing(12);
  grid->addWidget(buildMatchStatus(), 0, 0);
  grid->addWidget(buildBasicControl(), 0, 1);
  grid->addWidget(buildScoreControl(), 0, 2);
  grid->addWidget(buildEventSender(), 1, 0, 1, 2);
  grid->addWidget(buildBuffControl(), 1, 2);
  root->addLayout(grid);
  root->addStretch(1);

  connect(m_sim, &Simulator::stateChanged, this, &SimulatorWindow::refresh);
  connect(m_sim, &Simulator::connectionChanged, this, &SimulatorWindow::refresh);
  connect(m_sim, &Simulator::videoChanged, this, &SimulatorWindow::refresh);
  refresh();
}

QWidget* SimulatorWindow::buildMatchStatus()
{
  auto* g = group(QStringLiteral("📊 比赛状态"), this);
  auto* v = new QVBoxLayout(g);

  auto* stageRow = new QHBoxLayout;
  stageRow->addWidget(new QLabel(QStringLiteral("比赛阶段:"), g));
  stageRow->addStretch(1);
  m_stageLabel = new QLabel(g);
  m_stageLabel->setStyleSheet(QStringLiteral("font-weight:bold;color:#ffcc4d;"));
  stageRow->addWidget(m_stageLabel);
  v->addLayout(stageRow);

  auto* roundRow = new QHBoxLayout;
  roundRow->addWidget(new QLabel(QStringLiteral("当前局数:"), g));
  roundRow->addStretch(1);
  auto* minus = button(QStringLiteral("-"), g);
  minus->setFixedWidth(30);
  m_roundSpin = new QSpinBox(g);
  m_roundSpin->setRange(1, 5);
  m_roundSpin->setFixedWidth(54);
  m_roundSpin->setAlignment(Qt::AlignCenter);
  auto* plus = button(QStringLiteral("+"), g);
  plus->setFixedWidth(30);
  m_roundLabel = new QLabel(g);
  roundRow->addWidget(minus);
  roundRow->addWidget(m_roundSpin);
  roundRow->addWidget(plus);
  roundRow->addWidget(m_roundLabel);
  v->addLayout(roundRow);
  connect(minus, &QPushButton::clicked, this, [this] { m_sim->setRound(m_sim->round() - 1); });
  connect(plus, &QPushButton::clicked, this, [this] { m_sim->setRound(m_sim->round() + 1); });
  connect(m_roundSpin, &QSpinBox::valueChanged, this, [this](int r) { m_sim->setRound(r); });

  auto* timeRow = new QHBoxLayout;
  timeRow->addWidget(new QLabel(QStringLiteral("阶段时间:"), g));
  timeRow->addStretch(1);
  m_timeLabel = new QLabel(g);
  m_timeLabel->setStyleSheet(QStringLiteral("font-weight:bold;"));
  timeRow->addWidget(m_timeLabel);
  v->addLayout(timeRow);

  auto* scores = new QHBoxLayout;
  auto makeCard = [g](const QString& name, const QString& obj) {
    auto* card = new QGroupBox(g);
    card->setStyleSheet(QStringLiteral("QGroupBox{border:2px solid #33415a;border-radius:8px;}"));
    auto* cv = new QVBoxLayout(card);
    auto* nameLabel = new QLabel(name, card);
    nameLabel->setAlignment(Qt::AlignCenter);
    auto* value = new QLabel(QStringLiteral("0"), card);
    value->setObjectName(obj);
    value->setAlignment(Qt::AlignCenter);
    cv->addWidget(nameLabel);
    cv->addWidget(value);
    return qMakePair(card, value);
  };
  const auto redCard = makeCard(QStringLiteral("红方"), QStringLiteral("scoreRed"));
  const auto blueCard = makeCard(QStringLiteral("蓝方"), QStringLiteral("scoreBlue"));
  m_redScoreLabel = redCard.second;
  m_blueScoreLabel = blueCard.second;
  scores->addWidget(redCard.first);
  scores->addWidget(blueCard.first);
  v->addLayout(scores);

  v->addWidget(new QLabel(QStringLiteral("比赛控制"), g));
  auto* ctrl = new QGridLayout;
  ctrl->setSpacing(6);
  auto* startBtn = button(QStringLiteral("▶ 开始比赛"), g);
  m_pauseButton = button(QStringLiteral("⏸ 暂停/继续"), g);
  auto* endBtn = button(QStringLiteral("■ 结束比赛"), g);
  auto* resetBtn = button(QStringLiteral("⟲ 重置"), g);
  ctrl->addWidget(startBtn, 0, 0, 1, 2);
  ctrl->addWidget(m_pauseButton, 0, 2, 1, 2);
  ctrl->addWidget(endBtn, 1, 0, 1, 2);
  ctrl->addWidget(resetBtn, 1, 2, 1, 2);
  v->addLayout(ctrl);
  connect(startBtn, &QPushButton::clicked, this, [this] { m_sim->setStage(1); });
  connect(m_pauseButton, &QPushButton::clicked, this, [this] { m_sim->setPaused(!m_sim->paused()); });
  connect(endBtn, &QPushButton::clicked, this, [this] { m_sim->setStage(5); });
  connect(resetBtn, &QPushButton::clicked, this, [this] { m_sim->resetAll(); });

  v->addWidget(new QLabel(QStringLiteral("阶段控制"), g));
  auto* stages = new QGridLayout;
  stages->setSpacing(6);
  const struct { const char* text; int stage; } kStages[] = {
      {"未开始", 0}, {"准备阶段", 1}, {"自检阶段", 2},
      {"倒计时", 3}, {"比赛中", 4}, {"结算中", 5}};
  for (int i = 0; i < 6; ++i) {
    auto* b = button(QString::fromUtf8(kStages[i].text), g);
    const int st = kStages[i].stage;
    connect(b, &QPushButton::clicked, this, [this, st] { m_sim->setStage(st); });
    stages->addWidget(b, i / 3, i % 3);
  }
  v->addLayout(stages);
  v->addStretch(1);

  return g;
}

QWidget* SimulatorWindow::buildBasicControl()
{
  auto* g = group(QStringLiteral("🎛 基本控制"), this);
  auto* v = new QVBoxLayout(g);

  v->addWidget(new QLabel(QStringLiteral("机器人选择"), g));
  v->addWidget(new QLabel(QStringLiteral("选择要发送消息的机器人:"), g));
  m_robotCombo = new QComboBox(g);
  m_robotCombo->addItem(QStringLiteral("全部机器人 / 无选择"), 0);
  for (int id : {1, 2, 3, 4, 7})
    m_robotCombo->addItem(kRobotNames[id], id);
  v->addWidget(m_robotCombo);
  connect(m_robotCombo, &QComboBox::currentIndexChanged, this, [this](int) {
    const int id = m_robotCombo->currentData().toInt();
    if (id > 0) m_sim->setSelfRobotId(id);
    if (m_economyWidget) m_economyWidget->setVisible(id > 0);
  });
  auto* selRow = new QHBoxLayout;
  selRow->addWidget(new QLabel(QStringLiteral("当前选择:"), g));
  selRow->addStretch(1);
  m_selectLabel = new QLabel(g);
  m_selectLabel->setStyleSheet(QStringLiteral("font-weight:bold;color:#5b9dff;"));
  selRow->addWidget(m_selectLabel);
  v->addLayout(selRow);

  // 机器人动态数据（血量/能量/经验）与金币随选择的机器人显示。
  // 隐藏时保留占位，避免切换选择时整个界面被撑高/抖动。
  m_economyWidget = buildEconomy();
  QSizePolicy sp = m_economyWidget->sizePolicy();
  sp.setRetainSizeWhenHidden(true);
  m_economyWidget->setSizePolicy(sp);
  m_economyWidget->setVisible(false);
  v->addWidget(m_economyWidget);

  // 默认选中模拟器当前绑定的机器人，使面板与状态一致。
  for (int i = 1; i < m_robotCombo->count(); ++i) {
    if (m_robotCombo->itemData(i).toInt() == int(m_sim->selfRobotId())) {
      m_robotCombo->setCurrentIndex(i);
      break;
    }
  }

  v->addWidget(new QLabel(QStringLiteral("消息控制"), g));
  auto* msgs = new QHBoxLayout;
  m_autoButton = button(QStringLiteral("⏸ 暂停自动更新"), g);
  m_autoButton->setCheckable(true);
  auto* forceBtn = button(QStringLiteral("⚡ 强制更新状态"), g);
  msgs->addWidget(m_autoButton);
  msgs->addWidget(forceBtn);
  v->addLayout(msgs);
  connect(m_autoButton, &QPushButton::toggled, this, [this](bool on) {
    m_sim->setAutoUpdate(!on);
  });
  connect(forceBtn, &QPushButton::clicked, this, [this] { m_sim->forceUpdate(); });

  v->addWidget(new QLabel(QStringLiteral("图传发送"), g));
  auto* vid = new QHBoxLayout;
  m_videoButton = button(QStringLiteral("📹 导入视频"), g);
  vid->addWidget(m_videoButton);
  m_videoLabel = new QLabel(QStringLiteral("未发送"), g);
  m_videoLabel->setStyleSheet(QStringLiteral("color:#9aa7b4;"));
  vid->addWidget(m_videoLabel, 1);
  v->addLayout(vid);
  connect(m_videoButton, &QPushButton::clicked, this, [this] {
    if (m_sim->videoRunning()) {
      m_sim->stopVideo();
      return;
    }
    const QString file = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择图传视频"), QString(),
        QStringLiteral("视频文件 (*.mp4 *.mkv *.avi *.mov *.webm);;所有文件 (*)"));
    if (!file.isEmpty()) m_sim->importVideo(file);
  });

  return g;
}

QWidget* SimulatorWindow::buildScoreControl()
{
  auto* g = group(QStringLiteral("🏆 得分控制"), this);
  auto* v = new QVBoxLayout(g);

  auto addScoreRow = [this, g, v](const QString& team, bool red) {
    v->addWidget(new QLabel(team, g));
    auto* row = new QHBoxLayout;
    for (int delta : {10, 50, 100}) {
      auto* b = button(QStringLiteral("+%1").arg(delta), g);
      connect(b, &QPushButton::clicked, this, [this, red, delta] { m_sim->adjustScore(red, delta); });
      row->addWidget(b);
    }
    v->addLayout(row);
  };
  addScoreRow(QStringLiteral("红方得分"), true);
  addScoreRow(QStringLiteral("蓝方得分"), false);
  return g;
}

QWidget* SimulatorWindow::buildEconomy()
{
  auto* g = group(QStringLiteral("🤖 机器人状态与经济"), this);
  auto* v = new QVBoxLayout(g);

  auto valueRow = [this, g, v](const QString& name, QSpinBox*& spin, int max) {
    auto* row = new QHBoxLayout;
    row->addWidget(new QLabel(name, g));
    spin = new QSpinBox(g);
    spin->setRange(0, max);
    spin->setAlignment(Qt::AlignCenter);
    row->addWidget(spin, 1);
    v->addLayout(row);
  };
  valueRow(QStringLiteral("血量"), m_healthSpin, 1000);
  valueRow(QStringLiteral("能量"), m_energySpin, 60);
  valueRow(QStringLiteral("经验"), m_expSpin, 1000);
  connect(m_healthSpin, &QSpinBox::valueChanged, this, [this](int x) { m_sim->setHealth(x); });
  connect(m_energySpin, &QSpinBox::valueChanged, this, [this](int x) { m_sim->setEnergy(x); });
  connect(m_expSpin, &QSpinBox::valueChanged, this, [this](int x) { m_sim->setExperience(x); });

  auto* row = new QHBoxLayout;
  row->addWidget(new QLabel(QStringLiteral("金币"), g));
  m_economyLabel = new QLabel(g);
  m_economyLabel->setStyleSheet(QStringLiteral("font-weight:bold;color:#ffcc4d;"));
  m_economyLabel->setAlignment(Qt::AlignCenter);
  row->addWidget(m_economyLabel, 1);
  v->addLayout(row);
  auto* btns = new QHBoxLayout;
  for (int delta : {100, 500, 1000}) {
    auto* b = button(QStringLiteral("+%1 金币").arg(delta), g);
    connect(b, &QPushButton::clicked, this, [this, delta] { m_sim->adjustEconomy(delta); });
    btns->addWidget(b);
  }
  v->addLayout(btns);
  return g;
}

QWidget* SimulatorWindow::buildEventSender()
{
  auto* g = group(QStringLiteral("📨 事件发送"), this);
  auto* v = new QVBoxLayout(g);
  v->addWidget(new QLabel(QStringLiteral("按裁判手册分类：点击一级菜单展开具体事件"), g));

  auto* grid = new QGridLayout;
  grid->setSpacing(6);
  int slot = 0;
  auto place = [this, g, grid, &slot](const QString& text, QMenu* menu) {
    auto* b = button(text, g);
    b->setMenu(menu);
    grid->addWidget(b, slot / 3, slot % 3);
    ++slot;
  };
  auto item = [this](QMenu* menu, const QString& text, std::function<void()> fn) {
    connect(menu->addAction(text), &QAction::triggered, this, [fn] { fn(); });
  };
  auto category = [this] { return new QMenu(this); };

  // ① 基地与前哨站
  {
    auto* m = category();
    item(m, QStringLiteral("前哨站被摧毁"), [this] { m_sim->sendEvent(2); });
    item(m, QStringLiteral("基地遭攻击"), [this] { m_sim->sendEvent(11); });
    item(m, QStringLiteral("对方前哨站停转"), [this] { m_sim->sendEvent(12); });
    item(m, QStringLiteral("对方基地护甲展开"), [this] { m_sim->sendEvent(13); });
    place(QStringLiteral("🏰 基地与前哨站"), m);
  }
  // ② 能量机关（阵营 + 停止 / 激活小 / 激活大）
  {
    auto* m = category();
    m_runeRedAction = m->addAction(QStringLiteral("红方"));
    m_runeRedAction->setCheckable(true);
    m_runeBlueAction = m->addAction(QStringLiteral("蓝方"));
    m_runeBlueAction->setCheckable(true);
    m->addSeparator();
    item(m, QStringLiteral("停止激活"), [this] { m_sim->sendRune(1); });
    item(m, QStringLiteral("激活小能量机关"), [this] { activateRune(false); });
    item(m, QStringLiteral("激活大能量机关"), [this] { activateRune(true); });
    connect(m_runeRedAction, &QAction::triggered, this, [this] { m_runeTeam = 0; updateRuneTeam(); });
    connect(m_runeBlueAction, &QAction::triggered, this, [this] { m_runeTeam = 1; updateRuneTeam(); });
    place(QStringLiteral("⚡ 能量机关"), m);
  }
  // ③ 飞镖
  {
    auto* m = category();
    item(m, QStringLiteral("飞镖命中"), [this] { m_sim->sendEvent(9, QStringLiteral("1")); });
    item(m, QStringLiteral("对方飞镖闸门开启"), [this] { m_sim->sendEvent(10); });
    place(QStringLiteral("🎯 飞镖"), m);
  }
  // ④ 空中支援（联动：对方呼叫 → 己方防御·易伤减益）
  {
    auto* m = category();
    item(m, QStringLiteral("对方呼叫空中支援"), [this] {
      m_sim->sendEvent(7);
      m_sim->sendBuff(2, -30);
    });
    item(m, QStringLiteral("对方空中支援被反制"), [this] { m_sim->sendEvent(8); });
    place(QStringLiteral("🚁 空中支援"), m);
  }
  // ⑤ 机器人
  {
    auto* m = category();
    item(m, QStringLiteral("击杀"), [this] { m_sim->sendEvent(1, QStringLiteral("3,103")); });
    item(m, QStringLiteral("己方英雄狙击"), [this] { m_sim->sendEvent(5); });
    item(m, QStringLiteral("对方英雄狙击"), [this] { m_sim->sendEvent(6); });
    item(m, QStringLiteral("装配中对方请求四级"), [this] { m_sim->sendEvent(14); });
    item(m, QStringLiteral("装配结果"), [this] { m_sim->sendEvent(15); });
    place(QStringLiteral("🤖 机器人"), m);
  }
  // ⑥ 大符
  {
    auto* m = category();
    item(m, QStringLiteral("大符激活"), [this] { m_sim->sendEvent(3, QStringLiteral("3,5")); });
    place(QStringLiteral("🎰 大符"), m);
  }

  v->addLayout(grid);
  v->addStretch(1);
  updateRuneTeam();
  return g;
}

// 能量机关：小/大激活 → 同步 RuneStatusSync + Event 4(1小2大) + 关联 Buff。
void SimulatorWindow::activateRune(bool big)
{
  m_sim->sendRune(2, big ? 3 : 1, big ? 5.0f : 3.0f);
  m_sim->sendEvent(4, big ? QStringLiteral("2") : QStringLiteral("1"));
  m_sim->sendBuff(big ? 1 : 3, 30);  // 大符→攻击增益；小符→射击热量冷却
}

void SimulatorWindow::updateRuneTeam()
{
  if (m_runeRedAction) m_runeRedAction->setChecked(m_runeTeam == 0);
  if (m_runeBlueAction) m_runeBlueAction->setChecked(m_runeTeam == 1);
}

QWidget* SimulatorWindow::buildBuffControl()
{
  auto* g = group(QStringLiteral("💪 Buff 控制"), this);
  auto* v = new QVBoxLayout(g);
  v->addWidget(new QLabel(QStringLiteral("增益类型（点击发送，等级/时长按裁判手册）"), g));
  auto* grid = new QGridLayout;
  grid->setSpacing(6);
  for (int t = 1; t <= 7; ++t) {
    auto* b = button(QString::fromUtf8(kBuffNames[t]), g);
    connect(b, &QPushButton::clicked, this,
            [this, t] { m_sim->sendBuff(t, kBuffLevel[t], kBuffTime[t]); });
    grid->addWidget(b, (t - 1) / 2, (t - 1) % 2);
  }
  v->addLayout(grid);
  auto* clearAll = button(QStringLiteral("🧹 清除全部增益"), g);
  connect(clearAll, &QPushButton::clicked, this, [this] {
    for (int t = 1; t <= 7; ++t) m_sim->sendBuff(t, 0);
  });
  v->addWidget(clearAll);
  return g;
}

void SimulatorWindow::refresh()
{
  const bool connected = m_sim->isConnected();
  m_connLabel->setText(connected ? QStringLiteral("● 已连接")
                                 : QStringLiteral("● 未连接"));
  m_connLabel->setStyleSheet(connected ? QStringLiteral("color:#3ac06a;")
                                       : QStringLiteral("color:#e23a3a;"));

  m_stageLabel->setText(stageName(m_sim->stage()));
  m_roundLabel->setText(QStringLiteral("/ %1").arg(m_sim->totalRounds()));
  {
    const QSignalBlocker block(m_roundSpin);
    m_roundSpin->setValue(m_sim->round());
  }
  m_timeLabel->setText(mmss(m_sim->stageElapsedSec()));
  m_redScoreLabel->setText(QString::number(m_sim->redScore()));
  m_blueScoreLabel->setText(QString::number(m_sim->blueScore()));
  m_economyLabel->setText(QStringLiteral("余 %1").arg(m_sim->economy()));

  const int id = int(m_sim->selfRobotId());
  m_selectLabel->setText(id >= 1 && id <= 9 ? kRobotNames[id] : QStringLiteral("全部"));

  {
    const QSignalBlocker b1(m_healthSpin), b2(m_energySpin), b3(m_expSpin);
    m_healthSpin->setMaximum(qMax(1, m_sim->maxHealth()));
    m_healthSpin->setValue(int(m_sim->health()));
    m_energySpin->setValue(int(m_sim->energy()));
    m_expSpin->setValue(int(m_sim->experience()));
  }

  m_pauseButton->setText(m_sim->paused() ? QStringLiteral("▶ 继续") : QStringLiteral("⏸ 暂停"));
  {
    const QSignalBlocker block(m_autoButton);
    m_autoButton->setChecked(!m_sim->autoUpdate());
  }
  m_autoButton->setText(m_sim->autoUpdate() ? QStringLiteral("⏸ 暂停自动更新")
                                            : QStringLiteral("▶ 恢复自动更新"));

  if (m_videoButton && m_videoLabel) {
    const bool running = m_sim->videoRunning();
    m_videoButton->setText(running ? QStringLiteral("⏹ 停止图传")
                                   : QStringLiteral("📹 导入视频"));
    m_videoLabel->setText(running ? QFileInfo(m_sim->videoFile()).fileName()
                                  : QStringLiteral("未发送"));
  }
}

} // namespace rm
