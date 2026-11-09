#include "activator.h"
#include "appmodel.h"
#include "battery.h"
#include "clock.h"
#include "desktophome.h"
#include "fakelogind.h"
#include "favoritesmodel.h"
#include "harness.h"
#include "homeclient.h"
#include "idlepulse.h"
#include "launch.h"
#include "padlink.h"
#include "launcher.h"
#include "poweraction.h"
#include "sleepinhibitor.h"
#include "wifi.h"
#ifdef MICROSET_STORE_CORE
#include "fakebackend.h"
#include "storejob.h"
#include "storeworker.h"
#endif

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QSignalSpy>
#include <QTest>

#include <memory>

using namespace Qt::StringLiterals;

namespace {

class FakeHome : public DesktopHome
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "net.seilent.MicroOs.DesktopHome1")

public:
    explicit FakeHome(QObject *parent = nullptr)
        : DesktopHome(parent)
    {
    }

    void enter() override { calls << u"Enter"_s; }
    void toggle() override { calls << u"Toggle"_s; }
    void leave() override { calls << u"Leave"_s; }
    void focusWindow(const QString &address) override
    {
        calls << u"FocusWindow"_s;
        focused << address;
    }

    void publish(bool isShown, const QVariantList &rows, uint turn)
    {
        setAvailable(true);
        setWindows(rows);
        setTransform(turn);
        setShown(isShown);
    }

    void vanish()
    {
        setAvailable(false);
        setShown(false);
        setWindows({});
    }

    void refuse(const QString &method) { emit failed(method, u"refused"_s); }

    QStringList calls;
    QStringList focused;
};

class FakeActivator : public Activator
{
    Q_OBJECT

public:
    using Activator::Activator;

    void start(const QString &id) override
    {
        starts << id;
        pending << id;
        if (hold)
            return;
        complete();
    }

    void complete()
    {
        if (pending.isEmpty())
            return;
        const QString id = pending.takeFirst();
        if (failWith.isEmpty()) {
            reportStarted(id, 4242);
            return;
        }
        reportFailed(id, failWith);
    }

    void release()
    {
        hold = false;
        complete();
    }

    void reportStarted(const QString &id, qint64 pid) override
    {
        if (startedIds.contains(id))
            return;
        startedIds << id;
        emit started(id, pid);
    }

    void reportFailed(const QString &id, const QString &message) override { emit failed(id, message); }
    QMap<QString, QString> environment() const override { return env; }

    QStringList starts;
    QStringList pending;
    QStringList startedIds;
    bool hold = false;
    QString failWith;
    QMap<QString, QString> env;
};

class FakePower : public PowerAction
{
    Q_OBJECT

public:
    using PowerAction::PowerAction;

    void run(const QString &verb) override { verbs << verb; }

    QStringList verbs;
};

class FakePad : public PadLink
{
    Q_OBJECT

public:
    using PadLink::PadLink;

    bool wanted() const override { return m_wanted; }

    void setWanted(bool wanted) override
    {
        if (wanted == m_wanted)
            return;
        m_wanted = wanted;
        if (!wanted)
            asks << u"release"_s;
    }

    void finishRelease() { emit releaseDone(); }

    QStringList asks;

private:
    bool m_wanted = false;
};

class FakeLauncher : public Launcher
{
    Q_OBJECT

public:
    using Launcher::Launcher;

    QStringList appids;

protected:
    bool startUninstall(const QString &appid) override
    {
        appids << appid;
        return true;
    }
};

class FakeIdle : public IdlePulse
{
public:
    using IdlePulse::IdlePulse;

    void pulse() override { counted(); }
    bool ready() const override { return true; }
};

}

class LauncherTest : public QObject
{
    Q_OBJECT

    FakeHome *home = nullptr;
    AppModel *apps = nullptr;
    FavoritesModel *favorites = nullptr;
    FakeActivator *launch = nullptr;
    FakePower *power = nullptr;
    Launcher *launcher = nullptr;
    FakeIdle *idle = nullptr;

