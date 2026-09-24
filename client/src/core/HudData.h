#pragma once

#include <QColor>
#include <QPointF>
#include <QRect>
#include <QString>
#include <QVector>

namespace rm {

// HUD 数据模型（视图模型层）各 UI 中需要的字段都在此处集中定义，
// 新增数据时先加到这里，再在 GameData 填充、在对应 Widget 消费。


// 一行日志（本车日志 + 系统日志合并显示）
struct HudLogEntry
{
    QString time;
    QString source; // "本车" / "系统"
    QString text;
    int type = 0;   // 0 普通 / 1 警告 / 2 错误
};

// 小地图上的一个单位（对应 RadarInfoToClient）
struct HudRadarUnit
{
    int robotId = 0;
    float x = 0.0f;    // cm
    float y = 0.0f;    // cm
    int highlight = 0; // 0 否 / 1 是 / 2 是但定位模块离线
    bool enemy = false; // 对方单位（雷达只提供对方与己方位置，颜色区分）
};

struct HudRobot
{
    int id = 0;           // 队内编号（1 重装 / 3,4,5 步兵 / 6 空中 / 7 哨兵 / 8 飞镖 / 9 雷达）
    QString name;
    bool valid = false;   // 是否收到过该机器人的数据
    bool alive = true;
    int health = 0;
    int maxHealth = 0;
    int ammo = 0;
    int level = 0;
};

struct HudTeam
{
    int baseHealth = 0;
    int baseShield = 0;
    int baseStatus = 0;
    int outpostHealth = 0;
    int outpostStatus = 0;
    int totalDamage = 0;
    QVector<HudRobot> robots; // 己方侧顺序见 GameData::kAllySlots（重装/步兵3/步兵4/哨兵）
};

// 一条增益（对应 Buff 消息）
struct HudBuff
{
    QString name;
    int type = 0;      // 1 攻击 / 2 防御(易伤) / 3 热冷却 / 4 底盘功率 / 5 回血 / 6 发弹量 / 7 地形跨越
    int level = 0;     // 增益值（百分比或直接值，随类型）
    int maxTime = 0;
    int leftTime = 0;
};

// 本车模块上电/在线状态（对应 RobotModuleStatus）
struct HudModule
{
    QString name;
    int state = 0;   // 0 离线 / 1 在线 / 2 因安装不规范被视为离线
};

// 本车一次存活期的累计受伤（对应 RobotInjuryStat）
struct HudInjuryStat
{
    bool valid = false;
    int totalDamage = 0;
    int collisionDamage = 0;
    int smallProjectileDamage = 0; // 17mm
    int largeProjectileDamage = 0; // 42mm
    int dartSplashDamage = 0;
    int moduleOfflineDamage = 0;
    int offlineDamage = 0;
    int penaltyDamage = 0;
    int serverKillDamage = 0;
    int killerId = 0;
};

// 小地图指令标记（对应 MapClickInfo，单位 m）
struct HudMapMarker
{
    int mode = 0;      // 1 攻击 / 2 防御 / 3 警戒 / 4 自定义
    float x = 0.0f;    // m
    float y = 0.0f;    // m
    bool fromSelf = false; // 己方发出（robot_id 非全 0）则为 false→对方
};

// 屏幕顶部战况横幅
struct HudBanner
{
    QString text;
    int type = 0;    // 1 对方事件(红) / 2 己方事件(绿) / 0 中性
    int ageMs = 0;
    QString rich;    // 非空则用富文本渲染（红/蓝双方着色）
};

// 本车（MQTT clientID 所绑定机器人）状态
struct HudSelfStatus
{
    QString name;
    bool valid = false;
    bool alive = true;            // 本车是否存活（RobotStaticStatus.alive_state != 2）
    int health = 0;
    int maxHealth = 0;
    float heat = 0.0f;
    float maxHeat = 0.0f;
    int level = 0;
    int experience = 0;
    int experienceForUpgrade = 0;
    int chassisEnergy = 0;
    int maxChassisEnergy = 0;
    int remainingAmmo = 0;
    bool outOfCombat = false;
    int outOfCombatCountdown = 0;
    bool remoteHealAvailable = false;
    bool remoteAmmoAvailable = false;
    bool deployMode = false;      // 重装部署模式
    QVector<HudModule> modules;   // 各模块上电/在线状态（含发射机构、主控、电源等）
    int connectionState = -1;     // 机载裁判系统连接 0 未连接 / 1 已连接 / -1 未知
    int fieldState = -1;          // 0 已上场 / 1 未上场 / -1 未知
    int yellowCards = 0;          // 累计黄牌数（PenaltyInfo 1/2）
    int redCards = 0;             // 累计红牌数（PenaltyInfo 3）
    bool overheatLocked = false;  // 判罚：超热量（PenaltyInfo 5）生效中
    bool speedLocked = false;     // 判罚：超功率（PenaltyInfo 4）生效中
    bool fireRateLocked = false;  // 判罚：超射速（PenaltyInfo 6）生效中
    QVector<HudBuff> buffs;
};

// 底部居中状态行（按需显示）
struct HudBottomStatus
{
    bool respawnPending = false;
    int respawnProgress = 0;
    int respawnTotal = 0;
    bool canFreeRespawn = false;
    int goldCostForRespawn = 0;
    bool canPayForRespawn = false;
    QString assemblyText;         // 非空则显示重装装配进度
    bool airSupportActive = false;
    int airSupportLeftTime = 0;
    int airSupportCostCoins = 0;
    bool airSupportBeingTargeted = false;
    int airSupportShooterStatus = 0; // 0 锁定不可解 / 1 正常 / 2 锁定可花金币解
    int techCoreBasicState = 0;    // 0 无 / 1 初始 / 2 移动中 / 3 已到达
    int techCoreRemainStep = 0;
    int dartTargetId = 0;          // DartSelectTargetStatusSync
    int dartOpen = 0;              // 0 未开启 / 1 开启中 / 2 已开启
    int perfShooter = 0;           // RobotPerformanceSelectionSync
    int perfChassis = 0;
    int perfSentry = 0;
    bool ammoBuffActive = false;   // 是否存在类型 6 增益（在补给区）
    int airSupportCounterCooldown = -1; // <0 表示无倒计时
    // 哨兵姿态（SentryStatusSync）
    int sentryPosture = 0;         // 0 未知 / 1 进攻 / 2 防御 / 3 移动
    bool sentryWeakened = false;   // 是否弱化
    bool sentryPowered = false;    // 是否强化
};

struct HudData
{
    bool valid = false;
    int stage = 0;
    bool isPaused = false;
    int countdownSec = 0;
    int elapsedSec = 0;
    int redScore = 0;
    int blueScore = 0;
    int gameResult = 255;    // 0 平局 / 1 红胜 / 2 蓝胜 / 255 无效
    int endReason = 0;       // GameStatus.end_reason
    int selfRobotId = 0;     // 本车绝对编号（红 1-9 / 蓝 101-109）
    bool allyIsRed = true;
    HudTeam ally;
    HudTeam enemy;
    // 能量机关（大符）
    int runeStatus = 0;
    int runeArms = 0;
    float runeRings = 0.0f;
    int rfid = 0;            // 0 离线 / 1 在线 / 2 安装不规范离线
    quint64 economy = 0;
    int techLevel = 0;
    // 堡垒占点：<0 表示未被占，>=0 为剩余秒数
    int allyFortressTime = -1;
    int enemyFortressTime = -1;
    HudSelfStatus self;
    HudBottomStatus bottom;

