#include "auth.h"
#include "dbuspad.h"
#include "fakelogind.h"
#include "fakemicrosetd.h"
#include "framelimitinfo.h"
#include "harness.h"
#include "holdhint.h"
#include "overlay.h"
#include "overlayservice.h"
#include "runninggame.h"
#include "settingsmodel.h"
#include "sleepinhibitor.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusVariant>
#include <QDBusVirtualObject>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

#include <sys/file.h>
#include <time.h>

#include <unistd.h>

using namespace Qt::StringLiterals;

class FakeArbiter : public QDBusVirtualObject
{
public:
    explicit FakeArbiter(const QDBusConnection &bus)
        : m_bus(bus)
    {
    }

    QString holder() const { return m_stack.isEmpty() ? QString() : m_stack.last(); }
    QStringList stack() const { return m_stack; }

    QString introspect(const QString &) const override { return {}; }

    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override
    {
        const QString before = holder();
        const QString sender = message.service();
        if (message.interface() == DbusPad::arbiterInterface() && message.member() == u"Acquire"_s) {
            m_stack.removeAll(sender);
            m_stack.append(sender);
        } else if (message.interface() == DbusPad::arbiterInterface() && message.member() == u"Release"_s) {
            if (refuseRelease) {
                connection.send(message.createErrorReply(u"org.freedesktop.DBus.Error.Failed"_s, u"refused"_s));
                return true;
            }
            m_stack.removeAll(sender);
            ++releases;
        } else if (message.member() == u"Get"_s) {
            connection.send(message.createReply(QVariant::fromValue(QDBusVariant(holder()))));
            return true;
        } else {
            return false;
        }
        connection.send(message.createReply());
        if (holder() != before) {
            ++changes;
            QDBusMessage changed = QDBusMessage::createSignal(DbusPad::arbiterPath(),
                                                              u"org.freedesktop.DBus.Properties"_s,
                                                              u"PropertiesChanged"_s);
            changed << DbusPad::arbiterInterface() << QVariantMap{{u"Holder"_s, holder()}} << QStringList();
            m_bus.send(changed);
        }
        return true;
    }

    int changes = 0;
    int releases = 0;
    bool refuseRelease = false;

private:
    QDBusConnection m_bus;
    QStringList m_stack;
};

class FakeSystemd : public QDBusVirtualObject
{
public:
    struct Unit
    {
        QString name;
        QString slice;
        qulonglong since = 0;
        QList<uint> pids;
    };

    static inline const QString Path = u"/org/freedesktop/systemd1"_s;
    static inline const QString Manager = u"org.freedesktop.systemd1.Manager"_s;

    explicit FakeSystemd(const QDBusConnection &bus)
        : m_bus(bus)
    {
    }

    QString introspect(const QString &) const override { return {}; }

    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override
    {
        calls << message.member();
        if (message.member() == u"ListUnitsByPatterns"_s && fail) {
            connection.send(message.createErrorReply(u"org.freedesktop.DBus.Error.Failed"_s, u"failed"_s));
        } else if (message.member() == u"ListUnitsByPatterns"_s) {
            QList<SystemdUnit> out;
            for (qsizetype i = 0; i < units.size(); ++i) {
                SystemdUnit unit;
                unit.name = units.at(i).name;
                unit.active = u"active"_s;
                unit.path = QDBusObjectPath(Path + u"/unit/"_s + QString::number(i));
                unit.jobPath = QDBusObjectPath(u"/"_s);
                out << unit;
            }
            connection.send(message.createReply(QVariant::fromValue(out)));
        } else if (message.member() == u"GetUnitProcesses"_s) {
            QList<UnitProcess> out;
            for (const Unit &unit : std::as_const(units))
                if (unit.name == message.arguments().value(0).toString())
                    for (const uint pid : unit.pids)
                        out << UnitProcess{u"/game.slice/"_s + unit.name, pid, u"wine"_s};
            connection.send(message.createReply(QVariant::fromValue(out)));
        } else if (message.member() == u"Get"_s) {
            const Unit unit = units.value(message.path().section(u'/', -1).toInt());
            const QString property = message.arguments().value(1).toString();
            const QVariant value = property == u"Slice"_s ? QVariant(unit.slice) : QVariant::fromValue(unit.since);
            connection.send(message.createReply(QVariant::fromValue(QDBusVariant(value))));
        } else {
            connection.send(message.createReply());
        }
        return true;
    }

    void removed(const QString &name)
    {
        QDBusMessage signal = QDBusMessage::createSignal(Path, Manager, u"UnitRemoved"_s);
        signal << name << QVariant::fromValue(QDBusObjectPath(Path + u"/unit/0"_s));
        m_bus.send(signal);
    }

    void state(int index, const QString &active)
    {
        QDBusMessage signal = QDBusMessage::createSignal(Path + u"/unit/"_s + QString::number(index),
                                                         u"org.freedesktop.DBus.Properties"_s, u"PropertiesChanged"_s);
        signal << u"org.freedesktop.systemd1.Unit"_s << QVariantMap{{u"ActiveState"_s, active}} << QStringList();
        m_bus.send(signal);
    }

    QList<Unit> units;
    QStringList calls;
    bool fail = false;

private:
    QDBusConnection m_bus;
};

class OverlayTest : public QObject
{
    Q_OBJECT

    Harness m_h;
    int m_arbiters = 0;
    std::unique_ptr<FakeArbiter> m_arbiter;
    std::unique_ptr<FakeLogind> m_logind;
    std::unique_ptr<DbusPad> m_pad;
    std::unique_ptr<SleepInhibitor> m_sleep;
    std::unique_ptr<Overlay> m_overlay;
    std::unique_ptr<QQuickWindow> m_window;
    std::unique_ptr<QQuickWindow> m_toast;
    QStringList m_events;

    QDBusConnection bus(const QString &name) { return QDBusConnection::connectToBus(m_h.address(), name); }

    static QByteArray cmdline(const QList<QByteArray> &args)
    {
        QByteArray out;
        for (const QByteArray &arg : args)
            out += arg + '\0';
        return out;
    }

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

    void call(const QString &connection, const QString &method)
    {
        const QDBusPendingCall pending = bus(connection).asyncCall(QDBusMessage::createMethodCall(
            DbusPad::arbiterService(), DbusPad::arbiterPath(), DbusPad::arbiterInterface(), method));
        QTRY_VERIFY(pending.isFinished());
        QVERIFY(!pending.isError());
    }

    QString other() { return bus(u"other"_s).baseService(); }

    void writeEnviron(uint pid, const QList<QByteArray> &entries)
    {
        QByteArray data;
        for (const QByteArray &entry : entries)
            data += entry + '\0';
        m_h.writeFile(u"proc/%1/environ"_s.arg(pid), data);
    }

    static QStringList keysOf(const QVariantList &rows)
    {
        QStringList out;
        for (const QVariant &row : rows)
            out << row.toMap().value(u"key"_s).toString();
        return out;
    }

    static QVariantMap rowOf(const QVariantList &rows, const QString &key)
    {
        for (const QVariant &row : rows)
            if (row.toMap().value(u"key"_s).toString() == key)
                return row.toMap();
        return {};
    }
    QString ours() { return m_pad->name(); }
    bool windowUp() { return m_window->handle() && m_window->isVisible(); }

