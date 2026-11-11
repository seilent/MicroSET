#include "accentwatch.h"
#include "activator.h"
#include "appmodel.h"
#include "desktophome.h"
#include "favoritesmodel.h"
#include "homeclient.h"
#include "idlepulse.h"
#include "launcher.h"
#include "poweraction.h"
#ifdef MICROSET_STORE_CORE
#include "fakebackend.h"
#include "storejob.h"
#include "storeworker.h"
#endif

#include <QColor>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVirtualObject>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QtQml/qqmlextensionplugin.h>

#include <memory>

Q_IMPORT_QML_PLUGIN(MicroSetPlugin)
Q_IMPORT_QML_PLUGIN(MicroSetLauncherPlugin)

using namespace Qt::StringLiterals;

namespace {

class FakeHome : public DesktopHome
{
    Q_OBJECT

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

    void publish(bool isShown, const QVariantList &rows, uint turn = 1)
    {
        setAvailable(true);
        setWindows(rows);
        setTransform(turn);
        setShown(isShown);
    }

    QStringList calls;
    QStringList focused;
};

class FakePad : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    Q_INVOKABLE QString actionName(int action) const { return names.value(action); }

    void fire(const QString &name)
    {
        const int index = names.key(name, -1);
        if (index >= 0)
            emit action(index, false);
    }

    void press(const QString &name)
    {
        const int index = names.key(name, -1);
        if (index < 0)
            return;
        emit pressed(index);
        emit action(index, false);
    }

    void hold(const QString &name)
    {
        const int index = names.key(name, -1);
        if (index >= 0)
            emit action(index, true);
    }

    void release(const QString &name)
    {
        const int index = names.key(name, -1);
        if (index >= 0)
            emit released(index);
    }

    QMap<int, QString> names{{0, u"up"_s}, {1, u"down"_s}, {2, u"left"_s}, {3, u"right"_s},
                             {4, u"a"_s}, {5, u"b"_s}, {6, u"x"_s}, {7, u"y"_s}};

signals:
    void action(int action, bool repeat);
    void pressed(int action);
    void released(int action);
};

class FakeActivator : public Activator
{
    Q_OBJECT

public:
    using Activator::Activator;

    void start(const QString &id) override
    {
        starts << id;
        if (hold) {
            pending << id;
            return;
        }
        finish(id);
    }

    void release()
    {
        hold = false;
        if (!pending.isEmpty())
            finish(pending.takeFirst());
    }

    void finish(const QString &id)
    {
        if (!failWith.isEmpty()) {
            reportFailed(id, failWith);
            return;
        }
        reportStarted(id, 4242);
    }

    void reportStarted(const QString &id, qint64 pid) override { emit started(id, pid); }
    void reportFailed(const QString &id, const QString &message) override { emit failed(id, message); }
    QMap<QString, QString> environment() const override { return {}; }

    QStringList starts;
    QStringList pending;
    bool hold = false;
    QString failWith;
};

class FakePower : public PowerAction
{
    Q_OBJECT

public:
    using PowerAction::PowerAction;

    void run(const QString &verb) override { verbs << verb; }

    QStringList verbs;
};

class FakeIdle : public IdlePulse
{
public:
    using IdlePulse::IdlePulse;

    void pulse() override { counted(); }
    bool ready() const override { return true; }
};

class StubDesktopHome : public QDBusVirtualObject
{
public:
    explicit StubDesktopHome(const QString &address)
        : m_address(address)
    {
        QDBusConnection bus = connection();
        m_ok = bus.registerVirtualObject(DesktopHome::path(), this) && bus.registerService(DesktopHome::service());
    }

    ~StubDesktopHome() override
    {
        QDBusConnection bus = connection();
        bus.unregisterObject(DesktopHome::path());
        bus.unregisterService(DesktopHome::service());
        QDBusConnection::disconnectFromBus(u"stub-home"_s);
    }

    QDBusConnection connection() const { return QDBusConnection::connectToBus(m_address, u"stub-home"_s); }
    bool ok() const { return m_ok; }
    QString introspect(const QString &) const override { return {}; }

    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override
    {
        if (message.member() == u"GetAll"_s) {
            QVariantMap rows;
            rows.insert(u"Shown"_s, false);
            rows.insert(u"Windows"_s, QVariantList());
            rows.insert(u"Transform"_s, QVariant::fromValue(transform));
            connection.send(message.createReply(QVariant::fromValue(rows)));
            return true;
        }
        if (message.member() == u"Enter"_s || message.member() == u"Leave"_s || message.member() == u"Toggle"_s
            || message.member() == u"FocusWindow"_s) {
            connection.send(message.createReply());
            return true;
        }
        return false;
    }

    void publish(uint value)
    {
        transform = value;
        QDBusMessage signal = QDBusMessage::createSignal(DesktopHome::path(), u"org.freedesktop.DBus.Properties"_s,
                                                         u"PropertiesChanged"_s);
        QVariantMap changed;
        changed.insert(u"Transform"_s, QVariant::fromValue(value));
        signal << DesktopHome::interface() << changed << QStringList();
        connection().send(signal);
    }

    QString m_address;
    bool m_ok = false;
    uint transform = 0;
};

}

class LayoutTest : public QObject
{
    Q_OBJECT

    FakeHome *home = nullptr;
    AppModel *apps = nullptr;
    FavoritesModel *favorites = nullptr;
    FakeActivator *activator = nullptr;
    FakePower *power = nullptr;
    Launcher *launcher = nullptr;
    FakeIdle *idle = nullptr;
    QQmlApplicationEngine *engine = nullptr;
    QQuickWindow *window = nullptr;
    QQuickItem *root = nullptr;