    void build(int columns)
    {
        home = new FakeHome();
        apps = new AppModel();
        favorites = new FavoritesModel(QDBusConnection(u"launcher-test"_s), apps);
        launch = new FakeActivator();
        power = new FakePower();
        launcher = new Launcher(home, apps, favorites, launch, power, nullptr, nullptr, 1, 60);
        idle = new FakeIdle();
        launcher->setIdle(idle);
        launcher->setColumns(columns);
    }

    void star(const QStringList &ids) { favorites->setIdsForTest(ids); }

    QVariantList windows(int count) const
    {
        QVariantList out;
        for (int i = 0; i < count; ++i) {
            out << QVariantMap{{u"address"_s, u"0x%1"_s.arg(i + 1, 0, 16)},
                               {u"class"_s, u"foot"_s},
                               {u"title"_s, u"window %1"_s.arg(i + 1)}};
        }
        return out;
    }

    static QVariantList entries(int count)
    {
        QVariantList out;
        for (int i = 0; i < count; ++i)
            out << QVariantMap{{u"id"_s, u"app%1.desktop"_s.arg(i)}, {u"name"_s, u"app %1"_s.arg(i)}};
        return out;
    }

    static QStringList ids(int count)
    {
        QStringList out;
        for (int i = 0; i < count; ++i)
            out << u"app%1.desktop"_s.arg(i);
        return out;
    }

private slots:
    void cleanup()
    {
        delete launcher;
        launcher = nullptr;
        idle = nullptr;
        delete launch;
        launch = nullptr;
        delete power;
        power = nullptr;
        delete apps;
        apps = nullptr;
        delete favorites;
        favorites = nullptr;
        delete home;
        home = nullptr;
    }

    void transformToPadTurn()
    {
        QCOMPARE(Launcher::turnFor(0, 1), 1);
        QCOMPARE(Launcher::turnFor(1, 1), 2);
        QCOMPARE(Launcher::turnFor(2, 1), 3);
        QCOMPARE(Launcher::turnFor(3, 1), 0);
        QCOMPARE(Launcher::turnFor(0, 3), 3);
        QCOMPARE(Launcher::turnFor(1, 3), 0);
        build(6);
        home->publish(false, {}, 1);
        QCOMPARE(launcher->padTurn(), 2);
        home->publish(false, {}, 0);
        QCOMPARE(launcher->padTurn(), 1);
        home->publish(false, {}, 2);
        QCOMPARE(launcher->padTurn(), 3);
        home->publish(false, {}, 3);
        QCOMPARE(launcher->padTurn(), 0);
    }

    void contentFollowsShown()
    {
        build(6);
        QVERIFY(launcher->content());
        home->publish(true, {}, 1);
        QVERIFY(launcher->shown());
        QVERIFY(launcher->content());
        home->publish(false, {}, 1);
        QVERIFY(!launcher->shown());
        QVERIFY(!launcher->content());
    }

    void serviceDownKeepsContent()
    {
        build(6);
        home->vanish();
        QVERIFY(!launcher->shown());
        QVERIFY(launcher->content());
        QVERIFY(launcher->serviceDown());
    }

    void arbiterFollowsShown()
    {
        build(6);
        home->publish(false, {}, 1);
        QVERIFY(!launcher->arbiterWanted());
        home->publish(true, {}, 1);
        QVERIFY(launcher->arbiterWanted());
        home->publish(false, {}, 1);
        QVERIFY(!launcher->arbiterWanted());
        home->publish(true, {}, 1);
        home->vanish();
        QVERIFY(!launcher->arbiterWanted());
    }

    void showCallsEnter()
    {
        build(6);
        launcher->Show();
        QCOMPARE(home->calls, QStringList{u"Enter"_s});
        launcher->Toggle();
        QCOMPARE(home->calls.last(), u"Toggle"_s);
    }

    void showPowerOpensOnceShown()
    {
        build(6);
        launcher->ShowPower();
        QVERIFY(!launcher->power());
        home->publish(true, {}, 1);
        QVERIFY(launcher->power());
        launcher->closePower();
        QVERIFY(!launcher->power());
    }

