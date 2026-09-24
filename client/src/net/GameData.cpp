#include "net/GameData.h"

namespace rm {
namespace {

// 己方地面机器人展示槽位：id + 名称 + 在 GlobalUnitStatus.robot_health 中的下标。
struct AllySlot {
    quint32 id;
    int protoIndex;
    const char* name;
};
const AllySlot kAllySlots[4] = {
    {1, 0, "重装"},
    {3, 2, "步兵3"},
    {4, 3, "步兵4"},
    {7, 4, "哨兵"},
};

// 机器人 id 可能是队内编号（1-9）或绝对编号（红 1-9 / 蓝 101-109）。
quint32 teamRelative(quint32 id) { return id > 100 ? id - 100 : id; }

// 槽位编号 -> 兵种名。
QString robotTypeName(quint32 id)
{
    switch (teamRelative(id)) {
    case 1: case 2: return QObject::tr("重装");
    case 3: case 4: case 5: return QObject::tr("步兵");
    case 6: return QObject::tr("无人机");
    case 7: return QObject::tr("哨兵");
    case 8: return QObject::tr("飞镖");
    case 9: return QObject::tr("雷达");
    default: return QObject::tr("通用");
    }
}

// 绝对编号 -> 阵营名（红 1-9 / 蓝 101-109）。
QString sideName(quint32 id) { return id > 100 ? QObject::tr("蓝方") : QObject::tr("红方"); }

// 阵营色（与 OverlayHud 的 kRed / kBlue 一致）。
QString sideColor(quint32 id)
{
    return id > 100 ? QStringLiteral("#3A7BE2") : QStringLiteral("#E23A3A");
}

QString robotSideLabel(quint32 id) { return sideName(id) + robotTypeName(id); }

// 击杀事件的富文本：击杀方/被击杀方按各自阵营色着色。
QString killRichText(const QString& param)
{
    const QStringList ids = param.split(QLatin1Char(','));
    if (ids.size() != 2)
        return QString();
    auto span = [](quint32 id) {
        return QStringLiteral("<span style=\"color:%1\">%2</span>")
            .arg(sideColor(id), robotSideLabel(id));
    };
    return QObject::tr("击杀事件（%1 → %2）").arg(span(ids[0].toUInt()), span(ids[1].toUInt()));
}

constexpr qint64 kBannerMs = 4000;
constexpr qint64 kMapMarkerMs = 10000;

template <typename Msg>
bool parseInto(std::optional<Msg>& slot, const QByteArray& payload)
{
    Msg m;
    if (!m.ParseFromArray(payload.constData(), payload.size()))
        return false;
    slot = std::move(m);
    return true;
}

QString eventText(int id, const QString& param)
{
    switch (id) {
    case 1: {
        const QStringList ids = param.split(QLatin1Char(','));
        if (ids.size() == 2)
            return QObject::tr("击杀事件（%1 → %2）")
                .arg(robotSideLabel(ids[0].toUInt()), robotSideLabel(ids[1].toUInt()));
        return QObject::tr("击杀事件（%1）").arg(param);
    }
    case 2: return QObject::tr("前哨站被摧毁（id %1）").arg(param);
    case 3: return QObject::tr("大能量机关激活（臂数,平均环数 %1）").arg(param);
    case 4: return QObject::tr("能量机关激活（%1）").arg(param);
    case 5: return QObject::tr("己方重装狙击伤害 %1").arg(param);
    case 6: return QObject::tr("对方重装狙击伤害 %1").arg(param);
    case 7: return QObject::tr("对方呼叫空中支援");
    case 8: return QObject::tr("对方空中支援被反制（剩余 %1 次）").arg(param);
    case 9: return QObject::tr("飞镖命中（%1）").arg(param);
    case 10: return QObject::tr("对方飞镖闸门开启");
    case 11: return QObject::tr("基地遭攻击");
    case 12: return QObject::tr("对方前哨站停转");
    case 13: return QObject::tr("对方基地护甲展开");
    case 14: return QObject::tr("装配中对方请求四级（强制退出缓冲期）");
    case 15: return QObject::tr("装配结果（%1）").arg(param);
    default: return QObject::tr("事件 %1（%2）").arg(id).arg(param);
    }
}

QString penaltyText(int type)
{
    static const char* const kTexts[] = {"--",       "黄牌",   "双方黄牌", "红牌",
                                         "超功率",   "超热量", "超射速"};
    return (type >= 0 && type < 7) ? QString::fromUtf8(kTexts[type]) : QStringLiteral("--");
}

// 事件的横幅颜色：1 对方(红) / 2 己方(绿) / 0 中性。
int eventBannerType(int id)
{
    switch (id) {
    case 3: case 4: case 5: case 8: case 9: case 12: return 2;
    case 6: case 7: case 10: case 11: case 13: case 14: return 1;
    default: return 0;
    }
}

QString assemblyText(const robomaster::TechCoreMotionStateSync& m)
{
    QStringList steps;
    if (m.basic_state() == 2)
        steps << QObject::tr("运动中");
    else if (m.basic_state() == 3)
        steps << QObject::tr("已到达");
    if (m.putin_state())
        steps << QObject::tr("已放入");
    if (m.move_state())
        steps << QObject::tr("已平移");
    if (m.rotate_state())
        steps << QObject::tr("已旋转");
    if (steps.isEmpty() && m.remain_time_step() == 0)
        return QString();
    QString text = QObject::tr("装配 ") + steps.join(QStringLiteral(" -> "));
    if (m.remain_time_step() > 0)
        text += QObject::tr("（剩余 %1s）").arg(m.remain_time_step());
    return text;
}

} // namespace

GameData::GameData(QObject* parent) : QObject(parent) {}

QStringList GameData::downlinkTopics()
{
    return QStringList{
        QStringLiteral("GameStatus"),
        QStringLiteral("GlobalUnitStatus"),
        QStringLiteral("GlobalLogisticsStatus"),
        QStringLiteral("GlobalSpecialMechanism"),
        QStringLiteral("Event"),
        QStringLiteral("RobotInjuryStat"),
        QStringLiteral("RobotRespawnStatus"),
        QStringLiteral("RobotStaticStatus"),
        QStringLiteral("RobotDynamicStatus"),
        QStringLiteral("RobotModuleStatus"),
        QStringLiteral("RobotPosition"),
        QStringLiteral("Buff"),
        QStringLiteral("PenaltyInfo"),
        QStringLiteral("RobotPathPlanInfo"),
        QStringLiteral("MapClickInfo"),
        QStringLiteral("RadarInfoToClient"),
        QStringLiteral("CustomByteBlock"),
        QStringLiteral("TechCoreMotionStateSync"),
        QStringLiteral("RobotPerformanceSelectionSync"),
        QStringLiteral("DeployModeStatusSync"),
        QStringLiteral("RuneStatusSync"),
        QStringLiteral("SentryStatusSync"),
        QStringLiteral("DartSelectTargetStatusSync"),
        QStringLiteral("SentryCtrlResult"),
        QStringLiteral("AirSupportStatusSync"),
    };
}

void GameData::setSelfRobotId(quint32 id) { m_selfId = id; }
void GameData::setSelfName(const QString& name) { m_selfName = name; }
void GameData::setAllyIsRed(bool red) { m_allyIsRed = red; }

void GameData::apply(const QString& topic, const QByteArray& payload)
{
    // 清理过期的一次性提示（横幅 / 地图标记）。
    for (int i = m_banners.size() - 1; i >= 0; --i)
        if (m_banners[i].clock.elapsed() > kBannerMs)
            m_banners.removeAt(i);
    for (int i = m_mapMarkers.size() - 1; i >= 0; --i)
        if (m_mapMarkers[i].clock.elapsed() > kMapMarkerMs)
            m_mapMarkers.removeAt(i);

    if (topic == QLatin1String("GameStatus")) {
        if (parseInto(m_game, payload))
            m_valid = true;
    } else if (topic == QLatin1String("GlobalUnitStatus")) {
        if (parseInto(m_units, payload))
            m_valid = true;
    } else if (topic == QLatin1String("GlobalLogisticsStatus")) {
        parseInto(m_logistics, payload);
    } else if (topic == QLatin1String("RobotDynamicStatus")) {
        parseInto(m_dynamic, payload);
    } else if (topic == QLatin1String("RobotModuleStatus")) {
        parseInto(m_modules, payload);
    } else if (topic == QLatin1String("RobotRespawnStatus")) {
        parseInto(m_respawn, payload);
    } else if (topic == QLatin1String("RuneStatusSync")) {
        parseInto(m_rune, payload);
    } else if (topic == QLatin1String("TechCoreMotionStateSync")) {
        parseInto(m_techCore, payload);
    } else if (topic == QLatin1String("DeployModeStatusSync")) {
        parseInto(m_deploy, payload);
    } else if (topic == QLatin1String("AirSupportStatusSync")) {
        parseInto(m_airSupport, payload);
    } else if (topic == QLatin1String("DartSelectTargetStatusSync")) {
        parseInto(m_dart, payload);
    } else if (topic == QLatin1String("SentryStatusSync")) {
        parseInto(m_sentry, payload);
    } else if (topic == QLatin1String("RobotPerformanceSelectionSync")) {
        parseInto(m_perf, payload);
    } else if (topic == QLatin1String("RobotPosition")) {
        // 官方 2.2.13：本车世界坐标与朝向（1Hz）。仅采纳本车（robot_id 匹配或未填）。
        robomaster::RobotPosition m;
        if (m.ParseFromArray(payload.constData(), payload.size())
            && (m.robot_id() == 0 || m.robot_id() == m_selfId))
            m_selfPos = std::move(m);
    } else if (topic == QLatin1String("RadarInfoToClient")) {
        parseInto(m_radar, payload);
    } else if (topic == QLatin1String("RobotPathPlanInfo")) {
        parseInto(m_path, payload);
    } else if (topic == QLatin1String("RobotInjuryStat")) {
        parseInto(m_injury, payload);
    } else if (topic == QLatin1String("GlobalSpecialMechanism")) {
        parseInto(m_mechanism, payload);
    } else if (topic == QLatin1String("MapClickInfo")) {
        robomaster::MapClickInfo m;
        if (m.ParseFromArray(payload.constData(), payload.size())) {
            m_mapMarkers.append({std::move(m), QElapsedTimer()});
            m_mapMarkers.last().clock.start();
        }
    } else if (topic == QLatin1String("RobotStaticStatus")) {
        robomaster::RobotStaticStatus m;
        if (m.ParseFromArray(payload.constData(), payload.size())) {
            const quint32 rel = teamRelative(m.robot_id());
            if (m.max_health())
                m_maxHealth[rel] = m.max_health();
            m_level[rel] = m.level();
            m_alive[rel] = m.alive_state() != 2;
            if (m.robot_id() == m_selfId || rel == teamRelative(m_selfId))
                m_selfStatic = std::move(m);
        }
    } else if (topic == QLatin1String("Buff")) {
        robomaster::Buff m;
        if (m.ParseFromArray(payload.constData(), payload.size())) {
            const bool mine = m.robot_id() == 0 || teamRelative(m.robot_id()) == teamRelative(m_selfId);
            if (mine) {
                for (int i = 0; i < m_buffs.size(); ++i) {
                    if (m_buffs[i].buff_type() == m.buff_type()) {
                        m_buffs.removeAt(i);
                        break;
                    }
                }
                if (m.buff_left_time() > 0)
                    m_buffs.append(std::move(m));
            }
        }
    } else if (topic == QLatin1String("Event")) {
        robomaster::Event m;
        if (m.ParseFromArray(payload.constData(), payload.size())) {
            const QString text = eventText(m.event_id(), QString::fromStdString(m.param()));
            emit logMessage(text);
            BannerEntry b;
            b.text = text;
            b.type = eventBannerType(int(m.event_id()));
            if (m.event_id() == 1)
                b.rich = killRichText(QString::fromStdString(m.param()));
            b.clock.start();
            m_banners.append(std::move(b));
        }
    } else if (topic == QLatin1String("PenaltyInfo")) {
        robomaster::PenaltyInfo m;
        if (m.ParseFromArray(payload.constData(), payload.size())) {
            const int type = int(m.penalty_type());
            m_penaltyType = type;
            m_penaltyEffectSec = int(m.penalty_effect_sec());
            m_penaltyClock.restart();
            if (type == 1 || type == 2)
                ++m_yellowCards;
            else if (type == 3)
                ++m_redCards;
            emit logMessage(tr("判罚：%1（剩余处罚 %2s，累计 %3 次）")
                                .arg(penaltyText(type))
                                .arg(m_penaltyEffectSec)
                                .arg(m.total_penalty_num()));
        }
    }
    // 其余 topic（RobotPosition / SentryCtrlResult 等）暂未映射到 HUD，先订阅但不处理。
}

HudData GameData::hudData() const
{
    HudData d;
    d.valid = m_valid;
    d.allyIsRed = m_allyIsRed;
    d.selfRobotId = int(m_selfId);

    if (m_game) {
        d.stage = int(m_game->current_stage());
        d.isPaused = m_game->is_paused();
        d.countdownSec = m_game->stage_countdown_sec();
        d.elapsedSec = m_game->stage_elapsed_sec();
        d.redScore = int(m_game->red_score());
        d.blueScore = int(m_game->blue_score());
        d.gameResult = int(m_game->game_result());
        d.endReason = int(m_game->end_reason());
    }

    if (m_units) {
        const auto& u = *m_units;
        d.ally.baseHealth = int(u.base_health());
        d.ally.baseShield = int(u.base_shield());
        d.ally.baseStatus = int(u.base_status());
        d.ally.outpostHealth = int(u.outpost_health());
        d.ally.outpostStatus = int(u.outpost_status());
        d.ally.totalDamage = int(u.total_damage_ally());
        d.enemy.baseHealth = int(u.enemy_base_health());
        d.enemy.baseShield = int(u.enemy_base_shield());
        d.enemy.baseStatus = int(u.enemy_base_status());
        d.enemy.outpostHealth = int(u.enemy_outpost_health());
        d.enemy.outpostStatus = int(u.enemy_outpost_status());
        d.enemy.totalDamage = int(u.total_damage_enemy());

        const int n = u.robot_health_size();
        const int enemyBase = n >= 10 ? 5 : n; // 官方顺序 5 己方 + 5 对方
        for (int i = 0; i < 4; ++i) {
            const quint32 id = kAllySlots[i].id;
            const int pi = kAllySlots[i].protoIndex;
            HudRobot ally;
            ally.id = int(id);
            ally.name = QString::fromUtf8(kAllySlots[i].name);
            ally.maxHealth = int(m_maxHealth.value(id, 0));
            ally.level = int(m_level.value(id, 0));
            ally.alive = m_alive.value(id, true);
            if (pi < n) {
                ally.valid = true;
                ally.health = int(u.robot_health(pi));
                if (pi < u.robot_bullets_size())
                    ally.ammo = u.robot_bullets(pi);
            }
            d.ally.robots.append(ally);

            HudRobot enemy;
            enemy.id = int(id);
            enemy.name = ally.name;
            if (enemyBase + pi < n) {
                enemy.valid = true;
                enemy.health = int(u.robot_health(enemyBase + pi));
                if (enemyBase + pi < u.robot_bullets_size())
                    enemy.ammo = u.robot_bullets(enemyBase + pi);
            }
            d.enemy.robots.append(enemy);
        }
    }

    if (m_logistics) {
        d.economy = quint64(m_logistics->remaining_economy());
        d.techLevel = int(m_logistics->tech_level());
    }
    if (m_rune) {
        d.runeStatus = int(m_rune->rune_status());
        d.runeArms = int(m_rune->activated_arms());
        d.runeRings = m_rune->average_rings();
    }
    if (m_modules)
        d.rfid = int(m_modules->rfid());

    // 堡垒占点：mechanism_id 1=己方堡垒被占, 2=对方堡垒被占。
    if (m_mechanism) {
        const int n = qMin(m_mechanism->mechanism_id_size(), m_mechanism->mechanism_time_sec_size());
        for (int i = 0; i < n; ++i) {
            const int id = int(m_mechanism->mechanism_id(i));
            const int t = int(m_mechanism->mechanism_time_sec(i));
            if (id == 1)
                d.allyFortressTime = t;
            else if (id == 2)
                d.enemyFortressTime = t;
        }
    }
    if (m_injury) {
        d.injury.valid = true;
        d.injury.totalDamage = int(m_injury->total_damage());
        d.injury.collisionDamage = int(m_injury->collision_damage());
        d.injury.smallProjectileDamage = int(m_injury->small_projectile_damage());
        d.injury.largeProjectileDamage = int(m_injury->large_projectile_damage());
        d.injury.dartSplashDamage = int(m_injury->dart_splash_damage());
        d.injury.moduleOfflineDamage = int(m_injury->module_offline_damage());
        d.injury.offlineDamage = int(m_injury->offline_damage());
        d.injury.penaltyDamage = int(m_injury->penalty_damage());
        d.injury.serverKillDamage = int(m_injury->server_kill_damage());
        d.injury.killerId = int(m_injury->killer_id());
    }

    HudSelfStatus& s = d.self;
    s.name = m_selfName;
    s.alive = m_alive.value(teamRelative(m_selfId), true);
    if (m_selfStatic) {
        s.valid = true;
        s.maxHealth = int(m_selfStatic->max_health());
        s.maxHeat = m_selfStatic->max_heat();
        s.maxChassisEnergy = int(m_selfStatic->max_chassis_energy());
        s.level = int(m_selfStatic->level());
        s.connectionState = int(m_selfStatic->connection_state());
        s.fieldState = int(m_selfStatic->field_state());
    }
    if (m_dynamic) {
        s.valid = true;
        s.health = int(m_dynamic->current_health());
        s.heat = m_dynamic->current_heat();
        s.experience = int(m_dynamic->current_experience());
        s.experienceForUpgrade = int(m_dynamic->experience_for_upgrade());
        s.chassisEnergy = int(m_dynamic->current_chassis_energy());
        s.remainingAmmo = int(m_dynamic->remaining_ammo());
        s.outOfCombat = m_dynamic->is_out_of_combat();
        s.outOfCombatCountdown = int(m_dynamic->out_of_combat_countdown());
        s.remoteHealAvailable = m_dynamic->can_remote_heal();
        s.remoteAmmoAvailable = m_dynamic->can_remote_ammo();
    }
    if (m_deploy)
        s.deployMode = m_deploy->status() == 1;
    if (m_modules) {
        struct Mod { int state; const char* name; };
        const Mod mods[] = {
            {int(m_modules->main_controller()), "主控"},
            {int(m_modules->power_manager()), "电源"},
            {int(m_modules->small_shooter()), "17mm"},
            {int(m_modules->big_shooter()), "42mm"},
            {int(m_modules->video_transmission()), "图传"},
            {int(m_modules->armor()), "装甲"},
            {int(m_modules->capacitor()), "电容"},
            {int(m_modules->uwb()), "UWB"},
            {int(m_modules->laser_detection_module()), "激光"},
            {int(m_modules->light_strip()), "灯条"},
            {int(m_modules->rfid()), "RFID"},
        };
        for (const Mod& mod : mods)
            s.modules.append({QString::fromUtf8(mod.name), mod.state});
    }
    for (const robomaster::Buff& b : m_buffs) {
        HudBuff hb;
        hb.type = int(b.buff_type());
        hb.level = b.buff_level();
        hb.maxTime = int(b.buff_max_time());
        hb.leftTime = int(b.buff_left_time());
        s.buffs.append(hb);
    }

    s.yellowCards = m_yellowCards;
    s.redCards = m_redCards;
    {
        const bool active = m_penaltyClock.isValid()
                            && m_penaltyClock.elapsed() < qint64(m_penaltyEffectSec) * 1000;
        s.overheatLocked = active && m_penaltyType == 5;
        s.speedLocked = active && m_penaltyType == 4;
        s.fireRateLocked = active && m_penaltyType == 6;
    }

    if (m_respawn) {
        d.bottom.respawnPending = m_respawn->is_pending_respawn();
        d.bottom.respawnProgress = int(m_respawn->current_respawn_progress());
        d.bottom.respawnTotal = int(m_respawn->total_respawn_progress());
        d.bottom.canFreeRespawn = m_respawn->can_free_respawn();
        d.bottom.goldCostForRespawn = int(m_respawn->gold_cost_for_respawn());
        d.bottom.canPayForRespawn = m_respawn->can_pay_for_respawn();
    }
    if (m_techCore)
        d.bottom.assemblyText = assemblyText(*m_techCore);
    if (m_techCore) {
        d.bottom.techCoreBasicState = int(m_techCore->basic_state());
        d.bottom.techCoreRemainStep = int(m_techCore->remain_time_step());
    }
    if (m_airSupport) {
        d.bottom.airSupportActive = m_airSupport->airsupport_status() == 1;
        d.bottom.airSupportLeftTime = int(m_airSupport->left_time());
        d.bottom.airSupportCostCoins = int(m_airSupport->cost_coins());
        d.bottom.airSupportBeingTargeted = m_airSupport->is_being_targeted() != 0;
        d.bottom.airSupportShooterStatus = int(m_airSupport->shooter_status());
    }
    if (m_dart) {
        d.bottom.dartTargetId = int(m_dart->target_id());
        d.bottom.dartOpen = int(m_dart->open());
    }
    if (m_perf) {
        d.bottom.perfShooter = int(m_perf->shooter());
        d.bottom.perfChassis = int(m_perf->chassis());
        d.bottom.perfSentry = int(m_perf->sentry_control());
    }
    if (m_sentry) {
        d.bottom.sentryPosture = int(m_sentry->posture_id());
        d.bottom.sentryWeakened = m_sentry->is_weakened();
        d.bottom.sentryPowered = m_sentry->is_powered();
    }
    for (const robomaster::Buff& b : m_buffs) {
        if (b.buff_type() == 6)
            d.bottom.ammoBuffActive = true;
    }

    // 雷达单位点：协议顺序 = 对方 1,2,3,4,6,7 + 己方 1,2,3,4,6,7（单位 cm）。
    if (m_radar) {
        d.radarValid = true;
        const int enemyBase = m_allyIsRed ? 101 : 1;
        const int ownBase = m_allyIsRed ? 1 : 101;
        static const int kRel[6] = {1, 2, 3, 4, 6, 7};
        const int n = qMin(m_radar->robot_info_size(), 12);
        for (int i = 0; i < n; ++i) {
            const auto& info = m_radar->robot_info(i);
            HudRadarUnit u;
            const int base = i < 6 ? enemyBase : ownBase;
            const int k = kRel[i < 6 ? i : i - 6];
            u.robotId = base + (k - 1);
            u.x = info.target_pos_x();
            u.y = info.target_pos_y();
            u.highlight = int(info.is_high_light());
            u.enemy = i < 6;
            d.radarUnits.append(u);
        }
    }

    // 本车位姿（RobotPosition，m）：小地图本车点与朝向箭头的权威来源。
    if (m_selfPos) {
        d.selfPosValid = true;
        d.selfPosX = m_selfPos->x();
        d.selfPosY = m_selfPos->y();
        d.selfYaw = m_selfPos->yaw();
    }

    // 哨兵路径：起点与偏移单位为 dm，统一换算为 cm 供小地图 2800x1500 映射。
    if (m_path) {
        d.pathValid = true;
        float x = float(m_path->start_pos_x()) * 10.0f;
        float y = float(m_path->start_pos_y()) * 10.0f;
        d.pathPoints.append(QPointF(x, y));
        const int n = qMin(m_path->offset_x_size(), m_path->offset_y_size());
        for (int i = 0; i < n; ++i) {
            x += float(m_path->offset_x(i)) * 10.0f;
            y += float(m_path->offset_y(i)) * 10.0f;
            d.pathPoints.append(QPointF(x, y));
        }
    }

    for (const MapMarkerEntry& e : m_mapMarkers) {
        if (e.clock.elapsed() > kMapMarkerMs)
            continue;
        HudMapMarker mm;
        mm.mode = int(e.msg.mode());
        mm.x = e.msg.map_x();
        mm.y = e.msg.map_y();
        d.mapMarkers.append(mm);
    }
    for (const BannerEntry& e : m_banners) {
        if (e.clock.elapsed() > kBannerMs)
            continue;
        HudBanner hb;
        hb.text = e.text;
        hb.type = e.type;
        hb.rich = e.rich;
        hb.ageMs = int(e.clock.elapsed());
        d.banners.append(hb);
    }
    return d;
}

} // namespace rm