    void reset()
    {
        delete engine;
        engine = nullptr;
        delete pad;
        pad = nullptr;
        window = nullptr;
        root = nullptr;
        delete launcher;
        launcher = nullptr;
        store.reset();
        idle = nullptr;
        delete activator;
        activator = nullptr;
        delete power;
        power = nullptr;
        delete apps;
        apps = nullptr;
        delete favorites;
        favorites = nullptr;
        delete home;
        home = nullptr;
    }

    FakePad *pad = nullptr;
#ifdef MICROSET_STORE_CORE
    std::unique_ptr<StoreWorker> store;
#endif

    void load(int w, int h, const QVariantList &entries, const QVariantList &rows = {}, uint turn = 1,
              const QStringList &favoriteIds = {})
    {
        reset();
        home = new FakeHome();
        apps = new AppModel();
        apps->setEntriesForTest(entries);
        favorites = new FavoritesModel(QDBusConnection(u"launcher-layout"_s), apps);
        favorites->setIdsForTest(favoriteIds);
        activator = new FakeActivator();
        power = new FakePower();
        launcher = new Launcher(home, apps, favorites, activator, power, nullptr, nullptr, 1, 80);
        idle = new FakeIdle();
        launcher->setIdle(idle);
        home->publish(true, rows, turn);
        pad = new FakePad();
        engine = new QQmlApplicationEngine();
        engine->addImageProvider(u"microset-appicon"_s, new AppIconProvider());
        engine->setInitialProperties({
            {u"launcher"_s, QVariant::fromValue(static_cast<QObject *>(launcher))},
            {u"pad"_s, QVariant::fromValue(static_cast<QObject *>(pad))},
        });
        engine->loadFromModule("MicroSetLauncher", "Home");
        window = qobject_cast<QQuickWindow *>(engine->rootObjects().value(0));
        QVERIFY(window != nullptr);
        QVERIFY(!window->isVisible());
        window->resize(w, h);
        window->show();
        root = window->contentItem();
        QTest::qWait(200);
    }

