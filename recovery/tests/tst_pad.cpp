#include "dbuspad.h"
#include "recovery.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusVariant>
#include <QDBusVirtualObject>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>
#include <QtQml/qqmlextensionplugin.h>

#include <memory>

Q_IMPORT_QML_PLUGIN(MicroSetRecoveryPlugin)

using namespace Qt::StringLiterals;

class FakeArbiter : public QDBusVirtualObject
{
public:
    explicit FakeArbiter(const QDBusConnection &bus)
        : m_bus(bus)
    {
    }

    QString holder() const { return m_stack.isEmpty() ? QString() : m_stack.last(); }

    QString introspect(const QString &) const override { return {}; }

    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override
    {
        const QString before = holder();
        if (message.interface() == DbusPad::arbiterInterface() && message.member() == u"Acquire"_s) {
            if (!m_stack.contains(message.service()))
                m_stack.append(message.service());
        } else if (message.interface() == DbusPad::arbiterInterface() && message.member() == u"Release"_s) {
            m_stack.removeAll(message.service());
        } else if (message.member() == u"Get"_s) {
            connection.send(message.createReply(QVariant::fromValue(QDBusVariant(holder()))));
            return true;
        } else {
            return false;
        }
        connection.send(message.createReply());
        if (holder() != before) {
            QDBusMessage changed = QDBusMessage::createSignal(DbusPad::arbiterPath(),
                                                              u"org.freedesktop.DBus.Properties"_s,
                                                              u"PropertiesChanged"_s);
            changed << DbusPad::arbiterInterface() << QVariantMap{{u"Holder"_s, holder()}} << QStringList();
            m_bus.send(changed);
        }
        return true;
    }

private:
    QDBusConnection m_bus;
    QStringList m_stack;
};

class PadTest : public QObject
{
    Q_OBJECT

    QProcess m_daemon;
    QString m_address;
    int m_arbiters = 0;
    std::unique_ptr<FakeArbiter> m_arbiter;
    std::unique_ptr<DbusPad> m_pad;

    QDBusConnection bus(const QString &name) { return QDBusConnection::connectToBus(m_address, name); }

    void startArbiter()
    {
        const QString name = u"arbiter%1"_s.arg(++m_arbiters);
        QDBusConnection conn = bus(name);
        m_arbiter = std::make_unique<FakeArbiter>(conn);
        QVERIFY(conn.registerVirtualObject(DbusPad::arbiterPath(), m_arbiter.get()));
        QVERIFY(conn.registerService(DbusPad::arbiterService()));
    }

    void stopArbiter()
    {
        QDBusConnection::disconnectFromBus(u"arbiter%1"_s.arg(m_arbiters));
        m_arbiter.reset();
    }

    void input(const QString &name, double value)
    {
        QDBusMessage event = QDBusMessage::createSignal(u"/org/shadowblip/InputPlumber/devices/target/dbus0"_s,
                                                        u"org.shadowblip.Input.DBusDevice"_s, u"InputEvent"_s);
        event << name << value;
        QVERIFY(bus(u"plumber"_s).send(event));
    }

    void tap(const QString &name)
    {
        input(name, 1.0);
        input(name, 0.0);
    }

    void call(const QString &connection, const QString &method)
    {
        const QDBusPendingCall pending = bus(connection).asyncCall(QDBusMessage::createMethodCall(
            DbusPad::arbiterService(), DbusPad::arbiterPath(), DbusPad::arbiterInterface(), method));
        QTRY_VERIFY(pending.isFinished());
        QVERIFY(!pending.isError());
    }

private slots:
    void initTestCase()
    {
        m_daemon.start(QStringLiteral(DBUS_DAEMON), {u"--session"_s, u"--nofork"_s, u"--print-address=1"_s});
        QVERIFY(m_daemon.waitForStarted());
        QTRY_VERIFY(m_daemon.canReadLine());
        m_address = QString::fromUtf8(m_daemon.readLine()).trimmed();
        QVERIFY(bus(u"plumber"_s).registerService(u"org.shadowblip.InputPlumber"_s));
        QVERIFY(bus(u"other"_s).isConnected());
    }

    void cleanupTestCase()
    {
        m_daemon.terminate();
        m_daemon.waitForFinished();
    }

    void init()
    {
        m_pad = std::make_unique<DbusPad>(bus(u"pad-session"_s), bus(u"pad-system"_s));
    }

    void cleanup()
    {
        m_pad.reset();
        QDBusConnection::disconnectFromBus(u"pad-session"_s);
        QDBusConnection::disconnectFromBus(u"pad-system"_s);
        stopArbiter();
    }

    void names()
    {
        const QList<std::pair<QString, int>> rows{
            {u"ui_up"_s, DbusPad::Up}, {u"ui_down"_s, DbusPad::Down}, {u"ui_left"_s, DbusPad::Left},
            {u"ui_right"_s, DbusPad::Right}, {u"ui_accept"_s, DbusPad::A}, {u"ui_back"_s, DbusPad::B},
            {u"ui_context"_s, DbusPad::X}, {u"ui_action"_s, DbusPad::Y}, {u"ui_l1"_s, DbusPad::LB},
            {u"ui_r1"_s, DbusPad::RB}, {u"ui_l2"_s, DbusPad::LT}, {u"ui_r2"_s, DbusPad::RT},
            {u"ui_option"_s, DbusPad::Start}, {u"ui_select"_s, DbusPad::Back},
        };
        for (const auto &[name, action] : rows)
            QCOMPARE(DbusPad::actionFor(name), action);
        QCOMPARE(DbusPad::actionFor(u"ui_guide"_s), -1);
        QCOMPARE(m_pad->actionName(DbusPad::Up), u"up"_s);
        QCOMPARE(m_pad->actionName(DbusPad::A), u"a"_s);
        QCOMPARE(m_pad->actionName(DbusPad::B), u"b"_s);
        QCOMPARE(m_pad->actionName(DbusPad::LT), u"lt"_s);
        QCOMPARE(m_pad->actionName(DbusPad::Back), u"back"_s);
    }