    void showSettled()
    {
        m_overlay->show();
        QTRY_COMPARE(m_arbiter->holder(), ours());
        QTRY_COMPARE(m_pad->holder(), ours());
        QTRY_COMPARE(m_overlay->opacity(), 1.0);
        QVERIFY(windowUp());
    }

private slots:
    void initTestCase()
    {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
        QVERIFY(m_h.startBus());
        QVERIFY(bus(u"other"_s).isConnected());
    }

    void init()
    {
        startArbiter();
        m_logind = std::make_unique<FakeLogind>(m_h.address());
        QVERIFY(m_logind->start());
        m_events.clear();
        m_window = std::make_unique<QQuickWindow>();
        m_window->resize(960, 640);
        m_toast = std::make_unique<QQuickWindow>();
        m_toast->resize(960, 40);
        m_pad = std::make_unique<DbusPad>(bus(u"pad-session"_s), bus(u"pad-system"_s), nullptr, false);
        m_sleep = std::make_unique<SleepInhibitor>(bus(u"pad-system"_s), u"test"_s);
        m_sleep->take();
        QTRY_COMPARE(m_logind->open(), 1);
        m_overlay = std::make_unique<Overlay>(m_pad.get(), m_sleep.get());
        m_overlay->setWindow(m_window.get());
        m_overlay->setToastWindow(m_toast.get());
        m_overlay->setHooks(
            [this](QWindow *w) {
                m_events << (w == m_window.get() ? u"create"_s : u"create toast"_s);
                w->create();
            },
            [this](QWindow *w) {
                m_events << u"create toast"_s;
                w->create();
            },
            [this](QWindow *w) { m_events << (w->isVisible() ? u"lower"_s : u"lower unmapped"_s); });
    }

    void cleanup()
    {
        m_overlay.reset();
        m_sleep.reset();
        m_pad.reset();
        m_window.reset();
        m_toast.reset();
        QDBusConnection::disconnectFromBus(u"pad-session"_s);
        QDBusConnection::disconnectFromBus(u"pad-system"_s);
        QDBusConnection::disconnectFromBus(u"other"_s);
        QVERIFY(bus(u"other"_s).isConnected());
        m_logind.reset();
        stopArbiter();
    }

    void hiddenShowsAndShownHides()
    {
        QSignalSpy shown(m_overlay.get(), &Overlay::shownChanged);
        QSignalSpy unused(m_overlay.get(), &Overlay::unused);
        m_overlay->toggle();
        QVERIFY(m_overlay->shown());
        QCOMPARE(m_events, (QStringList{u"create"_s, u"lower"_s}));
        QVERIFY(windowUp());
        QTRY_COMPARE(m_arbiter->holder(), ours());
        QTRY_COMPARE(m_overlay->opacity(), 1.0);
        m_overlay->toggle();
        QVERIFY(!m_overlay->shown());
        QTRY_COMPARE(m_arbiter->holder(), QString());
        QTRY_VERIFY(!m_window->handle());
        QCOMPARE(m_overlay->opacity(), 0.0);
        QCOMPARE(m_overlay->created(), 1);
        QCOMPARE(m_overlay->destroyed(), 1);
        QCOMPARE(shown.count(), 2);
        QTRY_COMPARE(unused.count(), 1);
        m_overlay->hide();
        QCOMPARE(shown.count(), 2);
        QTest::qWait(50);
        QCOMPARE(unused.count(), 1);
    }

    void showOnShownIsNoop()
    {
        showSettled();
        const int changes = m_arbiter->changes;
        m_overlay->show();
        QTest::qWait(100);
        QCOMPARE(m_arbiter->changes, changes);
        QCOMPARE(m_overlay->created(), 1);
    }

    void buriedComesBackWithOneHolderChange()
    {
        showSettled();
        call(u"other"_s, u"Acquire"_s);
        QTRY_COMPARE(m_pad->holder(), other());
        QTRY_VERIFY(!m_window->handle());
        QVERIFY(m_overlay->shown());
        QVERIFY(m_overlay->buried());
        QCOMPARE(m_overlay->opacity(), 1.0);
        QSignalSpy unused(m_overlay.get(), &Overlay::unused);
        QTest::qWait(50);
        QCOMPARE(unused.count(), 0);
        const int changes = m_arbiter->changes;
        QSignalSpy holders(m_pad.get(), &DbusPad::holderChanged);
        m_events.clear();
        m_overlay->toggle();
        QTRY_COMPARE(m_arbiter->holder(), ours());
        QTRY_VERIFY(windowUp());
        QTest::qWait(100);
        QCOMPARE(m_arbiter->changes, changes + 1);
        QCOMPARE(holders.count(), 1);
        QCOMPARE(m_arbiter->stack(), (QStringList{other(), ours()}));
        QCOMPARE(m_events, (QStringList{u"create"_s, u"lower"_s}));
        QVERIFY(m_overlay->shown());
    }

    void holderBackRecreates()
    {
        showSettled();
        call(u"other"_s, u"Acquire"_s);
        QTRY_VERIFY(!m_window->handle());
        call(u"other"_s, u"Release"_s);
        QTRY_COMPARE(m_pad->holder(), ours());
        QTRY_VERIFY(windowUp());
        QCOMPARE(m_overlay->created(), 2);
        QCOMPARE(m_overlay->destroyed(), 1);
    }

    void hideOnBuriedReleases()
    {
        showSettled();
        call(u"other"_s, u"Acquire"_s);
        QTRY_VERIFY(m_overlay->buried());
        m_overlay->hide();
        QVERIFY(!m_overlay->shown());
        QTRY_COMPARE(m_arbiter->stack(), QStringList{other()});
        QVERIFY(!m_window->handle());
        call(u"other"_s, u"Release"_s);
        QTest::qWait(100);
        QVERIFY(!m_window->handle());
    }

    void showOnBuriedRaises()
    {
        showSettled();
        call(u"other"_s, u"Acquire"_s);
        QTRY_VERIFY(m_overlay->buried());
        m_overlay->show();
        QTRY_COMPARE(m_arbiter->holder(), ours());
        QTRY_VERIFY(windowUp());
    }

    void arbiterRestartWithHolder()
    {
        showSettled();
        stopArbiter();
        QTRY_COMPARE(m_pad->holder(), QString());
        QVERIFY(m_overlay->shown());
        QVERIFY(windowUp());
        startArbiter();
        QTRY_COMPARE(m_arbiter->holder(), ours());
        QTRY_COMPARE(m_pad->holder(), ours());
        QVERIFY(windowUp());
        QCOMPARE(m_overlay->created(), 1);
    }

    void arbiterRestartWhileBuried()
    {
        showSettled();
        call(u"other"_s, u"Acquire"_s);
        QTRY_VERIFY(m_overlay->buried());
        stopArbiter();
        QTRY_VERIFY(!m_overlay->shown());
        startArbiter();
        QTest::qWait(200);
        QCOMPARE(m_arbiter->stack(), QStringList());
        QVERIFY(!m_overlay->shown());
        QVERIFY(!m_window->handle());
        m_overlay->toggle();
        QTRY_COMPARE(m_arbiter->holder(), ours());
        QTRY_VERIFY(windowUp());
    }