    QQuickItem *find(const QString &name) const
    {
        QList<QQuickItem *> pending{root};
        while (!pending.isEmpty()) {
            QQuickItem *item = pending.takeFirst();
            if (item->objectName() == name)
                return item;
            pending += item->childItems();
        }
        return nullptr;
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

    static QVariantList windows(int count)
    {
        QVariantList out;
        for (int i = 0; i < count; ++i)
            out << QVariantMap{{u"address"_s, u"0x%1"_s.arg(i + 1, 0, 16)},
                               {u"class"_s, u"foot"_s},
                               {u"title"_s, u"window %1"_s.arg(i + 1)}};
        return out;
    }

private slots:
    void cleanup() { reset(); }

    void accentFollowsWatch()
    {
        load(480, 320, entries(2));
        QObject *ui = engine->singletonInstance<QObject *>("MicroSet", "Ui");
        QVERIFY(ui);
        QColor hue;
        QVERIFY(QMetaObject::invokeMethod(ui, "accentForHue", Q_RETURN_ARG(QColor, hue), Q_ARG(int, 120)));
        auto *accent = new AccentWatch(window);
        accent->setFixture(120, true);
        window->setProperty("accent", QVariant::fromValue(static_cast<QObject *>(accent)));
        QTRY_VERIFY(ui->property("accentOff").toBool());
        QCOMPARE(ui->property("accent").value<QColor>(), QColor(Qt::white));
        QCOMPARE(ui->property("accentHueColor").value<QColor>(), hue);
        accent->setFixture(120, false);
        QTRY_VERIFY(!ui->property("accentOff").toBool());
        QCOMPARE(ui->property("accent").value<QColor>(), hue);
    }

    void homeClientTurnsThePadOverTheBus()
    {
        QTemporaryDir dir;
        const QString socket = dir.filePath(u"bus"_s);
        QProcess daemon;
        daemon.setStandardOutputFile(QProcess::nullDevice());
        daemon.setStandardErrorFile(QProcess::nullDevice());
        daemon.start(u"dbus-daemon"_s,
                    {u"--session"_s, u"--nofork"_s, u"--nopidfile"_s, u"--address=unix:path="_s + socket});
        QVERIFY(daemon.waitForStarted());
        QElapsedTimer ready;
        ready.start();
        while (!QFile::exists(socket) && ready.elapsed() < 10000)
            QTest::qWait(20);
        QVERIFY(QFile::exists(socket));
        const QString address = u"unix:path="_s + socket;

        StubDesktopHome stub(address);
        QVERIFY(stub.ok());
        stub.transform = 0;
        QDBusConnection bus = QDBusConnection::connectToBus(address, u"launcher-home"_s);
        HomeClient client(bus);
        AppModel model;
        FakeActivator act;
        FakePower powerStub;
        Launcher subject(&client, &model, nullptr, &act, &powerStub, nullptr, nullptr, 1, 60);
        engine = new QQmlApplicationEngine();
        engine->setInitialProperties({{u"launcher"_s, QVariant::fromValue(static_cast<QObject *>(&subject))}});
        engine->loadFromModule("MicroSetLauncher", "Home");
        window = qobject_cast<QQuickWindow *>(engine->rootObjects().value(0));
        QVERIFY(window != nullptr);
        window->resize(480, 320);
        window->show();
        root = window->contentItem();
        QTest::qWait(200);
        QQuickItem *router = find(u"router"_s);
        QVERIFY(router != nullptr);
        const auto padName = [router](const QString &raw) {
            QString name;
            const bool called = QMetaObject::invokeMethod(router, "padName", Q_RETURN_ARG(QString, name),
                                                          Q_ARG(QString, raw));
            return called ? name : QString();
        };

        QTRY_COMPARE_WITH_TIMEOUT(client.transform(), 0u, 5000);
        QCOMPARE(subject.padTurn(), Launcher::turnFor(0, 1));
        QCOMPARE(subject.padTurn(), 1);
        QCOMPARE(router->property("turn").toInt(), 1);
        QCOMPARE(padName(u"right"_s), u"down"_s);

        stub.publish(3);
        QTRY_COMPARE_WITH_TIMEOUT(client.transform(), 3u, 5000);
        QCOMPARE(subject.padTurn(), Launcher::turnFor(3, 1));
        QCOMPARE(subject.padTurn(), 0);
        QCOMPARE(router->property("turn").toInt(), 0);
        QCOMPARE(padName(u"right"_s), u"right"_s);

        stub.publish(1);
        QTRY_COMPARE_WITH_TIMEOUT(client.transform(), 1u, 5000);
        QCOMPARE(subject.padTurn(), Launcher::turnFor(1, 1));
        QCOMPARE(subject.padTurn(), 2);
        QCOMPARE(router->property("turn").toInt(), 2);
        QCOMPARE(padName(u"right"_s), u"left"_s);

        for (uint turn : {2u, 0u}) {
            stub.publish(turn);
            QTRY_COMPARE_WITH_TIMEOUT(client.transform(), turn, 5000);
            QCOMPARE(subject.padTurn(), Launcher::turnFor(turn, 1));
            QCOMPARE(router->property("turn").toInt(), Launcher::turnFor(turn, 1));
        }

        delete engine;
        engine = nullptr;
        window = nullptr;
        root = nullptr;
        QDBusConnection::disconnectFromBus(u"launcher-home"_s);
        daemon.terminate();
        if (!daemon.waitForFinished(3000)) {
            daemon.kill();
            daemon.waitForFinished();
        }
    }

    void gridTilesGetTheirIconsOnTheFirstFill()
    {
        AppIcons::publish({});
        AppModel model;
        QSignalSpy changed(&model, &AppModel::entriesChanged);
        model.setEntriesForTest(QVariantList{
            QVariantMap{{u"id"_s, u"one.desktop"_s}, {u"name"_s, u"One"_s}, {u"iconFile"_s, u"/tmp/one.png"_s}},
            QVariantMap{{u"id"_s, u"two.desktop"_s}, {u"name"_s, u"Two"_s}, {u"iconFile"_s, u"/tmp/two.png"_s}}});
        const QVariantList rows = model.entries();
        QCOMPARE(rows.size(), 2);
        for (const QVariant &row : rows) {
            const QVariantMap map = row.toMap();
            QCOMPARE(map.value(u"source"_s).toString(),
                     u"image://microset-appicon/"_s + map.value(u"id"_s).toString());
        }
        QVERIFY(!changed.isEmpty());
    }

    void landscapeGrid()
    {
        load(480, 320, entries(30), {}, 1, ids(30));
        QCOMPARE(launcher->columns(), 6);
        QQuickItem *grid = find(u"grid"_s);
        QVERIFY(grid != nullptr);
        QCOMPARE(int(qRound(grid->property("cellWidth").toReal())), 72);
        QCOMPARE(int(qRound(grid->property("cellHeight").toReal())), 96);
        QCOMPARE(int(qRound(grid->y())), 98 - 2);
        QCOMPARE(int(qRound(grid->x())), 20 - 2);
        QCOMPARE(int(qRound(grid->width())), 6 * 72 + 4);
        QVERIFY(find(u"tile-6"_s) != nullptr);
        QCOMPARE(int(qRound(find(u"tile-6"_s)->x())), 0);
        QCOMPARE(int(qRound(find(u"tile-6"_s)->y())), 96);
    }

    void portraitGrid()
    {
        load(320, 480, entries(30), {}, 1, ids(30));
        QCOMPARE(launcher->columns(), 3);
        QQuickItem *grid = find(u"grid"_s);
        QVERIFY(grid != nullptr);
        QCOMPARE(int(qRound(grid->property("cellWidth").toReal())), 83);
        QCOMPARE(int(qRound(grid->property("cellHeight").toReal())), 107);
        QCOMPARE(int(qRound(grid->y())), 128 - 2);
        const QQuickItem *tile = find(u"tile-0"_s);
        QVERIFY(tile != nullptr);
        QCOMPARE(int(qRound(tile->width())), 71);
        QCOMPARE(int(qRound(find(u"tile-3"_s)->y())), 107);
    }

    void emptyStateText()
    {
        load(480, 320, {});
        QQuickItem *empty = find(u"empty"_s);
        QVERIFY(empty != nullptr);
        QVERIFY(empty->isVisible());
        QCOMPARE(empty->property("text").toString(), u"Press X in all apps to pin an app here."_s);
        QCOMPARE(int(qRound(empty->x())), 20);
        QCOMPARE(int(qRound(empty->width())), 140);
        QCOMPARE(int(qRound(empty->property("lineHeight").toReal())), 11);
        QCOMPARE(int(qRound(empty->y() + empty->property("baselineOffset").toReal())), 98 + 96 + 16);
        QVERIFY(find(u"end-allapps"_s) != nullptr);
        QVERIFY(find(u"end-store"_s) != nullptr);
    }

    void emptyLineIsHiddenWithOneFavorite()
    {
        load(480, 320, entries(2), {}, 1, ids(1));
        QQuickItem *empty = find(u"empty"_s);
        QVERIFY(empty != nullptr);
        QVERIFY(!empty->isVisible());
        QVERIFY(find(u"tile-0"_s) != nullptr);
        QVERIFY(find(u"end-allapps"_s) != nullptr);
        QVERIFY(find(u"tile-1"_s) == nullptr);
    }

    void allAppsPageListsEveryEntry()
    {
        load(480, 320, entries(12), {}, 1, ids(2));
        launcher->openAllApps();
        QTest::qWait(50);
        QQuickItem *apps = find(u"apps"_s);
        QVERIFY(apps != nullptr);
        QVERIFY(apps->isVisible());
        QQuickItem *home = find(u"home"_s);
        QVERIFY(home != nullptr);
        QVERIFY(!home->isVisible());
        QQuickItem *list = find(u"applist"_s);
        QVERIFY(list != nullptr);
        QCOMPARE(list->property("count").toInt(), 12);
        QCOMPARE(list->property("focusIndex").toInt(), 0);
    }

    void allAppsPadMap()
    {
        load(480, 320, entries(12), {}, 1, ids(2));
        launcher->openAllApps();
        QTest::qWait(50);
        QQuickItem *list = find(u"applist"_s);
        QVERIFY(list != nullptr);
        tap(u"down"_s);
        QTest::qWait(50);
        QCOMPARE(list->property("focusIndex").toInt(), 1);
        tap(u"down"_s);
        QTest::qWait(50);
        QCOMPARE(list->property("focusIndex").toInt(), 2);
        tap(u"up"_s);
        QTest::qWait(50);
        QCOMPARE(list->property("focusIndex").toInt(), 1);
        QCOMPARE(list->property("pageRows").toInt(), 5);
        tap(u"right"_s);
        QTest::qWait(50);
        QCOMPARE(list->property("focusIndex").toInt(), 6);
        tap(u"left"_s);
        QTest::qWait(50);
        QCOMPARE(list->property("focusIndex").toInt(), 1);
        tap(u"a"_s);
        QTest::qWait(50);
        QCOMPARE(activator->starts, QStringList{u"app1.desktop"_s});
    }

    void allAppsRepeatsMoveTheList()
    {
        load(480, 320, entries(12), {}, 1, ids(2));
        launcher->openAllApps();
        QTest::qWait(50);
        QQuickItem *list = find(u"applist"_s);
        QVERIFY(list != nullptr);
        repeatPress(u"down"_s);
        QTest::qWait(50);
        QCOMPARE(list->property("focusIndex").toInt(), 1);
        repeatPress(u"down"_s);
        QTest::qWait(50);
        QCOMPARE(list->property("focusIndex").toInt(), 2);
    }

    void allAppsBackReturnsHome()
    {
        load(480, 320, entries(6), {}, 1, ids(6));
        launcher->openAllApps();
        QTest::qWait(50);
        tap(u"b"_s);
        QTest::qWait(50);
        QCOMPARE(launcher->page(), Launcher::homePage());
        QVERIFY(home->calls.isEmpty());
        QQuickItem *apps = find(u"apps"_s);
        QVERIFY(apps != nullptr);
        QVERIFY(!apps->isVisible());
    }

    void allAppsKeepsItsFocusAcrossThePageSwitch()
    {
        load(480, 320, entries(12), {}, 1, ids(6));
        launcher->openAllApps();
        QTest::qWait(50);
        QQuickItem *list = find(u"applist"_s);
        QVERIFY(list != nullptr);
        tap(u"down"_s);
        tap(u"down"_s);
        QTest::qWait(50);
        QCOMPARE(list->property("focusIndex").toInt(), 2);
        tap(u"b"_s);
        QTest::qWait(50);
        launcher->focusGridAt(3);
        tap(u"y"_s);
        QTest::qWait(50);
        QCOMPARE(launcher->page(), Launcher::appsPage());
        QCOMPARE(list->property("focusIndex").toInt(), 2);
        QCOMPARE(launcher->focusRow(), 0);
        QCOMPARE(launcher->focusIndex(), 3);
    }

    void yOpensTheStoreFromAllApps()
    {
        load(480, 320, entries(6), {}, 1, ids(2));
        launcher->openAllApps();
        QTest::qWait(50);
        tap(u"y"_s);
        QTest::qWait(50);
        QCOMPARE(activator->starts, QStringList{Launcher::storeAppId()});
    }

    void storeEndTileStartsTheStore()
    {
        load(480, 320, entries(6), {}, 1, ids(2));
        QQuickItem *tile = find(u"end-store"_s);
        QVERIFY(tile != nullptr);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, tile->mapToScene(QPointF(10, 10)).toPoint());
        QCOMPARE(activator->starts, QStringList{Launcher::storeAppId()});
    }