    void showPowerGivesUpAtTheBound()
    {
        build(6);
        launcher->ShowPower();
        QVERIFY(!launcher->power());
        QTest::qWait(1200);
        QVERIFY(!launcher->power());
        home->publish(true, {}, 1);
        QVERIFY(!launcher->power());
    }

    void focusMovesThroughRunningRowAndGrid()
    {
        build(4);
        home->publish(true, windows(3), 1);
        apps->setEntriesForTest(entries(10));
        star(ids(10));
        launcher->focusRunningAt(0);
        QCOMPARE(launcher->focusRow(), 0);
        QCOMPARE(launcher->focusIndex(), 0);
        QVERIFY(launcher->handleAction(u"right"_s, false));
        QCOMPARE(launcher->focusIndex(), 1);
        QVERIFY(launcher->handleAction(u"right"_s, false));
        QVERIFY(launcher->handleAction(u"right"_s, false));
        QCOMPARE(launcher->focusIndex(), 2);
        QVERIFY(launcher->handleAction(u"down"_s, false));
        QCOMPARE(launcher->focusRow(), 1);
        QCOMPARE(launcher->focusIndex(), 2);
        QVERIFY(launcher->handleAction(u"up"_s, false));
        QCOMPARE(launcher->focusRow(), 0);
        QVERIFY(launcher->handleAction(u"left"_s, false));
        QCOMPARE(launcher->focusIndex(), 1);
    }

    void runningRowAppearingKeepsTheGridFocus()
    {
        build(4);
        apps->setEntriesForTest(entries(10));
        star(ids(10));
        home->publish(true, {}, 1);
        launcher->focusGridAt(1);
        QCOMPARE(launcher->focusRow(), 0);
        QCOMPARE(launcher->focusIndex(), 1);
        home->publish(true, windows(2), 1);
        QCOMPARE(launcher->focusRow(), 1);
        QCOMPARE(launcher->focusIndex(), 1);
        QVERIFY(launcher->handleAction(u"a"_s, false));
        QCOMPARE(launch->starts, QStringList{u"app1.desktop"_s});
    }

    void runningRowDisappearingKeepsTheGridFocus()
    {
        build(4);
        apps->setEntriesForTest(entries(10));
        star(ids(10));
        home->publish(true, windows(2), 1);
        launcher->focusGridAt(5);
        QCOMPARE(launcher->focusRow(), 2);
        QCOMPARE(launcher->focusIndex(), 1);
        home->publish(true, {}, 1);
        QCOMPARE(launcher->focusRow(), 1);
        QCOMPARE(launcher->focusIndex(), 1);
        QVERIFY(launcher->handleAction(u"a"_s, false));
        QCOMPARE(launch->starts, QStringList{u"app5.desktop"_s});
    }

    void emptyFavoritesClampsFocusToTheTiles()
    {
        build(6);
        QCOMPARE(launcher->apps().size(), 2);
        QVERIFY(launcher->handleAction(u"right"_s, false));
        QCOMPARE(launcher->focusIndex(), 1);
        QVERIFY(launcher->handleAction(u"right"_s, false));
        QCOMPARE(launcher->focusIndex(), 1);
        QVERIFY(launcher->handleAction(u"down"_s, false));
        QCOMPARE(launcher->focusRow(), 0);
    }

    void activateOnRunningTileFocusesWindow()
    {
        build(6);
        home->publish(true, windows(2), 1);
        launcher->focusRunningAt(0);
        launcher->handleAction(u"right"_s, false);
        QVERIFY(launcher->handleAction(u"a"_s, false));
        QCOMPARE(home->calls.last(), u"FocusWindow"_s);
        QCOMPARE(home->focused.last(), u"0x2"_s);
        QVERIFY(launch->starts.isEmpty());
    }

    void shownReturnsWhileHidden()
    {
        build(6);
        home->publish(false, {}, 1);
        home->publish(true, {}, 1);
        QVERIFY(launcher->handleAction(u"b"_s, false));
        QVERIFY(!launcher->content());
        home->publish(false, {}, 1);
        home->publish(true, {}, 1);
        QVERIFY(launcher->content());
    }

