#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

#include <string>

namespace rm {

class MqttClient;
class VideoSender;

/// Stands in for the referee system / game engine server.
/// Publishes downlink match data (Protobuf over MQTT) and logs uplink commands.
/// Only the MQTT side is implemented for now; UDP video comes later.
class Simulator : public QObject {
  Q_OBJECT
public:
  struct Config {
    QString mqttHost = QStringLiteral("127.0.0.1");
    quint16 mqttPort = 1883;
    QString clientId = QStringLiteral("rm_simulator");
  };

  explicit Simulator(Config cfg, QObject* parent = nullptr);
  void start();

  bool isConnected() const;

  int stage() const { return m_stage; }
  int round() const { return m_round; }
  int totalRounds() const { return m_totalRounds; }
  int stageElapsedSec() const { return m_stageElapsedSec; }
  uint32_t economy() const { return m_economy; }
  int redScore() const { return m_redScore; }
  int blueScore() const { return m_blueScore; }
  bool autoUpdate() const { return m_autoUpdate; }
  bool paused() const { return m_paused; }
  uint32_t selfRobotId() const { return m_selfId; }
  int runeStatus() const { return m_runeStatus; }
  int maxHealth() const;
  uint32_t health() const { return m_health; }
  uint32_t energy() const { return m_chassisEnergy; }
  uint32_t experience() const { return m_experience; }
  bool dead() const { return m_dead; }
  bool videoRunning() const;
  QString videoFile() const;

public slots:
  void setStage(int stage);
  void setRound(int round);
  void setPaused(bool paused);
  void adjustScore(bool red, int delta);
  void adjustEconomy(int delta);
  void sendEvent(int eventId, const QString& param = QString());
  void sendBuff(int type, int level, int durationSec = 30);
  // status: 1=未激活 2=正在激活 3=已激活（能量机关）。
  void sendRune(int status, int arms = 0, float rings = 0.0f);
  void setAutoUpdate(bool on);
  void resetAll();
  void forceUpdate();
  void setSelfRobotId(int id);
  void setHealth(int hp);
  void setEnergy(int energy);
  void setExperience(int exp);
  // 导入本地视频作为 UDP 图传源（调用 ffmpeg 转 HEVC，发往 3334）。
  void importVideo(const QString& file);
  void stopVideo();

signals:
  void connectionChanged(bool connected);
  void stateChanged();
  void videoChanged(bool running);

private slots:
  void tick();
  void onMessage(const QString& topic, const QByteArray& payload);

private:
  void publish(const QString& topic, const std::string& bytes);
  void publishGameStatus();
  void publishGlobalUnitStatus();
  void publishGlobalLogisticsStatus();
  void publishRobotStaticStatus();
  void publishRobotDynamicStatus();
  void publishRobotModuleStatus();
  void publishRobotRespawnStatus();
  void publishRadar();
  void publishPath();
  void publishSelfPosition();
  void publishDart();
  void publishSentry();
  void publishRune();
  void publishInjuryStat();
  void publishMechanism();
  void publishBuffNow(uint32_t type, int32_t level, int durationSec);
  // 上行指令对应的下行回馈（客户端做这些指令时能看到状态变化）。
  void publishDeploy();
  void publishAssembly();
  void publishAirSupport();
  void publishPerf();
  void publishMapClick(uint32_t mode, float mapX, float mapY);
  void publishSentryCtrlResult(uint32_t commandId, uint32_t resultCode);

  // 血量归零 -> 进入待复活：按手册公式计算读条总进度，仅在比赛阶段推进。
  bool isDead() const;
  void startRespawn();
  // 阵亡后清除该机器人全部增益（1..7 型清零）。
  void clearBuffs();

  // 各阶段的默认倒计时（秒），严格按裁判手册：准备 180 / 自检 15 / 倒计时 5 / 比赛 420。
  static int stageDefaultCountdown(int stage);

  Config m_cfg;
  MqttClient* m_mqtt = nullptr;
  VideoSender* m_video = nullptr;
  QTimer m_timer;      // 20 Hz base tick
  int m_tick = 0;
  int m_elapsedSec = 0;
  int m_redScore = 0;
  int m_blueScore = 0;

  uint32_t m_selfId = 1;
  uint32_t m_economy = 400;
  int m_stage = 4;            // robomaster::STAGE_BATTLE
  int m_round = 1;
  int m_totalRounds = 3;
  int m_stageElapsedSec = 0;
  int m_stageCountdownSec = 420;
  bool m_paused = false;
  bool m_autoUpdate = true;
  int m_gameResult = 255;
  int m_endReason = 1;
  int m_runeStatus = 1;       // 1=未激活 2=正在激活 3=已激活
  int m_runeArms = 0;
  float m_runeRings = 0.0f;

  // 当前选择机器人的动态数据（仅由 UI 设置，不随时间伪造）。
  uint32_t m_health = 600;
  uint32_t m_chassisEnergy = 50;
  uint32_t m_experience = 100;
  uint32_t m_ammo = 100;

  // 上行指令驱动的状态回馈（部署/装配/飞镖/空中支援/性能选择等）。
  int m_deployMode = 0;          // 0=未部署 1=已部署
  int m_dartTarget = 1;          // 1..5
  int m_dartOpen = 0;            // 0=关闭 1=开启中 2=已开启
  int m_airStatus = 0;           // 0=未进行 1=正在
  int m_airShooter = 1;          // 0=锁定不可解 1=正常 2=锁定可解
  int m_techCoreBasic = 0;       // 0=无 1=初始 2=运动中 3=已到达
  int m_techCoreDifficulty = 1;  // 装配难度
  int m_perfShooter = 0;         // 1=冷却 2=爆发 3=英雄近战 4=英雄远程
  int m_perfChassis = 0;         // 1=血量 2=功率 3=英雄近战 4=英雄远程
  int m_perfSentry = 0;          // 0=自动 1=半自动
  int m_sentryPosture = 1;       // 1=进攻 2=防御 3=移动

  // 待复活读条：血量归零后 pending，进度按秒推进到 total 后允许免费复活。
  bool m_dead = false;
  int m_respawnProgress = 0;
  int m_respawnTotal = 0;
  int m_immediateExchanges = 0; // 本局买活次数，影响读条总进度
};

} // namespace rm