    void storeLaunchFailureShowsTheToast()
    {
        load(480, 320, entries(6), {}, 1, ids(2));
        activator->failWith = u"no entry"_s;
        tap(u"y"_s);
        QTest::qWait(50);
        tap(u"y"_s);
        QTest::qWait(50);
        QQuickItem *toast = find(u"toast"_s);
        QVERIFY(toast != nullptr);
        QTRY_VERIFY_WITH_TIMEOUT(toast->property("shown").toBool(), 2000);
        QCOMPARE(toast->property("artist").toString(), u"Store"_s);
        QCOMPARE(toast->property("title").toString(), u"no entry"_s);
        QCOMPARE(launcher->page(), Launcher::appsPage());
    }

    void focusMarksIdleUnderDialogs()
    {
        load(480, 320, entries(12), {}, 1, ids(2));
        QQuickItem *tile = find(u"tile-0"_s);
        QVERIFY(tile != nullptr);
        QQuickItem *border = nullptr;
        for (QQuickItem *child : tile->childItems()) {
            if (QByteArray(child->metaObject()->className()).contains("Rectangle")
                && child->property("color").value<QColor>().alpha() == 0)
                border = child;
        }
        QVERIFY(border != nullptr);
        QVERIFY(border->isVisible());
        QVERIFY(QMetaObject::invokeMethod(window, "askUninstall", Q_ARG(QString, u"app0.desktop"_s),
                                          Q_ARG(QString, u"app 0"_s)));
        QQuickItem *confirm = find(u"confirm"_s);
        QTRY_VERIFY(confirm->property("shown").toBool());
        QVERIFY(!border->isVisible());
        QVERIFY(QMetaObject::invokeMethod(confirm, "close"));
        QTRY_VERIFY(border->isVisible());

        launcher->openAllApps();
        QTest::qWait(50);
        QObject *list = find(u"applist"_s);
        QVERIFY(list != nullptr);
        QQuickItem *row = nullptr;
        QVERIFY(QMetaObject::invokeMethod(list, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, row), Q_ARG(int, 0)));
        QVERIFY(row != nullptr);
        QQuickItem *ring = qvariant_cast<QQuickItem *>(
            qvariant_cast<QQuickItem *>(row->property("rowItem"))->property("ringItem"));
        QVERIFY(ring != nullptr);
        const qreal focusedStroke = ring->property("strokeWidth").toReal();
        launcher->ShowPower();
        QTRY_VERIFY_WITH_TIMEOUT(find(u"power"_s)->property("shown").toBool(), 2000);
        QVERIFY(ring->property("strokeWidth").toReal() < focusedStroke);
    }

    void powerPageTakesThePressOverAllApps()
    {
        load(480, 320, entries(12), {}, 1, ids(12));
        launcher->openAllApps();
        QTest::qWait(50);
        launcher->ShowPower();
        QTRY_VERIFY_WITH_TIMEOUT(find(u"power"_s)->property("shown").toBool(), 2000);
        tap(u"y"_s);
        QTest::qWait(50);
        QCOMPARE(launcher->page(), Launcher::appsPage());
        QVERIFY(activator->starts.isEmpty());
        QVERIFY(launcher->power());
    }

    void toastDoesNotBlockThePage()
    {
        load(480, 320, entries(12), {}, 1, ids(12));
        launcher->openAllApps();
        QTest::qWait(50);
        QMetaObject::invokeMethod(launcher, "uninstallFailed", Q_ARG(QString, u"App"_s),
                                  Q_ARG(QString, u"not installed"_s));
        QQuickItem *toast = find(u"toast"_s);
        QVERIFY(toast != nullptr);
        QTRY_VERIFY_WITH_TIMEOUT(toast->property("shown").toBool(), 2000);
        QQuickItem *list = find(u"applist"_s);
        const int focus = list->property("focusIndex").toInt();
        tap(u"down"_s);
        QTest::qWait(50);
        QCOMPARE(list->property("focusIndex").toInt(), focus + 1);
    }

    void longPressIsDroppedWhileLaunching()
    {
        load(480, 320, entries(4), {}, 1, ids(4));
        QQuickItem *homeItem = find(u"home"_s);
        QVERIFY(homeItem != nullptr);
        QQuickItem *tile = find(u"tile-0"_s);
        QVERIFY(tile != nullptr);
        activator->hold = true;
        launcher->launchApp(u"app0.desktop"_s);
        QVERIFY(launcher->launching());
        bool handled = true;
        QVERIFY(QMetaObject::invokeMethod(homeItem, "touchContext", Q_RETURN_ARG(bool, handled),
                                          Q_ARG(QString, u"tile"_s), Q_ARG(int, 0), Q_ARG(QQuickItem *, tile)));
        QVERIFY(!handled);
        QQuickItem *sheet = find(u"sheet"_s);
        QVERIFY(sheet != nullptr);
        QVERIFY(!sheet->property("shown").toBool());
        activator->release();
    }

    void heartToggleKeepsTheDeepRowInPlace()
    {
        load(480, 320, entries(30), {}, 1, ids(30));
        launcher->openAllApps();
        QTest::qWait(50);
        QQuickItem *list = find(u"applist"_s);
        QVERIFY(list != nullptr);
        QVERIFY(QMetaObject::invokeMethod(list, "snapTo", Q_ARG(int, 20)));
        QTest::qWait(50);
        list->setProperty("contentY", list->property("contentY").toReal() + 4 * list->property("rowPitch").toReal());
        QTest::qWait(50);
        const qreal contentY = list->property("contentY").toReal();
        const int focus = list->property("focusIndex").toInt();
        QCOMPARE(focus, 20);
        QVERIFY(contentY > 0);
        launcher->toggleFavorite(u"app20.desktop"_s);
        QTest::qWait(50);
        QCOMPARE(list->property("contentY").toReal(), contentY);
        QCOMPARE(list->property("focusIndex").toInt(), focus);
        QCOMPARE(launcher->favoriteIds().indexOf(u"app20.desktop"_s), -1);
    }