    void contentHidesBeforeFocusWindow()
    {
        build(6);
        home->publish(true, windows(2), 1);
        launcher->focusRunningAt(0);
        QVERIFY(launcher->content());
        QVERIFY(launcher->handleAction(u"a"_s, false));
        QVERIFY(!launcher->content());
        QCOMPARE(home->calls.last(), u"FocusWindow"_s);
    }

    void activateOnGridLaunches()
    {
        build(6);
        launch->hold = true;
        home->publish(true, {}, 1);
        apps->setEntriesForTest(entries(2));
        star(ids(2));
        QVERIFY(launcher->handleAction(u"a"_s, false));
        QCOMPARE(launch->starts, QStringList{u"app0.desktop"_s});
        QVERIFY(launcher->launching());
        QVERIFY(!launcher->content());
        launch->release();
        QVERIFY(!launcher->launching());
        QVERIFY(!launcher->content());
        QTRY_VERIFY_WITH_TIMEOUT(launcher->content(), 5000);
    }

    void launchReShowsAfterTheBound()
    {
        build(6);
        home->publish(true, {}, 1);
        apps->setEntriesForTest(entries(2));
        star(ids(2));
        QVERIFY(launcher->handleAction(u"a"_s, false));
        QVERIFY(!launcher->content());
        QTRY_VERIFY_WITH_TIMEOUT(launcher->content(), 5000);
    }

    void secondActivateWhileLaunchingIsIgnored()
    {
        build(6);
        launch->hold = true;
        home->publish(true, {}, 1);
        apps->setEntriesForTest(entries(2));
        star(ids(2));
        QVERIFY(launcher->handleAction(u"a"_s, false));
        QVERIFY(launcher->launching());
        QVERIFY(launcher->handleAction(u"a"_s, false));
        QCOMPARE(launch->starts.size(), 1);
        launch->release();
        QVERIFY(!launcher->launching());
        QTRY_VERIFY_WITH_TIMEOUT(launcher->content(), 5000);
        QVERIFY(launcher->handleAction(u"a"_s, false));
        QCOMPARE(launch->starts.size(), 2);
    }

    void launchFailureReShowsAtOnce()
    {
        build(6);
        home->publish(true, {}, 1);
        apps->setEntriesForTest(entries(2));
        star(ids(2));
        launch->failWith = u"refused"_s;
        QVERIFY(launcher->handleAction(u"a"_s, false));
        QVERIFY(!launcher->launching());
        QVERIFY(launcher->content());
    }

    void backOnGridLeaves()
    {
        build(6);
        home->publish(true, {}, 1);
        QVERIFY(launcher->handleAction(u"b"_s, false));
        QCOMPARE(home->calls, QStringList{u"Leave"_s});
    }

    void showPowerSetsAndRunPowerClears()
    {
        build(6);
        home->publish(true, {}, 1);
        launcher->ShowPower();
        QVERIFY(launcher->power());
        launcher->runPower(u"reboot"_s);
        QCOMPARE(power->verbs, QStringList{u"reboot"_s});
        QVERIFY(!launcher->power());
        launcher->ShowPower();
        QVERIFY(launcher->power());
        launcher->closePower();
        QVERIFY(!launcher->power());
        QVERIFY(!home->calls.contains(u"Leave"_s));
    }

    void errorReplyReShowsAtOnce()
    {
        build(6);
        home->publish(true, {}, 1);
        QVERIFY(launcher->handleAction(u"b"_s, false));
        QVERIFY(!launcher->content());
        home->refuse(u"Leave"_s);
        QVERIFY(launcher->content());
    }