    void showDuringReleaseKeepsWindow()
    {
        showSettled();
        m_overlay->hide();
        QTRY_COMPARE(m_arbiter->holder(), QString());
        m_overlay->show();
        QTRY_COMPARE(m_arbiter->holder(), ours());
        QTRY_COMPARE(m_overlay->opacity(), 1.0);
        QTest::qWait(Overlay::HideMs + Overlay::ReleaseFrameMs + 100);
        QVERIFY(windowUp());
        QCOMPARE(m_overlay->created(), 1);
        QCOMPARE(m_overlay->destroyed(), 0);
    }

    void acquireFailureShowsPage()
    {
        stopArbiter();
        m_overlay->show();
        QVERIFY(windowUp());
        QTest::qWait(200);
        QVERIFY(m_overlay->shown());
        QVERIFY(!m_overlay->buried());
        m_overlay->toggle();
        QVERIFY(!m_overlay->shown());
        QTRY_VERIFY(!m_window->handle());
        startArbiter();
    }

    void releaseFailureRetries()
    {
        showSettled();
        m_arbiter->refuseRelease = true;
        m_overlay->hide();
        QTRY_VERIFY(!m_window->handle());
        QTest::qWait(300);
        QCOMPARE(m_arbiter->holder(), ours());
        m_arbiter->refuseRelease = false;
        QTRY_COMPARE_WITH_TIMEOUT(m_arbiter->holder(), QString(), DbusPad::ReleaseRetryMs + 1000);
        QCOMPARE(m_arbiter->releases, 1);
    }

    void cyclesStayStable()
    {
        showSettled();
        m_overlay->hide();
        QTRY_VERIFY(!m_window->handle());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        const qsizetype windows = QGuiApplication::allWindows().size();
        const qsizetype padChildren = m_pad->children().size();
        const qsizetype overlayChildren = m_overlay->children().size();
        for (int i = 0; i < 20; ++i) {
            showSettled();
            m_overlay->hide();
            QTRY_COMPARE(m_arbiter->holder(), QString());
            QTRY_VERIFY(!m_window->handle());
        }
        QTest::qWait(100);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCOMPARE(m_overlay->created(), 21);
        QCOMPARE(m_overlay->destroyed(), 21);
        QCOMPARE(QGuiApplication::allWindows().size(), windows);
        QCOMPARE(m_pad->children().size(), padChildren);
        QCOMPARE(m_overlay->children().size(), overlayChildren);
        QCOMPARE(m_arbiter->stack(), QStringList());
    }

    void sleepHidesThenReleasesInhibitor()
    {
        showSettled();
        m_logind->prepareForSleep(true);
        QTRY_VERIFY(!m_overlay->shown());
        QVERIFY(!m_window->handle());
        QTRY_COMPARE(m_arbiter->holder(), QString());
        QTRY_COMPARE(m_logind->open(), 0);
        m_logind->prepareForSleep(false);
        QTRY_COMPARE(m_logind->open(), 1);
        QVERIFY(!m_overlay->shown());
    }

    void sleepWhileHiddenReleasesAtOnce()
    {
        m_logind->prepareForSleep(true);
        QTRY_COMPARE(m_logind->open(), 0);
        m_logind->prepareForSleep(false);
        QTRY_COMPARE(m_logind->open(), 1);
    }

    void hintWindowWhileHidden()
    {
        QSignalSpy page(m_overlay.get(), &Overlay::pageHint);
        m_overlay->setHint(true, u"/dev/mmcblk0"_s);
        QVERIFY(m_overlay->hintShown());
        QCOMPARE(m_overlay->hintDevice(), u"/dev/mmcblk0"_s);
        QCOMPARE(m_events, (QStringList{u"create toast"_s, u"lower"_s}));
        QVERIFY(m_toast->isVisible());
        QCOMPARE(page.count(), 0);
        QSignalSpy unused(m_overlay.get(), &Overlay::unused);
        m_overlay->setHint(false);
        QVERIFY(!m_overlay->hint());
        QTest::qWait(50);
        QCOMPARE(unused.count(), 0);
        m_overlay->hintDone();
        QTRY_COMPARE(unused.count(), 1);
        QVERIFY(!m_overlay->hintShown());
        QVERIFY(!m_toast->handle());
        m_overlay->setHint(true);
        QVERIFY(m_overlay->hintShown());
        showSettled();
        QVERIFY(!m_toast->handle());
        QVERIFY(!m_overlay->hintShown());
        QCOMPARE(page.count(), 1);
        m_overlay->hide();
        QTRY_VERIFY(!m_window->handle());
        QVERIFY(m_overlay->hintShown());
        QVERIFY(m_toast->isVisible());
        QCOMPARE(page.count(), 1);
    }

    void hintInPageWhileShown()
    {
        showSettled();
        QSignalSpy page(m_overlay.get(), &Overlay::pageHint);
        m_overlay->setHint(true);
        QCOMPARE(page.count(), 1);
        QVERIFY(!m_overlay->hintShown());
        call(u"other"_s, u"Acquire"_s);
        QTRY_VERIFY(m_overlay->buried());
        QVERIFY(m_overlay->hintShown());
        m_overlay->toggle();
        QTRY_VERIFY(windowUp());
        QVERIFY(!m_overlay->hintShown());
        QCOMPARE(page.count(), 2);
    }

    void hintShownWhileAnotherHolds()
    {
        call(u"other"_s, u"Acquire"_s);
        QTRY_COMPARE(m_pad->holder(), other());
        m_events.clear();
        m_overlay->setHint(true, u"/dev/mmcblk0"_s);
        QVERIFY(m_overlay->hintShown());
        QCOMPARE(m_events, (QStringList{u"create toast"_s, u"lower"_s}));
        call(u"other"_s, u"Release"_s);
    }

    void hintFollowsTheFormatProcess()
    {
        const QString dir = m_h.path(u"hold"_s);
        QDir().mkpath(dir);
        const QString proc = m_h.path(u"hintproc"_s);
        m_h.writeFile(u"hintproc/4242/cmdline"_s,
                      cmdline({"bash", "/usr/libexec/microos/microos-storage", "format", "--device", "/dev/mmcblk0"}));
        m_h.writeFile(u"hintproc/4343/cmdline"_s, cmdline({"bash", "/usr/libexec/microos/microos-storage", "trim"}));
        m_h.writeFile(u"hintproc/4444/cmdline"_s, cmdline({"/tmp/microos-storage", "format"}));
        m_h.writeFile(u"hintproc/4646/cmdline"_s,
                      cmdline({"bash", "/usr/libexec/microos/microos-storage", "format", "--device", "/dev/mmcblk0"}));
        QVERIFY(::chown(QFile::encodeName(proc + u"/4646"_s).constData(), 65534, 65534) == 0);
        HoldHint hint(dir, proc);
        QSignalSpy changed(&hint, &HoldHint::activeChanged);
        QVERIFY(!hint.active());
        m_h.writeFile(u"hold/storage"_s, "4242\n/dev/mmcblk0\n");
        QTRY_VERIFY(hint.active());
        QCOMPARE(hint.device(), u"/dev/mmcblk0"_s);
        QFile::remove(dir + u"/storage"_s);
        QTRY_VERIFY(!hint.active());
        QCOMPARE(hint.device(), QString());
        m_h.writeFile(u"hold/storage"_s, "");
        m_h.writeFile(u"hold/storage"_s, "4242\n");
        QTRY_VERIFY(hint.active());
        QCOMPARE(hint.device(), QString());
        m_h.writeFile(u"hold/storage"_s, "4242\n/dev/sda\n");
        QTest::qWait(200);
        QVERIFY(hint.active());
        QCOMPARE(hint.device(), QString());
        m_h.writeFile(u"hold/storage"_s, "4343\n/dev/mmcblk0\n");
        QTRY_VERIFY(!hint.active());
        const int before = int(changed.count());
        for (const QByteArray &pid : {QByteArray("4444"), QByteArray("4545"), QByteArray("4646")}) {
            m_h.writeFile(u"hold/storage"_s, pid + "\n/dev/mmcblk0\n");
            QTest::qWait(200);
            QVERIFY2(!hint.active(), pid.constData());
        }
        QCOMPARE(changed.count(), before);
        QFile::remove(dir + u"/storage"_s);
        m_h.writeFile(u"hold/storage"_s, "4242\n/dev/mmcblk1\n");
        QTRY_VERIFY(hint.active());
        HoldHint late(dir, proc);
        QVERIFY(late.active());
        QCOMPARE(late.device(), u"/dev/mmcblk1"_s);
    }

