#include <QtTest>

#include <QSettings>

#include "core/Protocol.h"
#include "core/UserActions.h"

// 校验键位持久化逻辑与动作表自身（载荷可生成、id/topic 非空）。
// 需要图形界面的快捷键激活无法在无头环境验证，此处覆盖最易出错的状态逻辑。
class TestUserActions : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("RMTest"));
        QCoreApplication::setApplicationName(QStringLiteral("RMTest"));
        QSettings().clear();
    }

    void defaultsAreSet()
    {
        QCOMPARE(rm::actionKey(QStringLiteral("common.buy17")).toString(), QStringLiteral("Q"));
        QCOMPARE(rm::actionKey(QStringLiteral("common.respawn")).toString(), QStringLiteral("R"));
        QCOMPARE(rm::actionKey(QStringLiteral("rune.activate")).toString(), QStringLiteral("3"));
    }

    void setThenReadRoundTrips()
    {
        rm::setActionKey(QStringLiteral("common.buy17"), QKeySequence(Qt::Key_P));
        QCOMPARE(rm::actionKey(QStringLiteral("common.buy17")).toString(), QStringLiteral("P"));
    }

    void clearedKeyStaysEmpty()
    {
        rm::setActionKey(QStringLiteral("common.respawn"), QKeySequence());
        QVERIFY(rm::actionKey(QStringLiteral("common.respawn")).isEmpty());
    }

    void resetRestoresDefaults()
    {
        rm::resetActionKeys();
        QCOMPARE(rm::actionKey(QStringLiteral("common.buy17")).toString(), QStringLiteral("Q"));
        QCOMPARE(rm::actionKey(QStringLiteral("common.respawn")).toString(), QStringLiteral("R"));
    }

    void everyActionHasValidPayload()
    {
        const auto& actions = rm::userActions();
        QVERIFY(!actions.isEmpty());
        for (const rm::UserAction& a : actions) {
            QVERIFY(!a.id.isEmpty());
            QVERIFY(!a.group.isEmpty());
            QVERIFY(!a.label.isEmpty());
            QVERIFY(!a.topic.isEmpty());
            QVERIFY(a.build != nullptr);
            // marker.toggle 只切换标记模式，不发指令，载荷为空属预期。
            if (a.id != QStringLiteral("marker.toggle"))
                QVERIFY(!a.build(a.param.label.isEmpty() ? 0 : a.param.def).isEmpty());
        }
    }

    // 官方 KeyboardMouseControl.keyboard_value 的 16 个按键位必须与协议一致。
    void keyboardMouseBitMapping()
    {
        QCOMPARE(rm::proto::keyboardMouseBit(Qt::Key_W), 0);
        QCOMPARE(rm::proto::keyboardMouseBit(Qt::Key_S), 1);
        QCOMPARE(rm::proto::keyboardMouseBit(Qt::Key_A), 2);
        QCOMPARE(rm::proto::keyboardMouseBit(Qt::Key_D), 3);
        QCOMPARE(rm::proto::keyboardMouseBit(Qt::Key_Shift), 4);
        QCOMPARE(rm::proto::keyboardMouseBit(Qt::Key_Control), 5);
        QCOMPARE(rm::proto::keyboardMouseBit(Qt::Key_Q), 6);
        QCOMPARE(rm::proto::keyboardMouseBit(Qt::Key_E), 7);
        QCOMPARE(rm::proto::keyboardMouseBit(Qt::Key_R), 8);
        QCOMPARE(rm::proto::keyboardMouseBit(Qt::Key_F), 9);
        QCOMPARE(rm::proto::keyboardMouseBit(Qt::Key_G), 10);
        QCOMPARE(rm::proto::keyboardMouseBit(Qt::Key_Z), 11);
        QCOMPARE(rm::proto::keyboardMouseBit(Qt::Key_X), 12);
        QCOMPARE(rm::proto::keyboardMouseBit(Qt::Key_C), 13);
        QCOMPARE(rm::proto::keyboardMouseBit(Qt::Key_V), 14);
        QCOMPARE(rm::proto::keyboardMouseBit(Qt::Key_B), 15);
        QCOMPARE(rm::proto::keyboardMouseBit(Qt::Key_M), -1);
        QCOMPARE(rm::proto::keyboardMouseBit(Qt::Key_Escape), -1);
    }

    void filterByRobotType()
    {
        const auto heavy = rm::userActionsForRobot(rm::RobotType::Heavy);
        bool hasAssembly = false;
        for (const rm::UserAction& a : heavy) {
            QVERIFY(a.robots.contains(rm::RobotType::Heavy));
            hasAssembly = hasAssembly || a.id == QStringLiteral("heavy.assembly");
        }
        QVERIFY(hasAssembly);

        const auto sentry = rm::userActionsForRobot(rm::RobotType::Sentry);
        for (const rm::UserAction& a : sentry)
            QVERIFY(a.robots.contains(rm::RobotType::Sentry));

        const auto infantry = rm::userActionsForRobot(rm::RobotType::Infantry3);
        for (const rm::UserAction& a : infantry)
            QVERIFY(!a.id.startsWith(QStringLiteral("heavy.")));

        // 0 = 未知兵种 -> 返回全部。
        QCOMPARE(rm::userActionsForRobot(0).size(), rm::userActions().size());
    }
};

QTEST_MAIN(TestUserActions)
#include "test_user_actions.moc"