    void gridEndsWithTheTwoTiles()
    {
        build(6);
        apps->setEntriesForTest(entries(4));
        star(ids(2));
        const QVariantList grid = launcher->apps();
        QCOMPARE(grid.size(), 4);
        QCOMPARE(grid.at(2).toMap().value(u"end"_s).toString(), u"allapps"_s);
        QCOMPARE(grid.at(2).toMap().value(u"name"_s).toString(), u"All apps"_s);
        QCOMPARE(grid.at(3).toMap().value(u"end"_s).toString(), u"store"_s);
        QCOMPARE(grid.at(3).toMap().value(u"name"_s).toString(), u"Store"_s);
        QCOMPARE(launcher->favoriteCount(), 2);
        QCOMPARE(launcher->allApps().size(), 4);
    }

    void favoritesAddTilesBeforeTheEnd()
    {
        build(6);
        apps->setEntriesForTest(entries(3));
        QCOMPARE(launcher->apps().size(), 2);
        star(QStringList{u"app2.desktop"_s});
        QCOMPARE(launcher->apps().size(), 3);
        QCOMPARE(launcher->apps().at(0).toMap().value(u"id"_s).toString(), u"app2.desktop"_s);
        QCOMPARE(launcher->favoriteIds(), QStringList{u"app2.desktop"_s});
    }

    void allAppsTileOpensThePage()
    {
        build(6);
        apps->setEntriesForTest(entries(4));
        star(ids(1));
        QCOMPARE(launcher->page(), Launcher::homePage());
        launcher->focusGridAt(1);
        QVERIFY(launcher->handleAction(u"a"_s, false));
        QCOMPARE(launcher->page(), Launcher::appsPage());
        launcher->closePage();
        QCOMPARE(launcher->page(), Launcher::homePage());
    }

    void yOpensAllApps()
    {
        build(6);
        home->publish(true, {}, 1);
        QVERIFY(launcher->handleAction(u"y"_s, false));
        QCOMPARE(launcher->page(), Launcher::appsPage());
    }

    void storeTileStartsTheStoreApp()
    {
        build(6);
        apps->setEntriesForTest(entries(4));
        launcher->focusGridAt(1);
        QVERIFY(launcher->handleAction(u"a"_s, false));
        QCOMPARE(launch->starts, QStringList{Launcher::storeAppId()});
    }

    void xRequestsTheSheetOnAFavoriteOnly()
    {
        build(6);
        apps->setEntriesForTest(entries(4));
        star(ids(1));
        QSignalSpy sheets(launcher, &Launcher::sheetRequested);
        launcher->focusGridAt(0);
        QVERIFY(launcher->handleAction(u"x"_s, false));
        QCOMPARE(sheets.count(), 1);
        launcher->focusGridAt(1);
        QVERIFY(launcher->handleAction(u"x"_s, false));
        QCOMPARE(sheets.count(), 1);
    }

    void hidingReturnsToHome()
    {
        build(6);
        home->publish(true, {}, 1);
        launcher->openAllApps();
        QCOMPARE(launcher->page(), Launcher::appsPage());
        launch->hold = true;
        launcher->launchApp(u"foot.desktop"_s);
        QCOMPARE(launch->starts, QStringList{u"foot.desktop"_s});
        QVERIFY(!launcher->content());
        QCOMPARE(launcher->page(), Launcher::homePage());
        launch->release();
    }

    void launchFailureFromAllAppsKeepsThePage()
    {
        build(6);
        home->publish(true, {}, 1);
        apps->setEntriesForTest(entries(2));
        launcher->openAllApps();
        QCOMPARE(launcher->page(), Launcher::appsPage());
        launch->failWith = u"no entry"_s;
        QSignalSpy failed(launcher, &Launcher::launchFailed);
        launcher->launchApp(u"app0.desktop"_s);
        QCOMPARE(failed.count(), 1);
        QCOMPARE(failed.at(0).at(0).toString(), u"app 0"_s);
        QCOMPARE(failed.at(0).at(1).toString(), u"no entry"_s);
        QVERIFY(!launcher->launching());
        QVERIFY(launcher->content());
        QCOMPARE(launcher->page(), Launcher::appsPage());
    }

