#include "auth.h"
#include "fakemanager.h"
#include "harness.h"
#include "sessionmenu.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTest>

#include <memory>

using namespace Qt::StringLiterals;

class SessionMenuTest : public QObject
{
    Q_OBJECT

    Harness m_h;
    std::unique_ptr<FakeManager> m_manager;

    QDBusConnection bus(const QString &name) { return QDBusConnection::connectToBus(m_h.address(), name); }

    bool startManager(const QString &connection)
    {
        QDBusConnection conn = bus(connection);
        m_manager = std::make_unique<FakeManager>(conn);
        return conn.registerVirtualObject(FakeManager::Path, m_manager.get(), QDBusConnection::SubPath)
            && conn.registerService(FakeManager::Service);
    }

    QString owner() { return bus(u"caller"_s).interface()->serviceOwner(SessionMenu::service()).value(); }

    QDBusMessage call(const QString &method, const QVariantList &arguments)
    {
        QDBusMessage message = QDBusMessage::createMethodCall(SessionMenu::service(), SessionMenu::path(),
                                                              SessionMenu::interface(), method);
        message.setArguments(arguments);
        QDBusConnection caller = bus(u"caller"_s);
        const QDBusPendingCall pending = caller.asyncCall(message);
        if (!QTest::qWaitFor([&pending] { return pending.isFinished(); }, 5000))
            return {};
        return pending.reply();
    }

    QDBusMessage show(const QList<SessionMenuRow> &rows, const QString &current = {}, const QVariantMap &hints = {})
    {
        return call(u"Show"_s, {QVariant::fromValue(rows), current, hints});
    }

private slots:
    void initTestCase() { QVERIFY(m_h.startBus()); }

    void init() { m_h.writeCgroup(QCoreApplication::applicationPid(), Harness::UserCgroup); }

    void cleanup()
    {
        QDBusConnection::disconnectFromBus(u"session"_s);
        QDBusConnection::disconnectFromBus(u"system"_s);
        QDBusConnection::disconnectFromBus(u"squatter"_s);
        QDBusConnection::disconnectFromBus(u"caller"_s);
        QDBusConnection::disconnectFromBus(u"other"_s);
        m_manager.reset();
        QVERIFY(bus(u"other"_s).isConnected());
    }

    void validShowEmitsRequested()
    {
        Auth auth(bus(u"other"_s), m_h.path(u"proc"_s));
        SessionMenu menu(auth, bus(u"session"_s), bus(u"system"_s));
        QVERIFY(menu.publish(bus(u"session"_s)));
        QSignalSpy requested(&menu, &SessionMenu::requested);
        const QList<SessionMenuRow> rows{
            {u"steam"_s, u"Steam OS"_s, {{u"state"_s, u"available"_s}, {u"install"_s, false}}},
            {u"microdap"_s, u"Micro DAP"_s, {{u"state"_s, u"refused"_s}}},
            {u"android"_s, u"Android"_s, {{u"state"_s, u"not-installed"_s}, {u"install"_s, true}}},
        };
        const QDBusMessage reply =
            show(rows, u"steam"_s, {{u"in-game"_s, true}, {u"switching"_s, true}});
        QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
        QCOMPARE(requested.count(), 1);
        const QList<SessionMenuEntry> entries = requested.at(0).at(0).value<QList<SessionMenuEntry>>();
        QCOMPARE(entries.size(), 3);
        QCOMPARE(entries.at(0).name, u"steam"_s);
        QCOMPARE(entries.at(0).label, u"Steam OS"_s);
        QCOMPARE(entries.at(0).state, u"available"_s);
        QVERIFY(!entries.at(0).install);
        QCOMPARE(entries.at(1).state, u"refused"_s);
        QVERIFY(!entries.at(1).install);
        QCOMPARE(entries.at(2).state, u"not-installed"_s);
        QVERIFY(entries.at(2).install);
        QCOMPARE(requested.at(0).at(1).toString(), u"steam"_s);
        QCOMPARE(requested.at(0).at(2).toBool(), true);
        QCOMPARE(requested.at(0).at(3).toBool(), true);
    }

    void defaultsForAbsentKeys()
    {
        Auth auth(bus(u"other"_s), m_h.path(u"proc"_s));
        SessionMenu menu(auth, bus(u"session"_s), bus(u"system"_s));
        QVERIFY(menu.publish(bus(u"session"_s)));
        QSignalSpy requested(&menu, &SessionMenu::requested);
        const QDBusMessage reply = show({{u"steam"_s, u"Steam OS"_s, {}}});
        QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
        const QList<SessionMenuEntry> entries = requested.at(0).at(0).value<QList<SessionMenuEntry>>();
        QCOMPARE(entries.at(0).state, u"available"_s);
        QVERIFY(!entries.at(0).install);
        QCOMPARE(requested.at(0).at(1).toString(), QString());
        QCOMPARE(requested.at(0).at(2).toBool(), false);
        QCOMPARE(requested.at(0).at(3).toBool(), false);
    }