    void hintFollowsPowerdUndervolt()
    {
        const QString dir = m_h.path(u"hold-uv"_s);
        QDir().mkpath(dir);
        const QString proc = m_h.path(u"hintproc-uv"_s);
        m_h.writeFile(u"hintproc-uv/5252/cmdline"_s, cmdline({"python3", "/usr/libexec/microos/microos-powerd"}));
        m_h.writeFile(u"hintproc-uv/5353/cmdline"_s, cmdline({"python3", "/tmp/microos-powerd"}));
        m_h.writeFile(u"hintproc-uv/5454/cmdline"_s, cmdline({"python3", "/usr/libexec/microos/microos-powerd"}));
        QVERIFY(::chown(QFile::encodeName(proc + u"/5454"_s).constData(), 65534, 65534) == 0);
        m_h.writeFile(u"hintproc-uv/4242/cmdline"_s,
                      cmdline({"bash", "/usr/libexec/microos/microos-storage", "format", "--device", "/dev/mmcblk0"}));
        HoldHint hint(dir, proc);
        QVERIFY(!hint.active());
        m_h.writeFile(u"hold-uv/gpu-undervolt"_s, "5252\ngpu-undervolt\n");
        QTRY_VERIFY(hint.active());
        QCOMPARE(hint.kind(), u"undervolt"_s);
        QCOMPARE(hint.device(), QString());
        m_h.writeFile(u"hold-uv/storage"_s, "4242\n/dev/mmcblk0\n");
        QTRY_COMPARE(hint.kind(), u"format"_s);
        QCOMPARE(hint.device(), u"/dev/mmcblk0"_s);
        QFile::remove(dir + u"/storage"_s);
        QTRY_COMPARE(hint.kind(), u"undervolt"_s);
        for (const QByteArray &text : {QByteArray("5252\nstorage\n"), QByteArray("5353\ngpu-undervolt\n"),
                                       QByteArray("5454\ngpu-undervolt\n"), QByteArray("5555\ngpu-undervolt\n")}) {
            m_h.writeFile(u"hold-uv/gpu-undervolt"_s, text);
            QTRY_VERIFY2(!hint.active(), text.constData());
            m_h.writeFile(u"hold-uv/gpu-undervolt"_s, "5252\ngpu-undervolt\n");
            QTRY_VERIFY(hint.active());
        }
        QFile::remove(dir + u"/gpu-undervolt"_s);
        QTRY_VERIFY(!hint.active());
        QCOMPARE(hint.kind(), QString());
    }

    void serviceRefusesGames()
    {
        Auth auth(bus(u"other"_s), m_h.path(u"proc"_s));
        OverlayService service(m_overlay.get(), auth);
        QVERIFY(service.publish(bus(u"pad-session"_s)));
        QDBusConnection caller = bus(u"caller"_s);
        const auto wait = [&caller](const QDBusMessage &message) {
            QDBusPendingCall pending = caller.asyncCall(message);
            if (!QTest::qWaitFor([&pending] { return pending.isFinished(); }, 5000))
                return QDBusMessage();
            return pending.reply();
        };
        const auto send = [&wait](const QString &method) {
            return wait(QDBusMessage::createMethodCall(OverlayService::service(), OverlayService::path(),
                                                       OverlayService::interface(), method));
        };
        m_h.writeCgroup(QCoreApplication::applicationPid(), Harness::GameCgroup);
        QDBusMessage reply = send(u"Toggle"_s);
        QCOMPARE(reply.errorName(), u"org.freedesktop.DBus.Error.AccessDenied"_s);
        QVERIFY(!m_overlay->shown());
        m_h.writeCgroup(QCoreApplication::applicationPid(), Harness::EmuCgroup);
        QCOMPARE(send(u"Show"_s).errorName(), u"org.freedesktop.DBus.Error.AccessDenied"_s);
        QVERIFY(!m_overlay->shown());
        m_h.writeCgroup(QCoreApplication::applicationPid(), Harness::UserCgroup);
        QVERIFY(caller.connect(OverlayService::service(), OverlayService::path(), u"org.freedesktop.DBus.Properties"_s,
                               u"PropertiesChanged"_s, this, SLOT(onProps(QString,QVariantMap,QStringList))));
        m_props.clear();
        reply = send(u"Toggle"_s);
        QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
        QVERIFY(m_overlay->shown());
        QTRY_COMPARE(m_props, (QVariantList{true}));
        QDBusMessage get = QDBusMessage::createMethodCall(OverlayService::service(), OverlayService::path(),
                                                          u"org.freedesktop.DBus.Properties"_s, u"Get"_s);
        get << OverlayService::interface() << u"Shown"_s;
        QCOMPARE(wait(get).arguments().value(0).value<QDBusVariant>().variant(), QVariant(true));
        QCOMPARE(send(u"Hide"_s).type(), QDBusMessage::ReplyMessage);
        QVERIFY(!m_overlay->shown());
        QTRY_COMPARE(m_props, (QVariantList{true, false}));
        m_h.writeCgroup(QCoreApplication::applicationPid(), Harness::GameCgroup);
        QCOMPARE(send(u"Hide"_s).errorName(), u"org.freedesktop.DBus.Error.AccessDenied"_s);
        QDBusConnection::disconnectFromBus(u"caller"_s);
        bus(u"pad-session"_s).unregisterObject(OverlayService::path());
        bus(u"pad-session"_s).unregisterService(OverlayService::service());
    }