    void favoriteTogglesDoNotTouchTheAppList()
    {
        build(6);
        apps->setEntriesForTest(entries(4));
        QSignalSpy list(launcher, &Launcher::allAppsChanged);
        QSignalSpy grid(launcher, &Launcher::appsChanged);
        launcher->toggleFavorite(u"app1.desktop"_s);
        QCOMPARE(grid.count(), 1);
        QCOMPARE(list.count(), 0);
        apps->setEntriesForTest(entries(5));
        QCOMPARE(list.count(), 1);
    }

    void uninstallAsksTheStoreBeforeItTouchesTheStar()
    {
        FakeHome h;
        AppModel model;
        FavoritesModel stars(QDBusConnection(u"launcher-test"_s), &model);
        FakeLauncher subject(&h, &model, &stars, nullptr, nullptr, nullptr, nullptr, 1);
        model.setEntriesForTest(QVariantList{
            QVariantMap{{u"id"_s, u"org.example.App.desktop"_s},
                        {u"name"_s, u"App"_s},
                        {u"flatpak"_s, u"org.example.App"_s}},
            QVariantMap{{u"id"_s, u"foot.desktop"_s}, {u"name"_s, u"Foot"_s}}});
        stars.setIdsForTest({u"org.example.App.desktop"_s, u"foot.desktop"_s});
        subject.uninstallApp(u"org.example.App.desktop"_s);
        QCOMPARE(subject.appids, QStringList{u"org.example.App"_s});
        QCOMPARE(stars.ids(), QStringList({u"org.example.App.desktop"_s, u"foot.desktop"_s}));
        subject.uninstallApp(u"foot.desktop"_s);
        QCOMPARE(subject.appids.size(), 1);
        QCOMPARE(stars.ids(), QStringList({u"org.example.App.desktop"_s, u"foot.desktop"_s}));
    }

#ifdef MICROSET_STORE_CORE
    void uninstallUnstarsAfterTheStoreFinishes()
    {
        FakeHome h;
        AppModel model;
        FavoritesModel stars(QDBusConnection(u"launcher-test"_s), &model);
        Launcher subject(&h, &model, &stars, nullptr, nullptr, nullptr, nullptr, 1);
        auto backend = std::make_unique<FakeBackend>();
        auto *fake = backend.get();
        StoreWorker worker(std::move(backend));
        worker.start();
        subject.setStore(&worker);
        model.setEntriesForTest(QVariantList{
            QVariantMap{{u"id"_s, u"org.example.App.desktop"_s},
                        {u"name"_s, u"App"_s},
                        {u"flatpak"_s, u"org.example.App"_s}}});
        stars.setIdsForTest({u"org.example.App.desktop"_s});
        QSignalSpy failed(&subject, &Launcher::uninstallFailed);
        subject.uninstallApp(u"org.example.App.desktop"_s);
        QCOMPARE(stars.ids(), QStringList{u"org.example.App.desktop"_s});
        QTRY_COMPARE(fake->uninstalled(), QStringList{u"org.example.App"_s});
        QTRY_COMPARE(stars.ids(), QStringList{});
        QCOMPARE(failed.count(), 0);
    }

    void uninstallFailureKeepsTheStarAndReportsTheError()
    {
        FakeHome h;
        AppModel model;
        FavoritesModel stars(QDBusConnection(u"launcher-test"_s), &model);
        Launcher subject(&h, &model, &stars, nullptr, nullptr, nullptr, nullptr, 1);
        auto backend = std::make_unique<FakeBackend>();
        auto *fake = backend.get();
        StoreWorker worker(std::move(backend));
        worker.start();
        subject.setStore(&worker);
        model.setEntriesForTest(QVariantList{
            QVariantMap{{u"id"_s, u"org.example.App.desktop"_s},
                        {u"name"_s, u"App"_s},
                        {u"flatpak"_s, u"org.example.App"_s}}});
        stars.setIdsForTest({u"org.example.App.desktop"_s});
        fake->setUninstallFails(u"org.example.App"_s, u"the app is running"_s);
        QSignalSpy failed(&subject, &Launcher::uninstallFailed);
        subject.uninstallApp(u"org.example.App.desktop"_s);
        QTRY_COMPARE(failed.count(), 1);
        QCOMPARE(failed.at(0).at(0).toString(), u"App"_s);
        QCOMPARE(failed.at(0).at(1).toString(), u"the app is running"_s);
        QCOMPARE(stars.ids(), QStringList{u"org.example.App.desktop"_s});
    }
#endif

