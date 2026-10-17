#include "auth.h"
#include "dbuspad.h"
#include "fakelogind.h"
#include "harness.h"
#include "holdhint.h"
#include "overlay.h"
#include "overlayservice.h"
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
#include <QTest>

#include <memory>

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