    void runningGame()
    {
        QDBusConnection manager = bus(u"systemd"_s);
        FakeSystemd systemd(manager);
        QVERIFY(manager.registerVirtualObject(FakeSystemd::Path, &systemd, QDBusConnection::SubPath));
        QVERIFY(manager.registerService(u"org.freedesktop.systemd1"_s));
        const QString proton = m_h.path(u"common/Proton 11.0 (ARM64)"_s);
        const QString runtime = m_h.path(u"common/SteamLinuxRuntime_4-arm64"_s);
        m_h.writeFile(u"common/Proton 11.0 (ARM64)/proton"_s, "#!/usr/bin/env python3\n");
        m_h.writeFile(u"common/SteamLinuxRuntime_4-arm64/_v2-entry-point"_s, "#!/bin/sh\n");
        const QByteArray tools =
            "STEAM_COMPAT_TOOL_PATHS=" + QFile::encodeName(proton) + ':' + QFile::encodeName(runtime);
        writeEnviron(11, {"SteamGameId=292120", tools});
        writeEnviron(12, {"SteamGameId=292120", tools, "STEAM_COMPAT_FEX_CONFIG=", "MICROOS_PROTON_HOOK=ok"});
        systemd.units = {{u"app-steam-app292120-100.scope"_s, RunningGame::Slice, 100, {10, 11, 12}}};
        RunningGame game(bus(u"game"_s), m_h.path(u"proc"_s));
        game.refresh();
        QTRY_COMPARE(game.info().value(u"appid"_s).toString(), u"292120"_s);
        QVariantMap info = game.info();
        QCOMPARE(info.value(u"unit"_s).toString(), u"app-steam-app292120-100.scope"_s);
        QCOMPARE(info.value(u"hook"_s).toString(), u"ok"_s);
        QCOMPARE(info.value(u"tool"_s).toString(), u"Proton 11.0 (ARM64)"_s);
        QVERIFY(info.value(u"proton"_s).toBool());
        QVERIFY(!info.value(u"steamFex"_s).toBool());
        QCOMPARE(info.value(u"profile"_s).toString(), QString());
        QVERIFY(systemd.calls.contains(u"Subscribe"_s));

        writeEnviron(20, {tools, "MICROOS_PROTON_HOOK=bad-local,no-fex-profiles", "MICROOS_PROTON_PROFILE=other-tool",
                     "MICROOS_PROTON_YIELDED=FEX_TSOENABLED", "STEAM_COMPAT_FEX_CONFIG=TSOEnabled"});
        systemd.units << FakeSystemd::Unit{u"app-steam-app2238900-200.scope"_s, RunningGame::Slice, 300, {20}}
                      << FakeSystemd::Unit{u"app-steam-app1145360-300.scope"_s, u"app.slice"_s, 400, {20}}
                      << FakeSystemd::Unit{u"app-steam-app0-1.scope"_s, RunningGame::Slice, 500, {20}}
                      << FakeSystemd::Unit{u"app-steam-appX-1.scope"_s, RunningGame::Slice, 500, {20}};
        game.refresh();
        QTRY_COMPARE(game.info().value(u"appid"_s).toString(), u"2238900"_s);
        info = game.info();
        QCOMPARE(info.value(u"hook"_s).toString(), u"bad-local,no-fex-profiles"_s);
        QCOMPARE(info.value(u"profile"_s).toString(), u"other-tool"_s);
        QCOMPARE(info.value(u"yielded"_s).toString(), u"FEX_TSOENABLED"_s);
        QVERIFY(info.value(u"steamFex"_s).toBool());

        writeEnviron(20, {tools});
        game.refresh();
        QTRY_COMPARE(game.info().value(u"hook"_s).toString(), QString());
        QVERIFY(game.info().value(u"proton"_s).toBool());
        QCOMPARE(game.info().value(u"tool"_s).toString(), u"Proton 11.0 (ARM64)"_s);
        writeEnviron(20, {"SteamGameId=2238900"});
        game.refresh();
        QTRY_VERIFY(!game.info().value(u"proton"_s).toBool());
        QCOMPARE(game.info().value(u"tool"_s).toString(), QString());
        writeEnviron(20, {tools});
        game.refresh();
        QTRY_VERIFY(game.info().value(u"proton"_s).toBool());
        writeEnviron(20, {"STEAM_COMPAT_TOOL_PATHS=" + QFile::encodeName(runtime)});
        game.refresh();
        QTRY_VERIFY(!game.info().value(u"proton"_s).toBool());
        QCOMPARE(game.info().value(u"tool"_s).toString(), QString());
        m_h.writeFile(u"common/linked/target"_s, "#!/usr/bin/env python3\n");
        QVERIFY(QFile::link(m_h.path(u"common/linked/target"_s), m_h.path(u"common/linked/proton"_s)));
        writeEnviron(20, {tools});
        game.refresh();
        QTRY_VERIFY(game.info().value(u"proton"_s).toBool());
        writeEnviron(20, {"STEAM_COMPAT_TOOL_PATHS=" + QFile::encodeName(m_h.path(u"common/linked"_s))});
        game.refresh();
        QTRY_VERIFY(!game.info().value(u"proton"_s).toBool());
        writeEnviron(20, {tools});
        game.refresh();
        QTRY_VERIFY(game.info().value(u"proton"_s).toBool());
        writeEnviron(20, {"STEAM_COMPAT_TOOL_PATHS=common/Proton 11.0 (ARM64)"});
        game.refresh();
        QTRY_VERIFY(!game.info().value(u"proton"_s).toBool());
        writeEnviron(20, {tools, "MICROOS_PROTON_HOOK=ok", QByteArray("X=") + QByteArray(RunningGame::MaxEnviron, 'x')});
        game.refresh();
        QTest::qWait(200);
        QCOMPARE(game.info().value(u"hook"_s).toString(), QString());

        writeEnviron(20, {"STEAM_COMPAT_TOOL_PATHS=", "MICROOS_PROTON_HOOK=ok"});
        game.refresh();
        QTRY_COMPARE(game.info().value(u"hook"_s).toString(), u"ok"_s);
        QVERIFY(!game.info().value(u"proton"_s).toBool());

        QSignalSpy changed(&game, &RunningGame::changed);
        systemd.removed(u"app-steam-app292120-100.scope"_s);
        systemd.state(0, u"failed"_s);
        QTest::qWait(200);
        QCOMPARE(changed.count(), 0);
        systemd.state(1, u"active"_s);
        QTest::qWait(200);
        QCOMPARE(changed.count(), 0);
        systemd.removed(u"app-steam-app2238900-200.scope"_s);
        QTRY_VERIFY(game.info().value(u"gone"_s).toBool());
        QCOMPARE(game.info().value(u"appid"_s).toString(), u"2238900"_s);
        game.refresh();
        QTRY_VERIFY(!game.info().value(u"gone"_s).toBool());
        systemd.state(1, u"failed"_s);
        QTRY_VERIFY(game.info().value(u"gone"_s).toBool());

        game.skip({u"2238900"_s});
        game.refresh();
        QTRY_COMPARE(game.info().value(u"appid"_s).toString(), u"292120"_s);
        systemd.fail = true;
        game.refresh();
        QTRY_VERIFY(game.info().isEmpty());
        systemd.fail = false;
        game.refresh();
        QTRY_COMPARE(game.info().value(u"appid"_s).toString(), u"292120"_s);
        systemd.units.clear();
        game.refresh();
        QTRY_VERIFY(game.info().isEmpty());
        QCOMPARE(systemd.calls.count(u"Subscribe"_s), 1);
        manager.unregisterObject(FakeSystemd::Path, QDBusConnection::UnregisterTree);
        manager.unregisterService(u"org.freedesktop.systemd1"_s);
        QDBusConnection::disconnectFromBus(u"systemd"_s);
        QDBusConnection::disconnectFromBus(u"game"_s);
    }