    void idlePulsesOncePerSecond()
    {
        build(6);
        home->publish(true, {}, 1);
        QCOMPARE(idle->pulseCount(), 0);
        launcher->pulse();
        QCOMPARE(idle->pulseCount(), 1);
        launcher->pulse();
        QCOMPARE(idle->pulseCount(), 1);
        QTest::qWait(1100);
        launcher->pulse();
        QCOMPARE(idle->pulseCount(), 2);
    }

    void clockFormatsTheDefaultBranch()
    {
        QCOMPARE(Clock::format(QDateTime(QDate(2026, 11, 9), QTime(7, 5))), u"7:05"_s);
        QCOMPARE(Clock::format(QDateTime(QDate(2026, 11, 9), QTime(23, 59))), u"23:59"_s);
        QCOMPARE(Clock::format(QDateTime(QDate(2026, 11, 9), QTime(0, 0))), u"0:00"_s);
    }

    void clockArmsAndReArmsAfterCancel()
    {
        Clock clock;
        QSignalSpy changed(&clock, &Clock::textChanged);
        clock.start();
        QCOMPARE(clock.armCount(), 1);
        QVERIFY(!clock.text().isEmpty());
        QCOMPARE(clock.text(), Clock::format(QDateTime::currentDateTime()));
        QCOMPARE(clock.cancelCount(), 0);
        changed.clear();
    }

    void managerEnvironmentParses()
    {
        const QMap<QString, QString> values = ManagerEnv::parse(
            {u"HOME=/var/home/micro"_s, u"PATH=/usr/bin"_s, u"BAD"_s, u"XDG_RUNTIME_DIR=/run/user/1000"_s});
        QCOMPARE(values.size(), 3);
        QCOMPARE(values.value(u"HOME"_s), u"/var/home/micro"_s);
        QCOMPARE(values.value(u"PATH"_s), u"/usr/bin"_s);
        QVERIFY(!values.contains(u"BAD"_s));
        QCOMPARE(LauncherLaunch::requiredNames(),
                 QStringList({u"HOME"_s, u"PATH"_s, u"XDG_RUNTIME_DIR"_s, u"DBUS_SESSION_BUS_ADDRESS"_s}));
    }

    void scopeNameEscapes()
    {
        QCOMPARE(LauncherLaunch::scopeName(u"foot.desktop"_s, 42),
                 u"app-microset\\x2dlauncher-foot.desktop-42.scope"_s);
        QCOMPARE(LauncherLaunch::escapeId(u"a b/c"_s), u"a\\x20b\\x2fc"_s);
    }

    void batteryAndWifiNames()
    {
        QCOMPARE(Battery::path(), u"/org/freedesktop/UPower/devices/DisplayDevice"_s);
        QCOMPARE(Battery::deviceInterface(), u"org.freedesktop.UPower.Device"_s);
        QCOMPARE(Wifi::wirelessType(), u"802-11-wireless"_s);
        QCOMPARE(Wifi::accessPointInterface(), u"org.freedesktop.NetworkManager.AccessPoint"_s);
        QCOMPARE(Wifi::deviceInterface(), u"org.freedesktop.NetworkManager.Device.Wireless"_s);
        QCOMPARE(LogindPower::path(), u"/org/freedesktop/login1"_s);
    }

    void hiddenEntries()
    {
        QVERIFY(AppModel::hidden(u"steam_http_loader.desktop"_s, u"steam-http-loader %u"_s));
        QVERIFY(AppModel::hidden(u"Lossless Scaling.desktop"_s, u"steam steam://rungameid/993090"_s));
        QVERIFY(AppModel::hidden(u"org.es_de.frontend.desktop"_s, u"es-de"_s));
        QVERIFY(AppModel::hidden(u"org.libretro.RetroArch.desktop"_s, u"retroarch"_s));
        QVERIFY(!AppModel::hidden(u"foot.desktop"_s, u"foot"_s));
        QVERIFY(!AppModel::hidden(u"steam.desktop"_s, u"steam"_s));
    }