#ifdef MICROSET_STORE_CORE
    void uninstallFailureShowsTheToast()
    {
        QVariantList rows = entries(2);
        rows << QVariantMap{{u"id"_s, u"org.example.App.desktop"_s},
                            {u"name"_s, u"App"_s},
                            {u"flatpak"_s, u"org.example.App"_s}};
        load(480, 320, rows, {}, 1, {u"app0.desktop"_s});
        auto backend = std::make_unique<FakeBackend>();
        auto *fake = backend.get();
        store = std::make_unique<StoreWorker>(std::move(backend));
        store->start();
        launcher->setStore(store.get());
        fake->setUninstallFails(u"org.example.App"_s, u"not installed"_s);
        launcher->uninstallApp(u"org.example.App.desktop"_s);
        QQuickItem *toast = find(u"toast"_s);
        QVERIFY(toast != nullptr);
        QTRY_VERIFY_WITH_TIMEOUT(toast->property("shown").toBool(), 2000);
        QCOMPARE(toast->property("artist").toString(), u"App"_s);
        QCOMPARE(toast->property("title").toString(), u"not installed"_s);
    }
#endif

    void xOnAFavoriteOpensTheSheet()
    {
        load(480, 320, entries(6), {}, 1, ids(2));
        launcher->focusGridAt(0);
        tap(u"x"_s);
        QTest::qWait(500);
        QQuickItem *sheet = find(u"sheet"_s);
        QVERIFY(sheet != nullptr);
        QVERIFY(sheet->property("shown").toBool());
        QCOMPARE(sheet->property("items").toStringList(), QStringList{u"remove from favorites"_s});
    }

    void sheetTakesThePressesFirst()
    {
        QVariantList rows = entries(2);
        rows << QVariantMap{{u"id"_s, u"org.example.App.desktop"_s},
                            {u"name"_s, u"App"_s},
                            {u"flatpak"_s, u"org.example.App"_s}};
        load(480, 320, rows, {}, 1, QStringList{u"org.example.App.desktop"_s});
        launcher->focusGridAt(0);
        tap(u"x"_s);
        QTest::qWait(500);
        QQuickItem *sheet = find(u"sheet"_s);
        QVERIFY(sheet != nullptr);
        QVERIFY(sheet->property("shown").toBool());
        QCOMPARE(sheet->property("items").toStringList(), QStringList({u"remove from favorites"_s, u"uninstall"_s}));
        tap(u"down"_s);
        QTest::qWait(50);
        QCOMPARE(sheet->property("focusIndex").toInt(), 1);
        QCOMPARE(launcher->focusIndex(), 0);
        QCOMPARE(launcher->page(), Launcher::homePage());
        tap(u"a"_s);
        QTest::qWait(500);
        QQuickItem *confirm = find(u"confirm"_s);
        QVERIFY(confirm != nullptr);
        QVERIFY(confirm->property("shown").toBool());
        QCOMPARE(confirm->property("items").toStringList(), QStringList({u"uninstall"_s, u"keep"_s}));
        QVERIFY(!sheet->property("shown").toBool());
        tap(u"b"_s);
        QTest::qWait(500);
        QVERIFY(!confirm->property("shown").toBool());
        QCOMPARE(favorites->ids(), QStringList{u"org.example.App.desktop"_s});
    }

    void sheetRemovesTheFavorite()
    {
        load(480, 320, entries(6), {}, 1, ids(2));
        launcher->focusGridAt(0);
        tap(u"x"_s);
        QTest::qWait(500);
        tap(u"a"_s);
        QTest::qWait(500);
        QCOMPARE(favorites->ids(), QStringList{u"app1.desktop"_s});
        QQuickItem *sheet = find(u"sheet"_s);
        QVERIFY(sheet != nullptr);
        QVERIFY(!sheet->property("shown").toBool());
    }

    void holdingAFavoriteOpensTheSheet()
    {
        load(480, 320, entries(6), {}, 1, ids(2));
        QQuickItem *tile = find(u"tile-0"_s);
        QVERIFY(tile != nullptr);
        const QPoint at = tile->mapToScene(QPointF(10, 10)).toPoint();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, at);
        QTest::qWait(600);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, at);
        QTest::qWait(300);
        QQuickItem *sheet = find(u"sheet"_s);
        QVERIFY(sheet != nullptr);
        QVERIFY(sheet->property("shown").toBool());
        QVERIFY(activator->starts.isEmpty());
    }

    void pulseOncePerPress()
    {
        load(480, 320, entries(6), {}, 1, ids(2));
        QCOMPARE(idle->pulseCount(), 0);
        tap(u"right"_s);
        QTest::qWait(50);
        QCOMPARE(idle->pulseCount(), 1);
        QTest::qWait(1100);
        launcher->openAllApps();
        QTest::qWait(50);
        tap(u"down"_s);
        QTest::qWait(50);
        QCOMPARE(idle->pulseCount(), 2);
        QTest::qWait(1100);
        tap(u"x"_s);
        QTest::qWait(600);
        QCOMPARE(find(u"sheet"_s)->property("shown").toBool(), true);
        QTest::qWait(1100);
        tap(u"b"_s);
        QTest::qWait(600);
        QCOMPARE(idle->pulseCount(), 4);
    }

    void runningRowSitsOverTheGrid()
    {
        load(480, 320, entries(10), windows(2), 1, ids(10));
        QQuickItem *running = find(u"running"_s);
        QVERIFY(running != nullptr);
        QVERIFY(running->isVisible());
        QCOMPARE(int(qRound(running->x())), 20 - 2);
        QCOMPARE(int(qRound(running->y())), 98 - 2);
        QCOMPARE(int(qRound(running->height())), 84 + 2);
        QQuickItem *tile = find(u"running-0"_s);
        QVERIFY(tile != nullptr);
        QCOMPARE(int(qRound(tile->width())), 60);
        QCOMPARE(int(qRound(tile->height())), 60);
        QQuickItem *grid = find(u"grid"_s);
        QVERIFY(grid != nullptr);
        QCOMPARE(int(qRound(grid->y())), 98 + 96 - 2);
        QVERIFY(int(qRound(running->y() + running->height())) < int(qRound(grid->y())));
    }

    void runningRowShowsTheCaptionAndBottomBorder()
    {
        load(480, 320, entries(10), windows(2), 1, ids(10));
        QQuickItem *tile = find(u"running-0"_s);
        QVERIFY(tile != nullptr);
        launcher->focusRunningAt(0);
        QTest::qWait(50);
        const QImage shot = window->grabWindow();
        QVERIFY(!shot.isNull());
        const QPoint corner = tile->mapToScene(QPointF(0, 0)).toPoint();
        const int mid = int(tile->width() / 2);
        const int bottom = corner.y() + int(tile->height());
        const QColor border = shot.pixelColor(corner.x() + mid, bottom);
        QVERIFY2(border.red() > 0xC0 && border.green() > 0xC0 && border.blue() > 0xC0,
                 qPrintable(QStringLiteral("running bottom border is %1, not white").arg(border.name())));
        const QQuickItem *title = tile->property("titleItem").value<QQuickItem *>();
        QVERIFY(title != nullptr);
        QVERIFY(title->isVisible());
        QCOMPARE(title->property("text").toString(), u"window 1"_s);
        const QPoint titleAt = title->mapToScene(QPointF(1, title->height() / 2)).toPoint();
        const QColor text = shot.pixelColor(titleAt.x(), titleAt.y());
        QVERIFY2(text.red() > 0x30,
                 qPrintable(QStringLiteral("running caption pixel is %1, too dark").arg(text.name())));
    }

    void gridScrollsToFocus()
    {
        load(480, 320, entries(30), {}, 1, ids(30));
        QQuickItem *grid = find(u"grid"_s);
        QVERIFY(grid != nullptr);
        QCOMPARE(grid->property("contentY").toReal(), -2.0);
        launcher->focusGridAt(18);
        QTest::qWait(50);
        QVERIFY(grid->property("contentY").toReal() > 0);
        const qreal top = 3 * 96 - 2;
        QVERIFY(grid->property("contentY").toReal() <= top + 0.5);
        launcher->focusGridAt(0);
        QTest::qWait(50);
        QCOMPARE(grid->property("contentY").toReal(), -2.0);
    }

    void runningRowScrollsToFocus()
    {
        load(480, 320, entries(2), windows(12));
        QQuickItem *running = find(u"running"_s);
        QVERIFY(running != nullptr);
        launcher->focusRunningAt(0);
        QTest::qWait(50);
        QCOMPARE(running->property("contentX").toReal(), -2.0);
        launcher->focusRunningAt(10);
        QTest::qWait(50);
        QVERIFY(running->property("contentX").toReal() > 0);
        QVERIFY(running->property("contentX").toReal() <= 10 * 72 - 2 + 0.5);
        launcher->focusRunningAt(0);
        QTest::qWait(50);
        QCOMPARE(running->property("contentX").toReal(), -2.0);
    }

    static QString rawFor(const QString &onScreen, int padTurn)
    {
        const QStringList dirs{u"up"_s, u"right"_s, u"down"_s, u"left"_s};
        const QStringList faces{u"y"_s, u"b"_s, u"a"_s, u"x"_s};
        const QStringList *names = dirs.indexOf(onScreen) >= 0 ? &dirs : &faces;
        const int index = names->indexOf(onScreen);
        if (index < 0)
            return onScreen;
        return names->at(((index - padTurn) % 4 + 4) % 4);
    }

    void tap(const QString &onScreen) { pad->fire(rawFor(onScreen, launcher->padTurn())); }

    void repeatPress(const QString &onScreen) { pad->hold(rawFor(onScreen, launcher->padTurn())); }

    void padUpMovesFocusUpOnScreen()
    {
        for (uint turn : {1u, 0u, 3u, 2u}) {
            load(480, 320, entries(18), {}, turn, ids(18));
            launcher->focusGridAt(8);
            QCOMPARE(launcher->focusRow(), 1);
            QCOMPARE(launcher->padTurn(), Launcher::turnFor(turn, 1));
            pad->fire(rawFor(u"up"_s, launcher->padTurn()));
            QTest::qWait(20);
            QCOMPARE(launcher->focusRow(), 0);
            pad->fire(rawFor(u"down"_s, launcher->padTurn()));
            QTest::qWait(20);
            QCOMPARE(launcher->focusRow(), 1);
        }
    }

    void oneRawSignalIsOneMove()
    {
        load(480, 320, entries(18), {}, 1, ids(18));
        QCOMPARE(launcher->padTurn(), 2);
        pad->fire(rawFor(u"right"_s, launcher->padTurn()));
        QTest::qWait(20);
        QCOMPARE(launcher->focusIndex(), 1);
        QCOMPARE(launcher->focusRow(), 0);
        pad->fire(rawFor(u"down"_s, launcher->padTurn()));
        QTest::qWait(20);
        QCOMPARE(launcher->focusRow(), 1);
        QCOMPARE(launcher->focusIndex(), 1);
    }

    void padPressesRouteByTheLauncherTurn()
    {
        struct Case {
            uint transform;
            QString raw;
            QString routed;
        };
        const Case cases[]{
            {3, u"left"_s, u"left"_s},
            {1, u"left"_s, u"right"_s},
            {0, u"right"_s, u"down"_s},
        };
        for (const Case &c : cases) {
            load(480, 320, entries(18), {}, c.transform, ids(18));
            QCOMPARE(launcher->padTurn(), (1 + int(c.transform)) % 4);
            QQuickItem *router = find(u"router"_s);
            QVERIFY(router != nullptr);
            QSignalSpy routed(router, SIGNAL(routed(QString, bool, bool)));
            QVERIFY(routed.isValid());
            pad->press(c.raw);
            QTRY_COMPARE(routed.count(), 1);
            QCOMPARE(routed.at(0).at(0).toString(), c.routed);
            QCOMPARE(routed.at(0).at(1).toBool(), false);
            pad->release(c.raw);
        }
    }

    void faceButtonsTurnToo()
    {
        load(480, 320, entries(6), {}, 0, ids(6));
        QCOMPARE(launcher->padTurn(), 1);
        const QStringList faces{u"y"_s, u"b"_s, u"a"_s, u"x"_s};
        const QString rawA = faces.at(((2 - launcher->padTurn()) % 4 + 4) % 4);
        QCOMPARE(rawA, u"b"_s);
        pad->fire(rawA);
        QTest::qWait(20);
        QCOMPARE(activator->starts, QStringList{u"app0.desktop"_s});
    }

    void touchTapStartsTheGridApp()
    {
        load(480, 320, entries(6), {}, 1, ids(6));
        QQuickItem *tile = find(u"tile-2"_s);
        QVERIFY(tile != nullptr);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, tile->mapToScene(QPointF(10, 10)).toPoint());
        QCOMPARE(activator->starts, QStringList{u"app2.desktop"_s});
    }

    void touchTapOnRunningTileFocusesIt()
    {
        load(480, 320, entries(6), windows(3));
        QQuickItem *tile = find(u"running-1"_s);
        QVERIFY(tile != nullptr);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, tile->mapToScene(QPointF(10, 10)).toPoint());
        QCOMPARE(home->calls, QStringList{u"FocusWindow"_s});
        QCOMPARE(home->focused, QStringList{u"0x2"_s});
    }

    void powerPagePadDrivesTheRows()
    {
        load(480, 320, entries(6), {}, 3);
        launcher->ShowPower();
        QTest::qWait(500);
        QQuickItem *page = find(u"power"_s);
        QVERIFY(page != nullptr);
        QVERIFY(page->property("shown").toBool());
        pad->fire(u"down"_s);
        QTest::qWait(20);
        QCOMPARE(page->property("focusIndex").toInt(), 1);
        pad->fire(u"a"_s);
        QTest::qWait(200);
        QCOMPARE(power->verbs, QStringList{u"reboot"_s});
        QVERIFY(!launcher->power());
    }

    void powerPageScrimTapDismisses()
    {
        load(480, 320, entries(6));
        launcher->ShowPower();
        QTest::qWait(500);
        QQuickItem *page = find(u"power"_s);
        QVERIFY(page != nullptr);
        QVERIFY(page->property("shown").toBool());
        QQuickItem *scrim = page->property("scrim").value<QQuickItem *>();
        QVERIFY(scrim != nullptr);
        const QPoint at = scrim->mapToScene(QPointF(2, 2)).toPoint();
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, at);
        QTRY_VERIFY_WITH_TIMEOUT(!launcher->power(), 2000);
        QVERIFY(power->verbs.isEmpty());
        QVERIFY(!home->calls.contains(u"Leave"_s));
    }

    void powerPageRowTapRunsTheVerb()
    {
        load(480, 320, entries(6));
        launcher->ShowPower();
        QTest::qWait(500);
        QQuickItem *page = find(u"power"_s);
        QVERIFY(page != nullptr);
        QQuickItem *row = page->property("list").value<QQuickItem *>();
        QVERIFY(row != nullptr);
        QQuickItem *third = nullptr;
        for (QQuickItem *child : row->childItems()) {
            if (child->property("index").toInt() == 2)
                third = child;
        }
        QVERIFY(third != nullptr);
        const QPoint at = third->mapToScene(QPointF(20, 10)).toPoint();
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, at);
        QTRY_VERIFY_WITH_TIMEOUT(!power->verbs.isEmpty(), 2000);
        QCOMPARE(power->verbs, QStringList{u"power off"_s});
        QVERIFY(!launcher->power());
    }

    void powerPageRenders()
    {
        load(480, 320, entries(6));
        QQuickItem *page = find(u"power"_s);
        QVERIFY(page != nullptr);
        QCOMPARE(page->opacity(), 1.0);
        QVERIFY(!page->property("shown").toBool());
        QSignalSpy shown(page, SIGNAL(shownChanged()));
        launcher->ShowPower();
        QTRY_VERIFY_WITH_TIMEOUT(page->property("shown").toBool(), 2000);
        QVERIFY(shown.count() >= 1);
        QTest::qWait(400);
        QVERIFY(page->opacity() > 0.0);
        QVERIFY(page->property("visible").toBool());
        launcher->closePower();
        QTRY_VERIFY_WITH_TIMEOUT(!page->property("shown").toBool(), 2000);
    }

    void focusBorderIsNotClipped()
    {
        load(480, 320, entries(6), {}, 1, ids(6));
        QQuickItem *tile = find(u"tile-0"_s);
        QVERIFY(tile != nullptr);
        launcher->focusGridAt(0);
        QTest::qWait(50);
        QVERIFY(tile->property("focused").toBool());
        focusBorderVisible(tile, u"grid"_s);
    }

    void runningFocusBorderIsNotClipped()
    {
        load(480, 320, entries(6), windows(4));
        QQuickItem *tile = find(u"running-0"_s);
        QVERIFY(tile != nullptr);
        launcher->focusRunningAt(0);
        QTest::qWait(50);
        QVERIFY(tile->property("focused").toBool());
        focusBorderVisible(tile, u"running"_s);
    }

    void focusBorderVisible(QQuickItem *tile, const QString &what)
    {
        const QImage shot = window->grabWindow();
        QVERIFY(!shot.isNull());
        const QPoint corner = tile->mapToScene(QPointF(0, 0)).toPoint();
        const int mid = int(tile->width() / 2);
        const int low = int(tile->height() / 2);
        const QColor left = shot.pixelColor(corner.x() - 1, corner.y() + low);
        const QColor top = shot.pixelColor(corner.x() + mid, corner.y() - 1);
        QVERIFY2(left.red() > 0xC0 && left.green() > 0xC0 && left.blue() > 0xC0,
                 qPrintable(QStringLiteral("%1 left border is %2, not white").arg(what, left.name())));
        QVERIFY2(top.red() > 0xC0 && top.green() > 0xC0 && top.blue() > 0xC0,
                 qPrintable(QStringLiteral("%1 top border is %2, not white").arg(what, top.name())));
    }

    void longSsidElides()
    {
        load(480, 320, entries(6));
        launcher->setWifi(true, QString(32, u'a'));
        QTest::qWait(50);
        QQuickItem *ssid = nullptr;
        QList<QQuickItem *> pending{root};
        while (!pending.isEmpty()) {
            QQuickItem *item = pending.takeFirst();
            if (item->property("text").toString() == QString(32, u'a'))
                ssid = item;
            pending += item->childItems();
        }
        QVERIFY(ssid != nullptr);
        QVERIFY(ssid->isVisible());
        QCOMPARE(ssid->property("elide").toInt(), int(Qt::ElideRight));
        QVERIFY(ssid->width() <= 480 / 2 - 16 + 0.5);
    }
};

QTEST_MAIN(LayoutTest)

#include "tst_launcherlayout.moc"