    static qulonglong nowUs()
    {
        timespec now{};
        ::clock_gettime(CLOCK_MONOTONIC, &now);
        return qulonglong(now.tv_sec) * 1000000ULL + qulonglong(now.tv_nsec) / 1000ULL;
    }

    void runningGameLsfg()
    {
        QDBusConnection manager = bus(u"systemd"_s);
        FakeSystemd systemd(manager);
        QVERIFY(manager.registerVirtualObject(FakeSystemd::Path, &systemd, QDBusConnection::SubPath));
        QVERIFY(manager.registerService(u"org.freedesktop.systemd1"_s));
        const QString active = m_h.path(u"lsfg-active"_s);
        QVERIFY(QDir().mkpath(active));
        writeEnviron(41, {"SteamGameId=292120", "MICROOS_LSFG=check-failed"});
        writeEnviron(42, {"SteamGameId=292120", "MICROOS_LSFG=on"});
        systemd.units = {{u"app-steam-app292120-400.scope"_s, RunningGame::Slice, 100, {41, 42}}};
        RunningGame game(bus(u"game"_s), m_h.path(u"proc"_s));
        game.setSignalDir(active);
        const auto again = [&] {
            game.refresh();
            QTest::qWait(200);
            return game.info();
        };
        game.refresh();
        QTRY_COMPARE(game.info().value(u"lsfg"_s).toString(), u"check-failed"_s);
        QVERIFY(!game.info().contains(u"lsfgHeld"_s));
        QCOMPARE(game.info().value(u"steamGameId"_s).toString(), u"292120"_s);
        writeEnviron(41, {"SteamGameId=292120"});
        QVariantMap info = again();
        QCOMPARE(info.value(u"lsfg"_s).toString(), u"on"_s);
        QCOMPARE(info.value(u"lsfgHeld"_s), QVariant(false));
        QCOMPARE(info.value(u"lsfgYoung"_s), QVariant(false));

        const auto lock = [&](const QString &name) {
            auto file = std::make_unique<QFile>(active + u'/' + name);
            if (!file->open(QIODevice::WriteOnly) || ::flock(file->handle(), LOCK_EX | LOCK_NB) != 0)
                return std::unique_ptr<QFile>();
            return file;
        };
        auto other = lock(u"2238900-5"_s);
        auto odd = lock(u"292120-x"_s);
        QVERIFY(other && odd);
        QCOMPARE(again().value(u"lsfgHeld"_s), QVariant(false));
        m_h.writeFile(u"lsfg-active/292120-6"_s, "");
        QCOMPARE(again().value(u"lsfgHeld"_s), QVariant(false));
        auto held = lock(u"292120-77"_s);
        QVERIFY(held);
        QCOMPARE(again().value(u"lsfgHeld"_s), QVariant(true));
        ::flock(held->handle(), LOCK_UN);
        QCOMPARE(again().value(u"lsfgHeld"_s), QVariant(false));
        ::flock(held->handle(), LOCK_EX);
        writeEnviron(42, {"SteamGameId=292120", "MICROOS_LSFG=cap-overridden"});
        info = again();
        QCOMPARE(info.value(u"lsfg"_s).toString(), u"cap-overridden"_s);
        QCOMPARE(info.value(u"lsfgHeld"_s), QVariant(true));

        writeEnviron(51, {"SteamGameId=11868105691086454784", "MICROOS_LSFG=on", "MICROOS_PROTON_HOOK=ok"});
        systemd.units = {{u"app-steam-app2763258687-402.scope"_s, RunningGame::Slice, 100, {51}}};
        info = again();
        QCOMPARE(info.value(u"appid"_s).toString(), u"2763258687"_s);
        QCOMPARE(info.value(u"steamGameId"_s).toString(), u"11868105691086454784"_s);
        QCOMPARE(info.value(u"lsfgHeld"_s), QVariant(false));
        auto shortcut = lock(u"11868105691086454784-77"_s);
        auto scope = lock(u"2763258687-78"_s);
        QVERIFY(shortcut && scope);
        info = again();
        QCOMPARE(info.value(u"lsfgHeld"_s), QVariant(true));
        auto model = std::make_unique<SettingsModel>();
        model->setFixture(FakeMicroSetd::describe({FakeMicroSetd::shipped()}), {}, {});
        model->lsfg()->setFixture(QJsonObject{{u"owned"_s, u"yes"_s}, {u"install"_s, u"installed"_s}}, true, u"steam"_s);
        model->setGame(info);
        QCOMPARE(rowOf(model->groups(0), u"game-lsfg"_s).value(u"value"_s).toString(), u"on"_s);
        shortcut.reset();
        info = again();
        QCOMPARE(info.value(u"lsfgHeld"_s), QVariant(false));
        model->setGame(info);
        QCOMPARE(rowOf(model->groups(0), u"game-lsfg"_s).value(u"value"_s).toString(), u"failed to start"_s);
        for (const QByteArray &id : {"SteamGameId=1186810569108645478x"_ba, "SteamGameId=118681056910864547840"_ba,
                                     "SteamGameId=-11868105691086454784"_ba, "SteamGameId="_ba, "Other=1"_ba}) {
            writeEnviron(51, {id, "MICROOS_LSFG=on"});
            info = again();
            QVERIFY2(info.value(u"steamGameId"_s).toString().isEmpty(), id.constData());
            QVERIFY2(info.value(u"lsfgHeld"_s).toBool(), id.constData());
        }
        scope.reset();
        QCOMPARE(again().value(u"lsfgHeld"_s), QVariant(false));

        held.reset();
        systemd.units = {{u"app-steam-app292120-401.scope"_s, RunningGame::Slice, nowUs() - 9700000, {41, 42}}};
        game.refresh();
        QTRY_COMPARE(game.info().value(u"unit"_s).toString(), u"app-steam-app292120-401.scope"_s);
        QCOMPARE(game.info().value(u"lsfgYoung"_s), QVariant(true));
        const int calls = systemd.calls.count(u"ListUnitsByPatterns"_s);
        QTRY_COMPARE_WITH_TIMEOUT(game.info().value(u"lsfgYoung"_s), QVariant(false), 3000);
        QVERIFY(systemd.calls.count(u"ListUnitsByPatterns"_s) > calls);

        writeEnviron(41, {"SteamGameId=292120"});
        writeEnviron(42, {"SteamGameId=292120"});
        info = again();
        QCOMPARE(info.value(u"lsfg"_s).toString(), QString());
        QVERIFY(!info.contains(u"lsfgHeld"_s));
        manager.unregisterObject(FakeSystemd::Path, QDBusConnection::UnregisterTree);
        manager.unregisterService(u"org.freedesktop.systemd1"_s);
        QDBusConnection::disconnectFromBus(u"systemd"_s);
        QDBusConnection::disconnectFromBus(u"game"_s);
    }