    void methodsBeforeName()
    {
        Harness harness;
        QVERIFY(harness.startBus());
        QDBusConnection bus = harness.client();
        FakeHome *h = new FakeHome();
        AppModel *model = new AppModel();
        FakeActivator *act = new FakeActivator();
        Launcher subject(h, model, nullptr, act, nullptr, nullptr, nullptr, 1);
        QDBusConnectionInterface *iface = bus.interface();
        const QString name = Launcher::service();
        QVERIFY(!iface->isServiceRegistered(name).value());
        QVERIFY(subject.publish(bus));
        QDBusMessage show = QDBusMessage::createMethodCall(name, Launcher::path(), Launcher::interface(), u"Show"_s);
        const QDBusMessage reply = bus.call(show, QDBus::Block, 2000);
        QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
        QCOMPARE(h->calls, QStringList{u"Enter"_s});
    }

    void suspendReleasesThePad()
    {
        Harness harness;
        QVERIFY(harness.startBus());
        FakeLogind logind(harness.address());
        QVERIFY(logind.start());
        QDBusConnection system = QDBusConnection::connectToBus(harness.address(), u"launcher-system"_s);
        QDBusConnection session = harness.client();
        FakeHome *h = new FakeHome();
        AppModel *model = new AppModel();
        FakeActivator *act = new FakeActivator();
        FakePad pad;
        SleepInhibitor sleep(system, u"test"_s);
        QVERIFY(sleep.subscribed());
        Launcher subject(h, model, nullptr, act, nullptr, &pad, &sleep, 1);
        h->publish(true, {}, 1);
        QVERIFY(subject.shown());
        QVERIFY(pad.wanted());
        sleep.take();
        QTRY_VERIFY_WITH_TIMEOUT(sleep.held(), 3000);
        QCOMPARE(logind.inhibits.size(), 1);
        QVERIFY(logind.inhibits.constFirst().startsWith(u"sleep|microset|test|delay"_s));
        const int takes = sleep.takeCount();
        logind.prepareForSleep(true);
        QTest::qWait(100);
        QVERIFY(pad.asks.contains(u"release"_s));
        QVERIFY(!pad.wanted());
        QVERIFY(sleep.held());
        QCOMPARE(sleep.takeCount(), takes);
        pad.finishRelease();
        QTRY_VERIFY_WITH_TIMEOUT(!sleep.held(), 3000);
        logind.prepareForSleep(false);
        QTest::qWait(100);
        QVERIFY(subject.shown());
        QVERIFY(pad.wanted());
        QDBusConnection::disconnectFromBus(u"launcher-system"_s);
    }

    void focusGridAtAndRunningAt()
    {
        build(4);
        home->publish(true, windows(2), 1);
        apps->setEntriesForTest(entries(10));
        star(ids(10));
        QSignalSpy focus(launcher, &Launcher::focusChanged);
        launcher->focusRunningAt(1);
        QCOMPARE(focus.count(), 1);
        launcher->focusRunningAt(1);
        QCOMPARE(focus.count(), 2);
        launcher->focusRunningAt(0);
        QCOMPARE(focus.count(), 3);
        QCOMPARE(launcher->focusRow(), 0);
        QCOMPARE(launcher->focusIndex(), 0);
        launcher->focusGridAt(9);
        QCOMPARE(launcher->focusRow(), 1 + 2);
        QCOMPARE(launcher->focusIndex(), 1);
        launcher->focusGridAt(4);
        QCOMPARE(launcher->focusRow(), 2);
        QCOMPARE(launcher->focusIndex(), 0);
    }
};

QTEST_GUILESS_MAIN(LauncherTest)

#include "tst_launcher.moc"
