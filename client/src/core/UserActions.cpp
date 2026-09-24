#include "core/UserActions.h"

#include <QSettings>

#include "core/robots/RobotActions.h"

namespace rm {

namespace robot {

UserAction makeAction(const char* id, const char* group, const char* label, const char* topic,
                      std::function<QByteArray(int)> build, QVector<int> robots, Qt::Key key,
                      ActionParam param, const char* panel)
{
    UserAction u;
    u.id = QString::fromLatin1(id);
    u.group = QObject::tr(group);
    u.label = QObject::tr(label);
    u.topic = QString::fromLatin1(topic);
    u.build = std::move(build);
    u.robots = std::move(robots);
    u.param = std::move(param);
    u.panel = QString::fromLatin1(panel ? panel : "");
    if (key != Qt::Key_unknown)
        u.defaultKey = QKeySequence(key);
    return u;
}

} // namespace robot

namespace {

// 汇总各兵种模块 + 通用模块，顺序即界面顺序。
QVector<UserAction> makeActions()
{
    QVector<UserAction> a = robot::commonActions();
    a += robot::heavyActions();
    a += robot::infantryActions();
    a += robot::sentryActions();
    a += robot::aerialActions();
    a += robot::dartActions();
    a += robot::radarActions();
    return a;
}

} // namespace

const UserAction* userAction(const QString& id)
{
    for (const auto& a : userActions()) {
        if (a.id == id)
            return &a;
    }
    return nullptr;
}

QByteArray buildActionPayload(const UserAction& a, int param)
{
    return a.build ? a.build(param) : QByteArray();
}

const QVector<UserAction>& userActions()
{
    static const QVector<UserAction> actions = makeActions();
    return actions;
}

QVector<UserAction> userActionsForRobot(int robotType)
{
    if (robotType <= 0)
        return userActions();
    QVector<UserAction> out;
    for (const UserAction& a : userActions()) {
        if (a.robots.isEmpty() || a.robots.contains(robotType))
            out.push_back(a);
    }
    return out;
}

QKeySequence actionKey(const QString& id)
{
    QSettings s;
    const QString key = QStringLiteral("keys/") + id;
    if (s.contains(key))
        return QKeySequence::fromString(s.value(key).toString(), QKeySequence::PortableText);
    if (const UserAction* a = userAction(id))
        return a->defaultKey;
    return {};
}

void setActionKey(const QString& id, const QKeySequence& seq)
{
    QSettings s;
    s.setValue(QStringLiteral("keys/") + id, seq.toString(QKeySequence::PortableText));
}

void resetActionKeys()
{
    QSettings s;
    s.remove(QStringLiteral("keys"));
}

} // namespace rm