    void unknownKeysIgnored()
    {
        Auth auth(bus(u"other"_s), m_h.path(u"proc"_s));
        SessionMenu menu(auth, bus(u"session"_s), bus(u"system"_s));
        QVERIFY(menu.publish(bus(u"session"_s)));
        QSignalSpy requested(&menu, &SessionMenu::requested);
        const QDBusMessage reply =
            show({{u"steam"_s, u"Steam OS"_s, {{u"other"_s, 7}, {u"state"_s, u"refused"_s}}}}, u"steam"_s,
                 {{u"other"_s, true}});
        QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
        const QList<SessionMenuEntry> entries = requested.at(0).at(0).value<QList<SessionMenuEntry>>();
        QCOMPARE(entries.at(0).state, u"refused"_s);
        QVERIFY(!entries.at(0).install);
        QCOMPARE(requested.at(0).at(2).toBool(), false);
        QCOMPARE(requested.at(0).at(3).toBool(), false);
    }

    void unknownStateTreatedAvailable()
    {
        Auth auth(bus(u"other"_s), m_h.path(u"proc"_s));
        SessionMenu menu(auth, bus(u"session"_s), bus(u"system"_s));
        QVERIFY(menu.publish(bus(u"session"_s)));
        QSignalSpy requested(&menu, &SessionMenu::requested);
        QTest::ignoreMessage(QtInfoMsg,
                             QRegularExpression(u"microset overlay: session steam state broken is unknown"_s));
        const QDBusMessage reply = show({{u"steam"_s, u"Steam OS"_s, {{u"state"_s, u"broken"_s}}}});
        QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
        QCOMPARE(requested.at(0).at(0).value<QList<SessionMenuEntry>>().at(0).state, u"available"_s);
    }

    void showRejectsInvalidArgs()
    {
        Auth auth(bus(u"other"_s), m_h.path(u"proc"_s));
        SessionMenu menu(auth, bus(u"session"_s), bus(u"system"_s));
        QVERIFY(menu.publish(bus(u"session"_s)));
        QSignalSpy requested(&menu, &SessionMenu::requested);
        const auto invalid = [this, &requested](const QList<SessionMenuRow> &rows, const QString &current) {
            const QDBusMessage reply = show(rows, current);
            QCOMPARE(reply.type(), QDBusMessage::ErrorMessage);
            QCOMPARE(reply.errorName(), u"org.freedesktop.DBus.Error.InvalidArgs"_s);
            QCOMPARE(requested.count(), 0);
        };
        invalid({{u"Steam"_s, u"Steam OS"_s, {}}}, {});
        invalid({{u"steam"_s, QString(), {}}}, {});
        invalid({{u"steam"_s, QString(257, QChar(u'x')), {}}}, {});
        QList<SessionMenuRow> many;
        for (int i = 0; i < 65; ++i)
            many << SessionMenuRow{u"row-%1"_s.arg(i), u"Row"_s, {}};
        invalid(many, {});
        invalid({{u"steam"_s, u"Steam OS"_s, {}}}, u"microdap"_s);
    }

    void refusalIsAccessDenied()
    {
        Auth auth(bus(u"other"_s), m_h.path(u"proc"_s));
        SessionMenu menu(auth, bus(u"session"_s), bus(u"system"_s));
        QVERIFY(menu.publish(bus(u"session"_s)));
        QSignalSpy requested(&menu, &SessionMenu::requested);
        m_h.writeCgroup(QCoreApplication::applicationPid(), Harness::GameCgroup);
        QTest::ignoreMessage(QtInfoMsg,
                             QRegularExpression(u"microset overlay: Show by uid .* refused: called from a game"_s));
        const QDBusMessage reply = show({{u"steam"_s, u"Steam OS"_s, {}}});
        QCOMPARE(reply.type(), QDBusMessage::ErrorMessage);
        QCOMPARE(reply.errorName(), u"org.freedesktop.DBus.Error.AccessDenied"_s);
        QCOMPARE(requested.count(), 0);
    }

    void queuedNameWaitsForSquatter()
    {
        QDBusConnection squatter = bus(u"squatter"_s);
        QVERIFY(squatter.registerService(SessionMenu::service()));
        Auth auth(bus(u"other"_s), m_h.path(u"proc"_s));
        SessionMenu menu(auth, bus(u"session"_s), bus(u"system"_s));
        QTest::ignoreMessage(QtInfoMsg, "microset overlay: session menu name queued");
        QVERIFY(menu.publish(bus(u"session"_s)));
        QCOMPARE(owner(), squatter.baseService());
        QVERIFY(squatter.unregisterService(SessionMenu::service()));
        QTRY_COMPARE(owner(), bus(u"session"_s).baseService());
        QSignalSpy requested(&menu, &SessionMenu::requested);
        const QDBusMessage reply = show({{u"steam"_s, u"Steam OS"_s, {}}});
        QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
        QCOMPARE(requested.count(), 1);
    }