    void holderGates()
    {
        QSignalSpy actions(m_pad.get(), &DbusPad::action);
        QSignalSpy released(m_pad.get(), &DbusPad::released);
        tap(u"ui_down"_s);
        QTest::qWait(100);
        QCOMPARE(actions.count(), 0);

        startArbiter();
        QTRY_VERIFY(m_pad->holding());
        QCOMPARE(m_arbiter->holder(), bus(u"pad-session"_s).baseService());
        tap(u"ui_down"_s);
        QTRY_COMPARE(actions.count(), 1);
        QCOMPARE(actions.at(0).at(0).value<DbusPad::Action>(), DbusPad::Down);
        QCOMPARE(actions.at(0).at(1).toBool(), false);
        QTRY_COMPARE(released.count(), 1);

        input(u"ui_accept"_s, 1.0);
        QTRY_COMPARE(actions.count(), 2);
        call(u"other"_s, u"Acquire"_s);
        QTRY_VERIFY(!m_pad->holding());
        QCOMPARE(released.count(), 2);
        QCOMPARE(released.at(1).at(0).value<DbusPad::Action>(), DbusPad::A);
        input(u"ui_accept"_s, 0.0);
        tap(u"ui_up"_s);
        QTest::qWait(100);
        QCOMPARE(actions.count(), 2);
        QCOMPARE(released.count(), 2);

        call(u"other"_s, u"Release"_s);
        QTRY_VERIFY(m_pad->holding());
        tap(u"ui_back"_s);
        QTRY_COMPARE(actions.count(), 3);
        QCOMPARE(actions.at(2).at(0).value<DbusPad::Action>(), DbusPad::B);
    }

    void arbiterRestart()
    {
        startArbiter();
        QTRY_VERIFY(m_pad->holding());
        stopArbiter();
        QTRY_VERIFY(!m_pad->holding());
        QSignalSpy actions(m_pad.get(), &DbusPad::action);
        tap(u"ui_down"_s);
        QTest::qWait(100);
        QCOMPARE(actions.count(), 0);
        startArbiter();
        QTRY_VERIFY(m_pad->holding());
        QCOMPARE(m_arbiter->holder(), bus(u"pad-session"_s).baseService());
        tap(u"ui_down"_s);
        QTRY_COMPARE(actions.count(), 1);
    }

    void directionRepeats()
    {
        startArbiter();
        QTRY_VERIFY(m_pad->holding());
        QSignalSpy actions(m_pad.get(), &DbusPad::action);
        input(u"ui_down"_s, 1.0);
        QTRY_VERIFY_WITH_TIMEOUT(actions.count() >= 3, DbusPad::RepeatInitialMs + 4 * DbusPad::RepeatIntervalMs);
        QCOMPARE(actions.at(1).at(1).toBool(), true);
        input(u"ui_down"_s, 0.0);
        QTest::qWait(100);
        const qsizetype count = actions.count();
        QTest::qWait(2 * DbusPad::RepeatIntervalMs);
        QCOMPARE(actions.count(), count);
        input(u"ui_accept"_s, 1.0);
        QTest::qWait(DbusPad::RepeatInitialMs + 2 * DbusPad::RepeatIntervalMs);
        QCOMPARE(actions.count(), count + 1);
        input(u"ui_accept"_s, 0.0);
    }

    void screenFollowsPad()
    {
        startArbiter();
        QTRY_VERIFY(m_pad->holding());
        Recovery recovery;
        recovery.setFixture(u"steam"_s, u"Steam"_s, u"gamescope-session-plus@steam.service"_s, true, u"steam"_s,
                            true, {u"line"_s});
        QQmlApplicationEngine engine;
        engine.setInitialProperties({
            {u"recovery"_s, QVariant::fromValue(static_cast<QObject *>(&recovery))},
            {u"pad"_s, QVariant::fromValue(static_cast<QObject *>(m_pad.get()))},
            {u"screenshot"_s, true},
            {u"shotWidth"_s, 960},
            {u"shotHeight"_s, 640},
        });
        engine.loadFromModule("MicroSetRecovery", "Main");
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
        QVERIFY(window);
        QObject *screen = window->findChild<QObject *>(u"recoveryScreen"_s);
        QVERIFY(screen);
        QObject *list = qvariant_cast<QObject *>(screen->property("list"));
        QObject *dialog = qvariant_cast<QObject *>(screen->property("dialog"));
        QVERIFY(list && dialog);
        QCOMPARE(list->property("focusIndex").toInt(), 0);

        tap(u"ui_down"_s);
        QTRY_COMPARE(list->property("focusIndex").toInt(), 1);
        tap(u"ui_up"_s);
        QTRY_COMPARE(list->property("focusIndex").toInt(), 0);
        tap(u"ui_down"_s);
        QTRY_COMPARE(list->property("focusIndex").toInt(), 1);
        tap(u"ui_accept"_s);
        QTRY_VERIFY(dialog->property("shown").toBool());
        tap(u"ui_back"_s);
        QTRY_VERIFY(!dialog->property("shown").toBool());

        call(u"other"_s, u"Acquire"_s);
        QTRY_VERIFY(!m_pad->holding());
        tap(u"ui_down"_s);
        QTest::qWait(100);
        QCOMPARE(list->property("focusIndex").toInt(), 1);
        call(u"other"_s, u"Release"_s);
        QTRY_VERIFY(m_pad->holding());
    }
};

QTEST_MAIN(PadTest)
#include "tst_pad.moc"
