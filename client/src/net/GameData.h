#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

#include "core/HudData.h"
#include "robomaster.pb.h"

namespace rm {

// 把官方下行 MQTT 消息（topic 名 == 消息名，protobuf 二进制）解码为 HUD 数据模型。
class GameData : public QObject
{
    Q_OBJECT
public:
    explicit GameData(QObject* parent = nullptr);

    // 需要订阅的下行 topic（官方 V2.0.0 全部下行消息）。
    static QStringList downlinkTopics();

    void setSelfRobotId(quint32 id);
    void setSelfName(const QString& name);
    void setAllyIsRed(bool red);

    void apply(const QString& topic, const QByteArray& payload);
    HudData hudData() const;

signals:
    // 事件/判罚等需要展示到日志的消息。
    void logMessage(const QString& text);

private:
    quint32 m_selfId = 1;
    QString m_selfName;
    bool m_allyIsRed = true;
    bool m_valid = false;

    std::optional<robomaster::GameStatus> m_game;
    std::optional<robomaster::GlobalUnitStatus> m_units;
    std::optional<robomaster::GlobalLogisticsStatus> m_logistics;
    std::optional<robomaster::RobotDynamicStatus> m_dynamic;
    std::optional<robomaster::RobotModuleStatus> m_modules;
    std::optional<robomaster::RobotRespawnStatus> m_respawn;
    std::optional<robomaster::RobotStaticStatus> m_selfStatic;
    std::optional<robomaster::RuneStatusSync> m_rune;
    std::optional<robomaster::TechCoreMotionStateSync> m_techCore;
    std::optional<robomaster::DeployModeStatusSync> m_deploy;
    std::optional<robomaster::AirSupportStatusSync> m_airSupport;
    std::optional<robomaster::DartSelectTargetStatusSync> m_dart;
    std::optional<robomaster::SentryStatusSync> m_sentry;
    std::optional<robomaster::RobotPerformanceSelectionSync> m_perf;
    std::optional<robomaster::RadarInfoToClient> m_radar;
    std::optional<robomaster::RobotPosition> m_selfPos;
    std::optional<robomaster::RobotPathPlanInfo> m_path;
    std::optional<robomaster::RobotInjuryStat> m_injury;
    std::optional<robomaster::GlobalSpecialMechanism> m_mechanism;

    // 本队机器人固定属性（RobotStaticStatus），键为队内编号（1-9）。
    QHash<quint32, quint32> m_maxHealth;
    QHash<quint32, quint32> m_level;
    QHash<quint32, bool> m_alive;
    // 本车增益（仅 robot_id == 本车）。
    QVector<robomaster::Buff> m_buffs;

    // 判罚（PenaltyInfo 触发式）：记录最近一次判罚类型与生效时长，供 HUD 显示牌数与锁定告警。
    QElapsedTimer m_penaltyClock;
    int m_penaltyType = 0;
    int m_penaltyEffectSec = 0;
    int m_yellowCards = 0;
    int m_redCards = 0;

    // 小地图指令标记（MapClickInfo 触发式，带约 10s 存活期）。
    struct MapMarkerEntry
    {
        robomaster::MapClickInfo msg;
        QElapsedTimer clock;
    };
    QVector<MapMarkerEntry> m_mapMarkers;

    // 顶部战况横幅（Event 触发式，带约 4s 存活期）。
    struct BannerEntry
    {
        QString text;
        int type;
        QString rich;
        QElapsedTimer clock;
    };
    QVector<BannerEntry> m_banners;
};

} // namespace rm