    void switchStartsUnit()
    {
        QVERIFY(startManager(u"system"_s));
        FakeManager *systemd = m_manager.get();
        Auth auth(bus(u"other"_s), m_h.path(u"proc"_s));
        SessionMenu menu(auth, bus(u"session"_s), bus(u"system"_s));
        QSignalSpy replied(&menu, &SessionMenu::switchReplied);
        menu.startSwitch(u"microdap"_s);
        QTRY_COMPARE(systemd->calls, (QStringList{u"Subscribe"_s, u"StartUnit"_s}));
        QCOMPARE(systemd->units, (QStringList{u"microos-session-switch@microdap.service"_s}));
        QCOMPARE(systemd->modes, (QStringList{u"replace"_s}));
        QTRY_COMPARE(replied.count(), 1);
        QCOMPARE(replied.at(0).at(0).toBool(), true);
    }

    void switchReplyError()
    {
        QVERIFY(startManager(u"system"_s));
        FakeManager *systemd = m_manager.get();
        systemd->fail = true;
        Auth auth(bus(u"other"_s), m_h.path(u"proc"_s));
        SessionMenu menu(auth, bus(u"session"_s), bus(u"system"_s));
        QSignalSpy replied(&menu, &SessionMenu::switchReplied);
        QTest::ignoreMessage(QtInfoMsg, QRegularExpression(u"microset overlay: switch to microdap failed: .*"_s));
        menu.startSwitch(u"microdap"_s);
        QTRY_COMPARE(replied.count(), 1);
        QCOMPARE(replied.at(0).at(0).toBool(), false);
    }

    void jobRemovedResults()
    {
        QVERIFY(startManager(u"system"_s));
        FakeManager *systemd = m_manager.get();
        Auth auth(bus(u"other"_s), m_h.path(u"proc"_s));
        SessionMenu menu(auth, bus(u"session"_s), bus(u"system"_s));
        QSignalSpy replied(&menu, &SessionMenu::switchReplied);
        QSignalSpy removed(&menu, &SessionMenu::switchJobRemoved);
        menu.startSwitch(u"microdap"_s);
        QTRY_COMPARE(replied.count(), 1);
        systemd->removed(systemd->job, u"microos-session-switch@microdap.service"_s, u"done"_s);
        QTRY_COMPARE(removed.count(), 1);
        QCOMPARE(removed.at(0).at(0).toString(), u"done"_s);
        systemd->job = u"/org/freedesktop/systemd1/job/5"_s;
        menu.startSwitch(u"steam"_s);
        QTRY_COMPARE(replied.count(), 2);
        systemd->removed(systemd->job, u"microos-session-switch@steam.service"_s, u"failed"_s);
        QTRY_COMPARE(removed.count(), 2);
        QCOMPARE(removed.at(1).at(0).toString(), u"failed"_s);
    }

    void jobRemovedOtherIgnored()
    {
        QVERIFY(startManager(u"system"_s));
        FakeManager *systemd = m_manager.get();
        Auth auth(bus(u"other"_s), m_h.path(u"proc"_s));
        SessionMenu menu(auth, bus(u"session"_s), bus(u"system"_s));
        QSignalSpy replied(&menu, &SessionMenu::switchReplied);
        QSignalSpy removed(&menu, &SessionMenu::switchJobRemoved);
        menu.startSwitch(u"microdap"_s);
        QTRY_COMPARE(replied.count(), 1);
        systemd->removed(u"/org/freedesktop/systemd1/job/9"_s, u"other.service"_s, u"failed"_s);
        QTest::qWait(200);
        QCOMPARE(removed.count(), 0);
    }

    void jobRemovedBeforeReply()
    {
        QVERIFY(startManager(u"system"_s));
        FakeManager *systemd = m_manager.get();
        systemd->earlyResult = u"done"_s;
        Auth auth(bus(u"other"_s), m_h.path(u"proc"_s));
        SessionMenu menu(auth, bus(u"session"_s), bus(u"system"_s));
        QSignalSpy replied(&menu, &SessionMenu::switchReplied);
        QSignalSpy removed(&menu, &SessionMenu::switchJobRemoved);
        menu.startSwitch(u"microdap"_s);
        QTRY_COMPARE(replied.count(), 1);
        QTRY_COMPARE(removed.count(), 1);
        QCOMPARE(removed.at(0).at(0).toString(), u"done"_s);
    }

    void installStartsUserUnit()
    {
        QVERIFY(startManager(u"session"_s));
        FakeManager *session = m_manager.get();
        Auth auth(bus(u"other"_s), m_h.path(u"proc"_s));
        SessionMenu menu(auth, bus(u"session"_s), bus(u"system"_s));
        QSignalSpy replied(&menu, &SessionMenu::installReplied);
        menu.startInstall(u"android"_s);
        QTRY_COMPARE(session->calls, QStringList{u"StartUnit"_s});
        QCOMPARE(session->units, QStringList{u"microos-session-install@android.service"_s});
        QCOMPARE(session->modes, QStringList{u"replace"_s});
        QTRY_COMPARE(replied.count(), 1);
        QVERIFY(replied.at(0).at(0).toBool());
    }
};

QTEST_MAIN(SessionMenuTest)
#include "tst_sessionmenu.moc"
