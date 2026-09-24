#include "Simulator.h"
#include "MqttClient.h"
#include "VideoSender.h"
#include "robomaster.pb.h"

#include <QDebug>
#include <QStringList>

namespace rm {

namespace {

// Downlink topics published by the referee/game engine server.
const QString kTopicGameStatus = QStringLiteral("GameStatus");
const QString kTopicGlobalUnitStatus = QStringLiteral("GlobalUnitStatus");
const QString kTopicGlobalLogisticsStatus = QStringLiteral("GlobalLogisticsStatus");
const QString kTopicRobotStaticStatus = QStringLiteral("RobotStaticStatus");
const QString kTopicRobotDynamicStatus = QStringLiteral("RobotDynamicStatus");
const QString kTopicRobotModuleStatus = QStringLiteral("RobotModuleStatus");
const QString kTopicRobotRespawnStatus = QStringLiteral("RobotRespawnStatus");
const QString kTopicBuff = QStringLiteral("Buff");
const QString kTopicEvent = QStringLiteral("Event");
const QString kTopicRadar = QStringLiteral("RadarInfoToClient");
const QString kTopicPath = QStringLiteral("RobotPathPlanInfo");
const QString kTopicDart = QStringLiteral("DartSelectTargetStatusSync");
const QString kTopicSentry = QStringLiteral("SentryStatusSync");
const QString kTopicRune = QStringLiteral("RuneStatusSync");
const QString kTopicInjury = QStringLiteral("RobotInjuryStat");
const QString kTopicMechanism = QStringLiteral("GlobalSpecialMechanism");

// Uplink topics the server listens to (official V2.0.0 names).
const QStringList kUplinkTopics = {
    QStringLiteral("KeyboardMouseControl"),
    QStringLiteral("CustomControl"),
    QStringLiteral("MapClickCmd"),
    QStringLiteral("AssemblyCommand"),
    QStringLiteral("RobotPerformanceSelectionCommand"),
    QStringLiteral("CommonCommand"),
    QStringLiteral("HeroDeployModeEventCommand"),
    QStringLiteral("RuneActivateCommand"),
    QStringLiteral("DartCommand"),
    QStringLiteral("SentryCtrlCommand"),
    QStringLiteral("AirSupportCommand"),
};

// 测试钩子：RM_SIM_DEAD=1 让 clientID 对应的机器人处于阵亡/待复活状态，
// 便于在没有真实裁判系统时验证客户端的死亡界面。
bool simDead() { return qEnvironmentVariableIsSet("RM_SIM_DEAD"); }
uint32_t simSelfId()
{
  bool ok = false;
  const int v = qEnvironmentVariableIntValue("RM_CLIENT_ROBOT_ID", &ok);
  return ok ? uint32_t(v) : 1u;
}

// Our team's robots: id, type, max health, max ammo.
// 官方 GlobalUnitStatus.robot_health 顺序为 1,2,3,4,7（不含空中），此处保持一致。
struct RobotDef {
  uint32_t id;
  uint32_t type;
  uint32_t maxHealth;
  int32_t ammo;
};
const RobotDef kRobots[] = {
    {1, robomaster::TYPE_HERO, 600, 100},
    {2, robomaster::TYPE_ENGINEER, 400, 50},
    {3, robomaster::TYPE_INFANTRY, 300, 200},
    {4, robomaster::TYPE_INFANTRY, 300, 200},
    {7, robomaster::TYPE_SENTRY, 400, 300},
};

uint32_t maxHealthFor(uint32_t id) {
  for (const RobotDef& r : kRobots)
    if (r.id == id) return r.maxHealth;
  return 600;
}

QString prettyTopic(const QString& t) { return t; }

} // namespace

Simulator::Simulator(Config cfg, QObject* parent)
    : QObject(parent), m_cfg(std::move(cfg)), m_mqtt(new MqttClient(this)) {
  m_selfId = simSelfId();
  m_health = maxHealthFor(m_selfId);
  // 初始状态可由环境变量覆盖，保留 RM_SIM_STAGE / RM_SIM_RESULT / RM_SIM_PAUSED
  // 这些无 UI 时的测试钩子。
  bool stageOk = false;
  const int stage = qEnvironmentVariableIntValue("RM_SIM_STAGE", &stageOk);
  if (stageOk) m_stage = stage;
  bool resultOk = false;
  const int result = qEnvironmentVariableIntValue("RM_SIM_RESULT", &resultOk);
  if (resultOk) m_gameResult = result;
  m_paused = qEnvironmentVariableIsSet("RM_SIM_PAUSED");
  m_stageCountdownSec = stageDefaultCountdown(m_stage);
  // 测试钩子：RM_SIM_HP 直接设定初始血量（0 = 进入待复活读条）。
  bool hpOk = false;
  const int hp = qEnvironmentVariableIntValue("RM_SIM_HP", &hpOk);
  if (hpOk) {
    m_health = uint32_t(qBound(0, hp, maxHealth()));
    if (m_health == 0) {
      m_dead = true;
      startRespawn();
    }
  }

  connect(m_mqtt, &MqttClient::connected, this, [this] {
    qInfo() << "[sim] MQTT connected to" << m_cfg.mqttHost << m_cfg.mqttPort;
    for (const QString& t : kUplinkTopics) m_mqtt->subscribe(t);
    emit connectionChanged(true);
  });
  connect(m_mqtt, &MqttClient::disconnected, this, [this](const QString& cause) {
    qWarning() << "[sim] MQTT disconnected:" << cause;
    emit connectionChanged(false);
  });
  connect(m_mqtt, &MqttClient::errorOccurred, this, [](const QString& m) {
    qWarning() << "[sim]" << m;
  });
  connect(m_mqtt, &MqttClient::messageReceived, this, &Simulator::onMessage);

  m_video = new VideoSender(this);
  connect(m_video, &VideoSender::errorOccurred, this,
          [](const QString& m) { qWarning() << "[sim/video]" << m; });
  connect(m_video, &VideoSender::stopped, this, [this] { emit videoChanged(false); });
}

bool Simulator::isConnected() const { return m_mqtt && m_mqtt->isConnected(); }

int Simulator::maxHealth() const { return int(maxHealthFor(m_selfId)); }

// 各阶段默认倒计时（秒），取自裁判手册 / 官方协议注释：
// 未开始 300 / 准备 180 / 自检 15 / 倒计时 5 / 比赛 420 / 结算 0。
int Simulator::stageDefaultCountdown(int stage) {
  switch (stage) {
  case int(robomaster::STAGE_NOT_STARTED): return 300;
  case int(robomaster::STAGE_PREPARATION): return 180;
  case int(robomaster::STAGE_SELF_CHECK): return 15;
  case int(robomaster::STAGE_COUNTDOWN): return 5;
  case int(robomaster::STAGE_BATTLE): return 420;
  default: return 0;
  }
}

void Simulator::start() {
  m_mqtt->connectToBroker(m_cfg.mqttHost, m_cfg.mqttPort, m_cfg.clientId);
  m_timer.setInterval(50); // 20 Hz base tick
  m_timer.setTimerType(Qt::PreciseTimer);
  connect(&m_timer, &QTimer::timeout, this, &Simulator::tick);
  m_timer.start();
  // 测试钩子：无 UI 时直接指定图传视频。
  const QString vid = qEnvironmentVariable("RM_VIDEO_FILE");
  if (!vid.isEmpty())
    QTimer::singleShot(500, this, [this, vid] { importVideo(vid); });
}

bool Simulator::videoRunning() const {
  return m_video && m_video->isRunning();
}

QString Simulator::videoFile() const {
  return m_video ? m_video->currentFile() : QString();
}

void Simulator::importVideo(const QString& file) {
  const QString target = qEnvironmentVariable("RM_VIDEO_TARGET", QStringLiteral("127.0.0.1"));
  const bool ok = m_video->start(file, target, 3334);
  emit videoChanged(ok);
  if (ok)
    qInfo() << "[sim/video] 发送图传" << file << "->" << target << 3334;
}

void Simulator::stopVideo() {
  m_video->stop();
  emit videoChanged(false);
}

void Simulator::tick() {
  ++m_tick;

  // 暂停自动更新时只响应 UI 的手动操作（setter 会立即发布），不再自动推进。
  if (!m_autoUpdate) return;

  // 5 Hz
  if (m_tick % 4 == 0) publishGameStatus();

  // 10 Hz
  if (m_tick % 2 == 0) publishRobotDynamicStatus();

  // 1 Hz
  if (m_tick % 20 == 0) {
    ++m_elapsedSec;
    // 比赛时钟严格按手册推进：暂停或未开始时冻结；各阶段倒计时归零后自动进入下一阶段。
    if (!m_paused && m_stage != robomaster::STAGE_NOT_STARTED) {
      ++m_stageElapsedSec;
      if (m_stageCountdownSec > 0) --m_stageCountdownSec;
      if (m_stageCountdownSec <= 0 && m_stage >= int(robomaster::STAGE_PREPARATION) &&
          m_stage < int(robomaster::STAGE_SETTLEMENT))
        setStage(m_stage + 1);
    }
    // 周期性广播比赛状态：数值只由 UI 设置，不再随时间伪造（避免"自动模拟"）。
    // 唯一的时间推进是待复活读条：死亡后在比赛阶段每秒 +1，直到可免费复活。
    if (isDead() && m_stage == int(robomaster::STAGE_BATTLE) && m_respawnTotal <= 0)
      startRespawn();
    if (isDead() && m_stage == int(robomaster::STAGE_BATTLE) && !m_paused && m_respawnTotal > 0)
      m_respawnProgress = qMin(m_respawnProgress + 1, m_respawnTotal);
    publishGlobalUnitStatus();
    publishGlobalLogisticsStatus();
    publishRobotStaticStatus();
    publishRobotModuleStatus();
    publishRobotRespawnStatus();
    publishRadar();
    publishPath();
    publishSelfPosition();
    publishDart();
    publishSentry();
    publishRune();
    publishDeploy();
    publishAssembly();
    publishAirSupport();
    publishPerf();
    publishInjuryStat();
    publishMechanism();
  }

  emit stateChanged();
}

void Simulator::setStage(int stage) {
  m_stage = stage;
  m_stageElapsedSec = 0;
  m_stageCountdownSec = stageDefaultCountdown(stage);
  if (stage == int(robomaster::STAGE_SETTLEMENT)) m_paused = false;
  // 复活读条只在比赛阶段推进：进入比赛时启动，离开时暂停。
  if (m_dead) {
    if (stage == int(robomaster::STAGE_BATTLE) && m_respawnTotal <= 0)
      startRespawn();
    else if (stage != int(robomaster::STAGE_BATTLE))
      m_respawnTotal = 0, m_respawnProgress = 0;
  }
  publishGameStatus();
  publishRobotRespawnStatus();
  emit stateChanged();
}

void Simulator::setRound(int round) {
  m_round = qBound(1, round, m_totalRounds);
  publishGameStatus();
  emit stateChanged();
}

void Simulator::setPaused(bool paused) {
  m_paused = paused;
  publishGameStatus();
  emit stateChanged();
}

void Simulator::adjustScore(bool red, int delta) {
  if (red)
    m_redScore = qMax(0, m_redScore + delta);
  else
    m_blueScore = qMax(0, m_blueScore + delta);
  publishGameStatus();
  emit stateChanged();
}

void Simulator::adjustEconomy(int delta) {
  m_economy = uint32_t(qMax(0, int(m_economy) + delta));
  publishGlobalLogisticsStatus();
  emit stateChanged();
}

void Simulator::sendEvent(int eventId, const QString& param) {
  robomaster::Event msg;
  msg.set_event_id(uint32_t(eventId));
  msg.set_param(param.toStdString());
  publish(kTopicEvent, msg.SerializeAsString());
}

void Simulator::sendBuff(int type, int level, int durationSec) {
  publishBuffNow(uint32_t(type), level, durationSec);
}

void Simulator::sendRune(int status, int arms, float rings) {
  m_runeStatus = status;
  m_runeArms = arms;
  m_runeRings = rings;
  publishRune();
  emit stateChanged();
}

void Simulator::setAutoUpdate(bool on) {
  m_autoUpdate = on;
  emit stateChanged();
}

void Simulator::resetAll() {
  m_elapsedSec = 0;
  m_tick = 0;
  m_redScore = 0;
  m_blueScore = 0;
  m_economy = 400;
  m_stage = int(robomaster::STAGE_NOT_STARTED);
  m_round = 1;
  m_stageElapsedSec = 0;
  m_stageCountdownSec = stageDefaultCountdown(m_stage);
  m_paused = false;
  m_dead = false;
  m_respawnProgress = 0;
  m_respawnTotal = 0;
  m_immediateExchanges = 0;
  m_health = maxHealthFor(m_selfId);
  m_ammo = 100;
  m_deployMode = 0;
  m_dartTarget = 1;
  m_dartOpen = 0;
  m_airStatus = 0;
  m_airShooter = 1;
  m_techCoreBasic = 0;
  m_perfShooter = 0;
  m_perfChassis = 0;
  m_perfSentry = 0;
  m_sentryPosture = 1;
  publishGameStatus();
  publishGlobalUnitStatus();
  publishGlobalLogisticsStatus();
  publishRobotDynamicStatus();
  publishDeploy();
  publishAssembly();
  publishAirSupport();
  publishPerf();
  publishDart();
  publishSentry();
  publishRobotRespawnStatus();
  emit stateChanged();
}

void Simulator::forceUpdate() {
  publishGameStatus();
  publishGlobalUnitStatus();
  publishGlobalLogisticsStatus();
  publishRobotStaticStatus();
  publishRobotDynamicStatus();
  publishRobotModuleStatus();
  publishRobotRespawnStatus();
  publishRadar();
  publishPath();
  publishSelfPosition();
  publishDart();
  publishSentry();
  publishRune();
  publishDeploy();
  publishAssembly();
  publishAirSupport();
  publishPerf();
  publishInjuryStat();
  publishMechanism();
}

void Simulator::setSelfRobotId(int id) {
  const uint32_t nid = uint32_t(id);
  if (nid == m_selfId) return; // 未变化时不要重置血量/复活状态
  m_selfId = nid;
  m_health = maxHealthFor(m_selfId);
  m_dead = false;
  m_respawnProgress = 0;
  m_respawnTotal = 0;
  publishRobotStaticStatus();
  publishRobotDynamicStatus();
  publishRobotRespawnStatus();
  emit stateChanged();
}

void Simulator::setHealth(int hp) {
  const int bounded = qBound(0, hp, maxHealth());
  m_health = uint32_t(bounded);
  if (bounded <= 0 && !m_dead) {
    m_dead = true;
    clearBuffs(); // 阵亡即失去全部增益
    startRespawn();
  } else if (bounded > 0 && m_dead) {
    m_dead = false;
    m_respawnProgress = 0;
    m_respawnTotal = 0;
  }
  publishRobotDynamicStatus();
  publishRobotRespawnStatus();
  publishRobotStaticStatus();
  emit stateChanged();
}

void Simulator::setEnergy(int energy) {
  m_chassisEnergy = uint32_t(qBound(0, energy, 60));
  publishRobotDynamicStatus();
  emit stateChanged();
}

void Simulator::setExperience(int exp) {
  m_experience = uint32_t(qMax(0, exp));
  publishRobotDynamicStatus();
  emit stateChanged();
}

bool Simulator::isDead() const { return m_dead || simDead(); }

// 按裁判手册：复活读条总时长随比赛进行时间与买活次数增加，
// total = round(10 + (420 - 剩余秒数)/10 + 20×买活次数)，仅在比赛阶段启动。
void Simulator::startRespawn() {
  if (m_stage != int(robomaster::STAGE_BATTLE)) {
    m_respawnProgress = 0;
    m_respawnTotal = 0;
    return;
  }
  const int remaining = qBound(0, m_stageCountdownSec, 420);
  m_respawnTotal = qMax(1, int(qRound(10.0 + (420 - remaining) / 10.0 +
                                       20.0 * m_immediateExchanges)));
  m_respawnProgress = 0;
}

void Simulator::publish(const QString& topic, const std::string& bytes) {
  m_mqtt->publish(
      topic, QByteArray(bytes.data(), static_cast<int>(bytes.size())));
}

void Simulator::publishGameStatus() {
  robomaster::GameStatus msg;
  msg.set_current_round(m_round);
  msg.set_total_rounds(m_totalRounds);
  msg.set_red_score(static_cast<uint32_t>(m_redScore));
  msg.set_blue_score(static_cast<uint32_t>(m_blueScore));
  msg.set_current_stage(m_stage);
  msg.set_stage_elapsed_sec(m_stageElapsedSec);
  msg.set_stage_countdown_sec(qMax(0, m_stageCountdownSec));
  msg.set_is_paused(m_paused);
  if (m_stage == robomaster::STAGE_SETTLEMENT) {
    msg.set_game_result(m_gameResult);
    msg.set_end_reason(m_endReason);
  }
  publish(kTopicGameStatus, msg.SerializeAsString());
}

void Simulator::publishGlobalUnitStatus() {
  robomaster::GlobalUnitStatus msg;
  msg.set_base_health(5000);
  msg.set_base_status(2);
  msg.set_base_shield(0);
  msg.set_outpost_health(1500);
  msg.set_outpost_status(1);
  msg.set_enemy_base_health(5000);
  msg.set_enemy_base_status(2);
  msg.set_enemy_base_shield(0);
  msg.set_enemy_outpost_health(1500);
  msg.set_enemy_outpost_status(1);
  for (const RobotDef& r : kRobots) {
    msg.add_robot_health(r.maxHealth);
    msg.add_robot_bullets(uint32_t(r.ammo));
  }
  // 对方 5 台（顺序同己方 1,2,3,4,7）。
  for (const RobotDef& r : kRobots) {
    msg.add_robot_health(r.maxHealth);
    msg.add_robot_bullets(uint32_t(r.ammo));
  }
  msg.set_total_damage_ally(0);
  msg.set_total_damage_enemy(0);
  publish(kTopicGlobalUnitStatus, msg.SerializeAsString());
}

void Simulator::publishGlobalLogisticsStatus() {
  robomaster::GlobalLogisticsStatus msg;
  msg.set_remaining_economy(m_economy);
  msg.set_total_economy_obtained(0);
  msg.set_tech_level(1);
  msg.set_encryption_level(0);
  publish(kTopicGlobalLogisticsStatus, msg.SerializeAsString());
}

void Simulator::publishRobotStaticStatus() {
  for (const RobotDef& r : kRobots) {
    robomaster::RobotStaticStatus msg;
    msg.set_connection_state(1);
    msg.set_field_state(0);
    msg.set_alive_state((isDead() && r.id == m_selfId) ? 2 : 1);
    msg.set_robot_id(r.id);
    msg.set_robot_type(r.type);
    msg.set_performance_system_shooter(r.id == 1 ? 3 : 1);
    msg.set_performance_system_chassis(r.id == 1 ? 3 : 1);
    msg.set_level(1);
    msg.set_max_health(r.maxHealth);
    msg.set_max_heat(r.id == 1 ? 240 : 120);
    msg.set_heat_cooldown_rate(10.0f);
    msg.set_max_power(80);
    msg.set_max_buffer_energy(60);
    msg.set_max_chassis_energy(60);
    publish(kTopicRobotStaticStatus, msg.SerializeAsString());
  }
}

void Simulator::publishRobotDynamicStatus() {
  // No robot_id field: this is the client-bound robot's own live data.
  // 数值全部来自 UI，不再随时间伪造。
  robomaster::RobotDynamicStatus msg;
  msg.set_current_health(m_health);
  msg.set_current_heat(0.0f);
  msg.set_last_projectile_fire_rate(18.0f);
  msg.set_current_chassis_energy(m_chassisEnergy);
  msg.set_current_buffer_energy(m_chassisEnergy);
  msg.set_current_experience(m_experience);
  msg.set_experience_for_upgrade(200);
  msg.set_total_projectiles_fired(0);
  msg.set_remaining_ammo(m_ammo);
  msg.set_is_out_of_combat(false);
  msg.set_out_of_combat_countdown(0);
  msg.set_can_remote_heal(true);
  msg.set_can_remote_ammo(true);
  publish(kTopicRobotDynamicStatus, msg.SerializeAsString());
}

void Simulator::publishRobotModuleStatus() {
  robomaster::RobotModuleStatus msg;
  msg.set_power_manager(1);
  msg.set_rfid(1);
  msg.set_light_strip(1);
  msg.set_small_shooter(1);
  msg.set_big_shooter(1);
  msg.set_uwb(1);
  msg.set_armor(1);
  msg.set_video_transmission(1);
  msg.set_capacitor(1);
  msg.set_main_controller(1);
  msg.set_laser_detection_module(0);
  publish(kTopicRobotModuleStatus, msg.SerializeAsString());
}

void Simulator::publishRobotRespawnStatus() {
  robomaster::RobotRespawnStatus msg;
  const bool dead = isDead();
  const bool pending = dead && m_respawnTotal > 0;
  const uint32_t total = pending ? uint32_t(m_respawnTotal) : 0;
  const uint32_t current = pending ? uint32_t(m_respawnProgress) : 0;
  msg.set_is_pending_respawn(pending);
  msg.set_total_respawn_progress(total);
  msg.set_current_respawn_progress(current);
  // 免费复活要等读条走完；花费金币可随时跳过等待。
  msg.set_can_free_respawn(pending && current >= total);
  msg.set_gold_cost_for_respawn(100);
  msg.set_can_pay_for_respawn(dead);
  publish(kTopicRobotRespawnStatus, msg.SerializeAsString());
}

// 手动发送一条增益（level<=0 视为清除），durationSec 为手册规定的持续时间。
void Simulator::publishBuffNow(uint32_t type, int32_t level, int durationSec) {
  robomaster::Buff msg;
  msg.set_robot_id(m_selfId);
  msg.set_buff_type(type);
  msg.set_buff_level(level);
  msg.set_buff_max_time(level > 0 ? durationSec : 0);
  msg.set_buff_left_time(level > 0 ? durationSec : 0);
  publish(kTopicBuff, msg.SerializeAsString());
}

// 阵亡清除全部增益：1..7 型均发 level=0（left_time=0 会被客户端移除）。
void Simulator::clearBuffs() {
  for (uint32_t t = 1; t <= 7; ++t) publishBuffNow(t, 0, 0);
}

// 雷达目标位置（cm）：顺序为对方 1,2,3,4,6,7 后己方 1,2,3,4,6,7。
// 场地 2800×1500 cm；位置固定，便于验证小地图，不随时间漂移。
void Simulator::publishRadar() {
  robomaster::RadarInfoToClient msg;
  const int baseX[6] = {600, 1000, 1400, 1800, 2200, 1000};
  const int baseY[6] = {300, 700, 1100, 400, 800, 1250};
  for (int i = 0; i < 12; ++i) {
    const bool enemy = i < 6;
    const int k = i % 6;
    const int x = enemy ? 2800 - baseX[k] : baseX[k];
    const int y = baseY[k];
    auto* u = msg.add_robot_info();
    u->set_target_pos_x(static_cast<uint32_t>(qBound(0, x, 2800)));
    u->set_target_pos_y(static_cast<uint32_t>(qBound(0, y, 1500)));
    u->set_is_high_light(i % 4 == 0 ? 1 : 0);
  }
  publish(kTopicRadar, msg.SerializeAsString());
}

// 哨兵路径（单位 dm）：start_pos 与 offset 均为分米，小地图左下角为原点；10 段折线，保持在场内。
void Simulator::publishPath() {
  robomaster::RobotPathPlanInfo msg;
  msg.set_intention(3); // 移动
  msg.set_start_pos_x(30);
  msg.set_start_pos_y(50);
  msg.set_sender_id(7);
  for (int i = 0; i < 10; ++i) {
    msg.add_offset_x(i % 2 == 0 ? 30 : 0);
    msg.add_offset_y(i % 2 == 0 ? 0 : 20);
  }
  publish(kTopicPath, msg.SerializeAsString());
}

// 本车世界坐标与朝向（RobotPosition，1Hz，单位 m；0x0201 坐标系，正北 0°）。
// 位置固定便于验证小地图本车点，朝向按阵营朝向对方（红 +X=90°，蓝 -X=270°）。
void Simulator::publishSelfPosition() {
  robomaster::RobotPosition msg;
  const bool blue = m_selfId >= 100;
  msg.set_x(blue ? 24.0f : 4.0f);
  msg.set_y(7.5f);
  msg.set_z(0.0f);
  msg.set_yaw(blue ? 270.0f : 90.0f);
  msg.set_robot_id(m_selfId);
  publish(QStringLiteral("RobotPosition"), msg.SerializeAsString());
}

// 飞镖目标选择状态：由 DartCommand 指令改变。
void Simulator::publishDart() {
  robomaster::DartSelectTargetStatusSync msg;
  msg.set_target_id(uint32_t(m_dartTarget));
  msg.set_open(uint32_t(m_dartOpen));
  publish(kTopicDart, msg.SerializeAsString());
}

// 哨兵姿态：由 SentryCtrlCommand 改变。
void Simulator::publishSentry() {
  robomaster::SentryStatusSync msg;
  msg.set_posture_id(uint32_t(m_sentryPosture));
  msg.set_is_weakened(false);
  msg.set_is_powered(false);
  publish(kTopicSentry, msg.SerializeAsString());
}

// 能量机关状态（RuneStatusSync）：1=未激活 2=正在激活 3=已激活。
void Simulator::publishRune() {
  robomaster::RuneStatusSync msg;
  msg.set_rune_status(uint32_t(m_runeStatus));
  msg.set_activated_arms(uint32_t(m_runeArms));
  msg.set_average_rings(m_runeRings);
  publish(kTopicRune, msg.SerializeAsString());
}

// 本车受伤统计：默认全 0，由 UI/事件驱动。
void Simulator::publishInjuryStat() {
  robomaster::RobotInjuryStat msg;
  msg.set_total_damage(0);
  msg.set_collision_damage(0);
  msg.set_small_projectile_damage(0);
  msg.set_large_projectile_damage(0);
  msg.set_dart_splash_damage(0);
  msg.set_module_offline_damage(0);
  msg.set_offline_damage(0);
  msg.set_penalty_damage(0);
  msg.set_server_kill_damage(0);
  msg.set_killer_id(0);
  publish(kTopicInjury, msg.SerializeAsString());
}

// 堡垒占点：无自动占点，发布空状态（清掉旧计时）。
void Simulator::publishMechanism() {
  robomaster::GlobalSpecialMechanism msg;
  publish(kTopicMechanism, msg.SerializeAsString());
}

// 部署模式回馈（HeroDeployModeEventCommand 的结果）。
void Simulator::publishDeploy() {
  robomaster::DeployModeStatusSync msg;
  msg.set_status(uint32_t(m_deployMode));
  publish(QStringLiteral("DeployModeStatusSync"), msg.SerializeAsString());
}

// 装配进度回馈（AssemblyCommand 的结果）。
void Simulator::publishAssembly() {
  robomaster::TechCoreMotionStateSync msg;
  msg.set_maximum_difficulty_level(4);
  msg.set_basic_state(uint32_t(m_techCoreBasic));
  if (m_techCoreBasic == 2) {
    msg.set_putin_state(1);
    msg.set_move_state(1);
    msg.set_rotate_state(1);
    msg.set_remain_time_all(9);
    msg.set_remain_time_step(3);
  }
  publish(QStringLiteral("TechCoreMotionStateSync"), msg.SerializeAsString());
}

// 空中支援状态回馈（AirSupportCommand 的结果）。
void Simulator::publishAirSupport() {
  robomaster::AirSupportStatusSync msg;
  msg.set_airsupport_status(uint32_t(m_airStatus));
  msg.set_left_time(m_airStatus ? 30 : 0);
  msg.set_cost_coins(200);
  msg.set_is_being_targeted(0);
  msg.set_shooter_status(uint32_t(m_airShooter));
  publish(QStringLiteral("AirSupportStatusSync"), msg.SerializeAsString());
}

// 性能选择回馈（RobotPerformanceSelectionCommand 的结果）。
void Simulator::publishPerf() {
  robomaster::RobotPerformanceSelectionSync msg;
  msg.set_shooter(uint32_t(m_perfShooter));
  msg.set_chassis(uint32_t(m_perfChassis));
  msg.set_sentry_control(uint32_t(m_perfSentry));
  publish(QStringLiteral("RobotPerformanceSelectionSync"), msg.SerializeAsString());
}

// 地图标记广播（MapClickCmd 的结果；客户端据此画出指令点位）。
void Simulator::publishMapClick(uint32_t mode, float mapX, float mapY) {
  robomaster::MapClickInfo msg;
  msg.set_is_send_all(1);
  msg.set_robot_id(std::string(7, '\0'));
  msg.set_mode(mode);
  msg.set_enemy_id(0);
  msg.set_ascii(0);
  msg.set_type(1);
  msg.set_map_x(mapX);
  msg.set_map_y(mapY);
  publish(QStringLiteral("MapClickInfo"), msg.SerializeAsString());
}

// 哨兵控制结果回馈。
void Simulator::publishSentryCtrlResult(uint32_t commandId, uint32_t resultCode) {
  robomaster::SentryCtrlResult msg;
  msg.set_command_id(commandId);
  msg.set_result_code(resultCode);
  publish(QStringLiteral("SentryCtrlResult"), msg.SerializeAsString());
}

void Simulator::onMessage(const QString& topic, const QByteArray& payload) {
  if (topic == QStringLiteral("KeyboardMouseControl")) {
    robomaster::KeyboardMouseControl m;
    if (m.ParseFromArray(payload.constData(), payload.size())) {
      qInfo().nospace() << "[sim] <- " << topic << " mouse=(" << m.mouse_x()
                        << "," << m.mouse_y() << "," << m.mouse_z() << ")"
                        << " buttons=(" << m.left_button_down() << ","
                        << m.right_button_down() << "," << m.mid_button_down()
                        << ") keys=0x" << Qt::hex << m.keyboard_value();
      return;
    }
  } else if (topic == QStringLiteral("CommonCommand")) {
    robomaster::CommonCommand m;
    if (m.ParseFromArray(payload.constData(), payload.size())) {
      qInfo() << "[sim] <- CommonCommand cmd_type=" << m.cmd_type()
              << "param=" << m.param();
      switch (m.cmd_type()) {
      case 1: // 兑换 17mm（参数须为 10 倍数）
      case 2: // 兑换 42mm
      case 5: // 远程兑换允许发弹量
        m_ammo += m.param();
        publishRobotDynamicStatus();
        break;
      case 6: // 远程回血
        setHealth(maxHealth());
        break;
      case 4: // 兑立即复活（买活）：累计次数抬高后续复活读条总进度
        ++m_immediateExchanges;
        setHealth(maxHealth());
        break;
      case 3: // 确认复活
        setHealth(maxHealth());
        break;
      default: break;
      }
      return;
    }
  } else if (topic == QStringLiteral("AssemblyCommand")) {
    robomaster::AssemblyCommand m;
    if (m.ParseFromArray(payload.constData(), payload.size())) {
      // 1=确认装配（进入运动中）；0/2=开始/取消（清空装配进度）。
      m_techCoreBasic = m.operation() == 1 ? 2 : 0;
      if (m.operation() == 1) m_techCoreDifficulty = int(m.difficulty());
      publishAssembly();
      return;
    }
  } else if (topic == QStringLiteral("HeroDeployModeEventCommand")) {
    robomaster::HeroDeployModeEventCommand m;
    if (m.ParseFromArray(payload.constData(), payload.size())) {
      m_deployMode = m.mode() == 1 ? 1 : 0;
      publishDeploy();
      return;
    }
  } else if (topic == QStringLiteral("RobotPerformanceSelectionCommand")) {
    robomaster::RobotPerformanceSelectionCommand m;
    if (m.ParseFromArray(payload.constData(), payload.size())) {
      if (m.shooter()) m_perfShooter = int(m.shooter());
      if (m.chassis()) m_perfChassis = int(m.chassis());
      if (m.has_sentry_control()) m_perfSentry = int(m.sentry_control());
      publishPerf();
      return;
    }
  } else if (topic == QStringLiteral("DartCommand")) {
    robomaster::DartCommand m;
    if (m.ParseFromArray(payload.constData(), payload.size())) {
      if (m.target_id()) m_dartTarget = int(m.target_id());
      m_dartOpen = m.open() ? 1 : 0;
      publishDart();
      if (m.launch_confirm()) sendEvent(9, QStringLiteral("1")); // 飞镖命中事件
      return;
    }
  } else if (topic == QStringLiteral("AirSupportCommand")) {
    robomaster::AirSupportCommand m;
    if (m.ParseFromArray(payload.constData(), payload.size())) {
      switch (m.command_id()) {
      case 0: m_airStatus = 0; break;                 // 取消
      case 1: case 2: m_airStatus = 1; break;         // 免费 / 付费呼叫
      case 3: m_airShooter = 1; break;                // 解除反制
      default: break;
      }
      publishAirSupport();
      return;
    }
  } else if (topic == QStringLiteral("RuneActivateCommand")) {
    robomaster::RuneActivateCommand m;
    if (m.ParseFromArray(payload.constData(), payload.size()) && m.activate()) {
      m_runeStatus = 3; // 已激活
      m_runeArms = 3;
      m_runeRings = 5.0f;
      publishRune();
      return;
    }
  } else if (topic == QStringLiteral("SentryCtrlCommand")) {
    robomaster::SentryCtrlCommand m;
    if (m.ParseFromArray(payload.constData(), payload.size())) {
      // 7/10=进攻 8/11=防御 9/12=移动。
      switch (m.command_id()) {
      case 7: case 10: m_sentryPosture = 1; break;
      case 8: case 11: m_sentryPosture = 2; break;
      case 9: case 12: m_sentryPosture = 3; break;
      case 5: case 6: setHealth(maxHealth()); break; // 确认/买活复活
      default: break;
      }
      publishSentry();
      publishSentryCtrlResult(m.command_id(), 0); // 0=成功
      return;
    }
  } else if (topic == QStringLiteral("MapClickCmd")) {
    robomaster::MapClickCmd m;
    if (m.ParseFromArray(payload.constData(), payload.size())) {
      publishMapClick(m.mode() ? m.mode() : 1, m.map_x(), m.map_y());
      return;
    }
  }
  qInfo() << "[sim] <-" << prettyTopic(topic) << payload.size() << "bytes";
}

} // namespace rm