    void frameLimitInfo()
    {
        const QVariantMap game{{u"appid"_s, u"2238900"_s}};
        const auto plus = [&](const QVariantMap &more) {
            QVariantMap out = game;
            out.insert(more);
            return out;
        };
        QCOMPARE(withFrameLimit({}, 30, 60), QVariantMap());
        QCOMPARE(withFrameLimit(game, -1, -1), game);
        QCOMPARE(withFrameLimit(game, 0, 0), plus({{u"fpsLimit"_s, 0}, {u"refreshHz"_s, 0}}));
        QCOMPARE(withFrameLimit(game, 30, -1), plus({{u"fpsLimit"_s, 30}}));
        QCOMPARE(withFrameLimit(game, -1, 60), plus({{u"refreshHz"_s, 60}}));
        QCOMPARE(withFrameLimit(game, 30, 60), plus({{u"fpsLimit"_s, 30}, {u"refreshHz"_s, 60}}));
    }

    void thisGameLsfgRows()
    {
        QTemporaryDir steam;
        qputenv("MICROSET_STEAM_DIR", steam.path().toUtf8());
        FakeMicroSetd::writeSteam(steam.path(), {{u"292120"_s, u"FINAL FANTASY XIII"_s}});
        const QStringList files{FakeMicroSetd::shipped()};
        auto model = std::make_unique<SettingsModel>();
        model->setFixture(FakeMicroSetd::describe(files), {}, {});
        model->setProtonFixture({{u"292120"_s, FakeMicroSetd::effective(files, {}, FakeMicroSetd::Profiles, u"292120"_s)}});
        const QVariantMap ok{{u"appid"_s, u"292120"_s}, {u"unit"_s, u"app-steam-app292120-100.scope"_s}, {u"hook"_s, u"ok"_s},
                             {u"tool"_s, u"Proton 11.0 (ARM64)"_s}, {u"proton"_s, true}, {u"steamFex"_s, false},
                             {u"lsfg"_s, u"on"_s}, {u"lsfgHeld"_s, true}, {u"lsfgYoung"_s, false}};
        const auto shown = [&](const QVariantMap &change) {
            QVariantMap game = ok;
            game.insert(change);
            model->setGame(game);
            return model->groups(0);
        };
        QVERIFY(!keysOf(shown({})).contains(u"game-lsfg"_s));
        model->lsfg()->setFixture(QJsonObject{{u"owned"_s, u"yes"_s}, {u"install"_s, u"installed"_s}}, true, u"steam"_s);
        QVariantList rows = shown({});
        QCOMPARE(keysOf(rows).mid(0, 5), (QStringList{u"label-this game"_s, u"game-292120"_s, u"game-profile"_s,
                                                      u"game-lsfg"_s, u"label-microset"_s}));
        QCOMPARE(rowOf(rows, u"game-lsfg"_s).value(u"kind"_s).toString(), u"info"_s);
        QCOMPARE(rowOf(rows, u"game-lsfg"_s).value(u"title"_s).toString(), u"Frame generation"_s);
        const auto text = [&](const QVariantMap &change) { return rowOf(shown(change), u"game-lsfg"_s).value(u"value"_s).toString(); };
        QCOMPARE(text({}), u"on"_s);
        QCOMPARE(text({{u"lsfg"_s, u"off"_s}}), u"off"_s);
        QCOMPARE(text({{u"lsfg"_s, u"no-dll"_s}}), u"Lossless.dll not found"_s);
        QCOMPARE(text({{u"lsfg"_s, u"disabled-by-env"_s}}), u"off by launch option"_s);
        QCOMPARE(text({{u"lsfg"_s, u"check-failed"_s}}), u"check failed"_s);
        QCOMPARE(text({{u"lsfg"_s, u"cap-overridden"_s}}), u"on, cap by launch option"_s);
        QCOMPARE(text({{u"lsfgHeld"_s, false}, {u"lsfgYoung"_s, true}}), u"not running yet"_s);
        QCOMPARE(text({{u"lsfgHeld"_s, false}}), u"failed to start"_s);
        QCOMPARE(text({{u"lsfg"_s, u"cap-overridden"_s}, {u"lsfgHeld"_s, false}}), u"failed to start"_s);
        QCOMPARE(text({{u"lsfgYoung"_s, true}}), u"on"_s);
        QCOMPARE(text({{u"lsfg"_s, u"cap-overridden"_s}, {u"lsfgYoung"_s, true}}), u"on, cap by launch option"_s);
        QCOMPARE(text({{u"lsfg"_s, u"<b>new</b>"_s}}), u"<b>new</b>"_s);
        QVERIFY(!keysOf(shown({{u"gone"_s, true}})).contains(u"game-lsfg"_s));
        QVERIFY(!keysOf(shown({{u"lsfg"_s, QString()}})).contains(u"game-lsfg"_s));
        rows = shown({{u"hook"_s, u"no-fex-profiles"_s}, {u"yielded"_s, u"FEX_TSOENABLED"_s}});
        QCOMPARE(keysOf(rows).mid(2, 4),
                 (QStringList{u"game-state"_s, u"game-profile"_s, u"game-yield"_s, u"game-lsfg"_s}));

        const auto warned = [&](const QVariantMap &change) { return keysOf(shown(change)).contains(u"game-lsfg-limit"_s); };
        const auto limited = [](int limit, int refresh, QVariantMap more = {}) {
            more.insert(u"fpsLimit"_s, limit);
            more.insert(u"refreshHz"_s, refresh);
            return more;
        };
        QVERIFY(!warned({}));
        QVERIFY(warned(limited(30, 60)));
        QVERIFY(!warned(limited(60, 60)));
        QVERIFY(!warned(limited(0, 60)));
        QVERIFY(!warned(limited(30, 30)));
        QVERIFY(!warned(limited(90, 60)));
        QVERIFY(warned(limited(45, 120)));
        QVERIFY(!warned({{u"fpsLimit"_s, 30}}));
        QVERIFY(!warned({{u"refreshHz"_s, 60}}));
        QVERIFY(!warned(limited(30, 60, {{u"lsfgHeld"_s, false}})));
        QVERIFY(warned(limited(30, 60, {{u"lsfg"_s, u"cap-overridden"_s}})));
        rows = shown(limited(30, 60));
        QCOMPARE(keysOf(rows).at(keysOf(rows).indexOf(u"game-lsfg"_s) + 1), u"game-lsfg-limit"_s);
        QCOMPARE(rowOf(rows, u"game-lsfg-limit"_s).value(u"title"_s).toString(), u"Steam frame limit"_s);
        QCOMPARE(rowOf(rows, u"game-lsfg-limit"_s).value(u"value"_s).toString(), u"on: counts generated frames"_s);
        model->lsfg()->setFixture({}, false, QString());
        QVERIFY(!keysOf(shown(limited(30, 60))).contains(u"game-lsfg"_s));
        qunsetenv("MICROSET_STEAM_DIR");
    }