    // 附加数据,借鉴东莞ACE
    QVector<HudLogEntry> logs;          // 左列合并日志（本车 + 系统）
    QVector<HudRadarUnit> radarUnits;   // 小地图单位（RadarInfoToClient）
    bool radarValid = false;
    // 本车实测位姿（RobotPosition，1Hz；x/y 单位 m，yaw 度、正北 0°、顺时针为正）。
    bool selfPosValid = false;
    float selfPosX = 0.0f;
    float selfPosY = 0.0f;
    float selfYaw = 0.0f;
    QVector<QPointF> pathPoints;        // 小地图路径（RobotPathPlanInfo），单位：cm
    bool pathValid = false;
    int videoSource = 0;                // 0 UDP 为主 / 1 CustomByteBlock 为主
    bool udpInvert = false;             // 图传 180° 反转
    bool udpVideoReady = false;
    bool customVideoReady = false;
    QString customVideoText;            // 副视频不可用时的说明文案
    bool showCrosshair = true;          // 比赛模式显示准星（操作模式隐藏）
    bool showVideoLog = false;          // 按住 Tab 时才显示左列本车视频/日志面板
    bool showMinimap = true;            // 是否显示小地图（设置项）
    bool showFps = false;               // 是否显示 FPS（设置项）
    int fps = 0;                        // 当前刷新帧率
    bool showDamagePanel = false;       // 按住 ~ 时显示本车受伤统计面板
    bool showStatsTable = false;        // 按住 T 时显示双方机器人数据表
    HudInjuryStat injury;               // 本车受伤统计（RobotInjuryStat）
    QVector<HudMapMarker> mapMarkers;   // 小地图上的指令标记（MapClickInfo）
    QVector<HudBanner> banners;         // 顶部战况横幅（Event）
    bool connectionLost = false;        // MQTT 连接断开
};

} // namespace rm