    void thisGameRows()
    {
        QTemporaryDir steam;
        qputenv("MICROSET_STEAM_DIR", steam.path().toUtf8());
        FakeMicroSetd::writeSteam(steam.path(), {{u"292120"_s, u"FINAL FANTASY XIII"_s}, {u"2238900"_s, u"SO2R"_s}});
        const QStringList files{FakeMicroSetd::shipped()};
        const auto made = [&](const QVariantMap &stored) {
            auto model = std::make_unique<SettingsModel>();
            model->setFixture(FakeMicroSetd::describe(files), stored, {});
            QMap<QString, QJsonObject> effective;
            for (const QString &appid : {u"292120"_s, u"2238900"_s})
                effective.insert(appid, FakeMicroSetd::effective(files, stored, FakeMicroSetd::Profiles, appid));
            model->setProtonFixture(effective);
            return model;
        };
        const auto model = made({});
        QCOMPARE(keysOf(model->groups(0)).first(), u"label-microset"_s);
        const QVariantMap ok{{u"appid"_s, u"292120"_s}, {u"unit"_s, u"app-steam-app292120-100.scope"_s}, {u"hook"_s, u"ok"_s},
                             {u"tool"_s, u"Proton 11.0 (ARM64)"_s}, {u"proton"_s, true}, {u"steamFex"_s, false}};
        model->setGame(ok);
        QVariantList rows = model->groups(0);
        QCOMPARE(keysOf(rows).mid(0, 5), (QStringList{u"label-this game"_s, u"game-292120"_s, u"game-profile"_s,
                                                      u"label-microset"_s, u"group-audio"_s}));
        QCOMPARE(rowOf(rows, u"label-this game"_s).value(u"kind"_s).toString(), u"label"_s);
        QCOMPARE(rowOf(rows, u"label-this game"_s).value(u"title"_s).toString(), u"this game"_s);
        const QVariantMap row = rowOf(rows, u"game-292120"_s);
        QCOMPARE(row.value(u"kind"_s).toString(), u"row"_s);
        QCOMPARE(row.value(u"title"_s).toString(), u"FINAL FANTASY XIII"_s);
        QCOMPARE(row.value(u"group"_s).toString(), u"app.292120"_s);
        QCOMPARE(row.value(u"value"_s).toString(), QString());
        QCOMPARE(rowOf(rows, u"game-profile"_s).value(u"kind"_s).toString(), u"info"_s);
        QCOMPARE(rowOf(rows, u"game-profile"_s).value(u"title"_s).toString(), u"MicroOS profile"_s);
        QCOMPARE(rowOf(rows, u"game-profile"_s).value(u"value"_s).toString(), u"on"_s);

        const auto shown = [&](const QVariantMap &change) {
            QVariantMap game = ok;
            game.insert(change);
            model->setGame(game);
            return model->groups(0);
        };
        rows = shown({{u"hook"_s, QString()}});
        QCOMPARE(rowOf(rows, u"game-missing"_s).value(u"title"_s).toString(), u"hook missing in"_s);
        QCOMPARE(rowOf(rows, u"game-missing"_s).value(u"value"_s).toString(), u"Proton 11.0 (ARM64)"_s);
        QVERIFY(!keysOf(rows).contains(u"game-state"_s));
        QVERIFY(!keysOf(rows).contains(u"game-profile"_s));
        rows = shown({{u"hook"_s, u"bad-local,no-fex-profiles"_s}});
        QCOMPARE(rowOf(rows, u"game-state"_s).value(u"title"_s).toString(), u"settings not reaching the game"_s);
        QCOMPARE(rowOf(rows, u"game-state"_s).value(u"value"_s).toString(), u"bad-local, no-fex-profiles"_s);
        rows = shown({{u"hook"_s, u"ok"_s}, {u"profile"_s, u"other-tool"_s}});
        QCOMPARE(rowOf(rows, u"game-profile"_s).value(u"title"_s).toString(), u"profile not for this Proton"_s);
        QCOMPARE(rowOf(rows, u"game-profile"_s).value(u"value"_s).toString(), QString());
        rows = shown({{u"hook"_s, u"no-fex-profiles"_s}, {u"yielded"_s, u"FEX_TSOENABLED,FEX_MULTIBLOCK"_s}});
        QCOMPARE(keysOf(rows).mid(0, 5), (QStringList{u"label-this game"_s, u"game-292120"_s, u"game-state"_s,
                                                      u"game-profile"_s, u"game-yield"_s}));
        QCOMPARE(rowOf(rows, u"game-yield"_s).value(u"title"_s).toString(), u"profile gives way to Steam"_s);
        QCOMPARE(rowOf(rows, u"game-yield"_s).value(u"value"_s).toString(), u"FEX_TSOENABLED, FEX_MULTIBLOCK"_s);
        rows = shown({{u"gone"_s, true}});
        QCOMPARE(rowOf(rows, u"game-state"_s).value(u"value"_s).toString(), u"unknown"_s);
        QVERIFY(!keysOf(rows).contains(u"game-profile"_s));
        rows = shown({{u"gone"_s, true}, {u"profile"_s, u"other-tool"_s}});
        QVERIFY(!keysOf(rows).contains(u"game-profile"_s));
        QCOMPARE(rowOf(rows, u"game-292120"_s).value(u"title"_s).toString(), u"FINAL FANTASY XIII"_s);
        rows = shown({{u"gone"_s, true}, {u"yielded"_s, u"FEX_TSOENABLED"_s}});
        QVERIFY(!keysOf(rows).contains(u"game-yield"_s));
        rows = shown({{u"appid"_s, u"2238900"_s}, {u"hook"_s, QString()}, {u"tool"_s, QString()}, {u"proton"_s, false}});
        QCOMPARE(keysOf(rows).mid(0, 3), (QStringList{u"label-this game"_s, u"game-2238900"_s, u"label-microset"_s}));
        rows = shown({{u"appid"_s, u"2238900"_s}});
        QCOMPARE(keysOf(rows).mid(0, 3), (QStringList{u"label-this game"_s, u"game-2238900"_s, u"label-microset"_s}));

        model->setGame(ok);
        model->enter(u"app.292120"_s);
        QVERIFY(!keysOf(model->rows(u"app.292120"_s, 0)).contains(u"game-steam"_s));
        rows = shown({{u"steamFex"_s, true}});
        const QVariantList page = model->rows(u"app.292120"_s, 0);
        QCOMPARE(keysOf(page).mid(keysOf(page).size() - 2), (QStringList{u"game-next"_s, u"game-steam"_s}));
        QCOMPARE(rowOf(page, u"game-steam"_s).value(u"title"_s).toString(), u"Set in Steam"_s);
        QCOMPARE(rowOf(page, u"game-steam"_s).value(u"value"_s).toString(), u"TSO, Multiblock"_s);
        model->enter(u"app.2238900"_s);
        QVERIFY(!keysOf(model->rows(u"app.2238900"_s, 0)).contains(u"game-steam"_s));
        model->setGame({});
        QCOMPARE(keysOf(model->groups(0)).first(), u"label-microset"_s);

        const auto off = made({{u"app.292120.profile"_s, u"off"_s}});
        off->setGame(ok);
        QCOMPARE(rowOf(off->groups(0), u"game-profile"_s).value(u"value"_s).toString(), u"off"_s);
        qunsetenv("MICROSET_STEAM_DIR");
    }

public slots:
    void onProps(const QString &interface, const QVariantMap &changed, const QStringList &)
    {
        if (interface == OverlayService::interface() && changed.contains(u"Shown"_s))
            m_props << changed.value(u"Shown"_s);
    }

private:
    QVariantList m_props;
};

QTEST_MAIN(OverlayTest)
#include "tst_overlay.moc"
