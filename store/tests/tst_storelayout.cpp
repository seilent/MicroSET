#include "accentwatch.h"
#include "fakebackend.h"
#include "idlepulse.h"
#include "storejob.h"
#include "storeprocess.h"
#include "storeworker.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
#include <QtQml/qqmlextensionplugin.h>

#include <memory>

Q_IMPORT_QML_PLUGIN(MicroSetPlugin)
Q_IMPORT_QML_PLUGIN(MicroSetStorePlugin)

using namespace Qt::StringLiterals;

namespace {

StoreEntry entryFor(const QString &id, const QString &name, const QString &summary = QString())
{
    StoreEntry entry;
    entry.id = id;
    entry.name = name;
    entry.summary = summary.isEmpty() ? name + u" summary"_s : summary;
    entry.developer = u"Example Developer"_s;
    return entry;
}

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

    QMap<int, QString> names{{0, u"up"_s}, {1, u"down"_s}, {2, u"left"_s}, {3, u"right"_s},
                             {4, u"a"_s}, {5, u"b"_s}, {6, u"x"_s}, {7, u"y"_s}, {8, u"back"_s}};

signals:
    void action(int action, bool repeat);
};

class FakeIdle : public IdlePulse
{
public:
    using IdlePulse::IdlePulse;

    void pulse() override { counted(); }
    bool ready() const override { return true; }
};

class TestStore : public StoreProcess
{
    Q_OBJECT

public:
    using StoreProcess::StoreProcess;

    QStringList starts;
    bool failLaunch = false;

protected:
    bool startApp(const QString &desktopId) override
    {
        starts << desktopId;
        return !failLaunch;
    }
};

}

class StoreLayoutTest : public QObject
{
    Q_OBJECT

    FakeBackend *fake = nullptr;
    std::unique_ptr<StoreWorker> worker;
    TestStore *store = nullptr;
    FakeIdle *idle = nullptr;
    FakePad *pad = nullptr;
    QQmlApplicationEngine *engine = nullptr;
    QQuickWindow *window = nullptr;
    QQuickItem *root = nullptr;
    QProcess *busProcess = nullptr;
    std::unique_ptr<QTemporaryDir> busDir;
    QString address;
    QTemporaryDir iconDir;

    void stopBus()
    {
        if (busProcess != nullptr) {
            busProcess->terminate();
            if (!busProcess->waitForFinished(3000)) {
                busProcess->kill();
                busProcess->waitForFinished();
            }
            delete busProcess;
            busProcess = nullptr;
        }
        if (!address.isEmpty()) {
            QDBusConnection::disconnectFromBus(u"store-bus"_s);
            QDBusConnection::disconnectFromBus(u"store-layout"_s);
            address.clear();
        }
        busDir.reset();
    }

    bool startBus()
    {
        stopBus();
        busDir = std::make_unique<QTemporaryDir>();
        if (!busDir->isValid())
            return false;
        const QString socket = busDir->filePath(u"bus"_s);
        busProcess = new QProcess();
        busProcess->setStandardOutputFile(QProcess::nullDevice());
        busProcess->setStandardErrorFile(QProcess::nullDevice());
        busProcess->start(u"dbus-daemon"_s,
                          {u"--session"_s, u"--nofork"_s, u"--nopidfile"_s, u"--address=unix:path="_s + socket});
        if (!busProcess->waitForStarted())
            return false;
        QElapsedTimer ready;
        ready.start();
        while (!QFile::exists(socket) && ready.elapsed() < 10000)
            QTest::qWait(20);
        if (!QFile::exists(socket))
            return false;
        address = u"unix:path="_s + socket;
        return true;
    }

    void reset()
    {
        delete engine;
        engine = nullptr;
        window = nullptr;
        root = nullptr;
        delete store;
        store = nullptr;
        idle = nullptr;
        delete pad;
        pad = nullptr;
        worker.reset();
        fake = nullptr;
        stopBus();
    }

    void load(const QList<StoreEntry> &entries, const QList<StoreEntry> &installed = {})
    {
        reset();
        auto backend = std::make_unique<FakeBackend>();
        fake = backend.get();
        fake->setEntries(entries);
        fake->setInstalled(installed);
        worker = std::make_unique<StoreWorker>(std::move(backend));
        worker->start();
        store = new TestStore(worker.get(), QDBusConnection(u"store-layout"_s));
        idle = new FakeIdle();
        store->setIdle(idle);
        pad = new FakePad();
        engine = new QQmlApplicationEngine();
        engine->setInitialProperties({
            {u"store"_s, QVariant::fromValue(static_cast<QObject *>(store))},
            {u"pad"_s, QVariant::fromValue(static_cast<QObject *>(pad))},
            {u"animate"_s, false},
        });
        engine->loadFromModule("MicroSetStore", "Store");
        window = qobject_cast<QQuickWindow *>(engine->rootObjects().value(0));
        QVERIFY(window != nullptr);
        window->resize(480, 320);
        window->show();
        root = window->contentItem();
        QTest::qWait(50);
    }

    void open(int count)
    {
        store->show();
        QTRY_COMPARE_WITH_TIMEOUT(store->count(), count, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(pageCount(), 1, 5000);
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

    QObject *current() const
    {
        QObject *stack = find(u"router"_s)->property("stack").value<QObject *>();
        if (stack == nullptr)
            return nullptr;
        return qvariant_cast<QObject *>(stack->property("currentItem"));
    }

    int depth() const
    {
        QObject *stack = find(u"router"_s)->property("stack").value<QObject *>();
        return stack == nullptr ? 0 : stack->property("depth").toInt();
    }

    int pageCount() const
    {
        return depth();
    }

    QVariantList pageRows() const
    {
        QObject *page = current();
        return page == nullptr ? QVariantList() : page->property("rows").toList();
    }

    QStringList rowKeys() const
    {
        QStringList out;
        for (const QVariant &row : pageRows())
            out << row.toMap().value(u"key"_s).toString();
        return out;
    }

    QString rowValue(const QString &key) const
    {
        for (const QVariant &row : pageRows()) {
            const QVariantMap map = row.toMap();
            if (map.value(u"key"_s).toString() == key)
                return map.value(u"value"_s).toString();
        }
        return {};
    }

    QString rowTitle(const QString &key) const
    {
        for (const QVariant &row : pageRows()) {
            const QVariantMap map = row.toMap();
            if (map.value(u"key"_s).toString() == key)
                return map.value(u"title"_s).toString();
        }
        return {};
    }

    void tap(const QString &name) { pad->fire(name); }

    void openDetail(const QString &id)
    {
        store->open(id);
        QMetaObject::invokeMethod(window, "openDetail", Q_ARG(QString, id));
        QTRY_COMPARE_WITH_TIMEOUT(depth(), 2, 5000);
    }

    QList<StoreEntry> entries(int count)
    {
        QList<StoreEntry> out;
        for (int i = 0; i < count; ++i)
            out << entryFor(u"org.example.App%1"_s.arg(i), u"app %1"_s.arg(i));
        return out;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(iconDir.isValid());
        QImage image(64, 64, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(70, 110, 190));
        painter.drawRoundedRect(4, 4, 56, 56, 14, 14);
        painter.end();
        QVERIFY(image.save(iconDir.filePath(u"icon.png"_s), "PNG"));
    }

    void cleanup() { reset(); }

    void cleanupTestCase() { stopBus(); }

    void accentFollowsWatch()
    {
        load({});
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

    void desktopIdComesFromTheExportsDir()
    {
        load({});
        QTemporaryDir home;
        const QString apps = home.filePath(u"flatpak/exports/share/applications"_s);
        QVERIFY(QDir().mkpath(apps));
        QFile file(apps + u"/org.example.Other.desktop"_s);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QVERIFY(file.write("[Desktop Entry]\nType=Application\nX-Flatpak=org.example.App0\n") > 0);
        file.close();
        const QByteArray before = qgetenv("XDG_DATA_HOME");
        qputenv("XDG_DATA_HOME", home.path().toUtf8());
        QCOMPARE(store->desktopId(u"org.example.App0"_s), u"org.example.Other.desktop"_s);
        QCOMPARE(store->desktopId(u"org.example.Absent"_s), u"org.example.Absent.desktop"_s);
        QFile direct(apps + u"/org.example.Direct.desktop"_s);
        QVERIFY(direct.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QVERIFY(direct.write("[Desktop Entry]\nType=Application\nX-Flatpak=org.example.Other\n") > 0);
        direct.close();
        QCOMPARE(store->desktopId(u"org.example.Direct"_s), u"org.example.Direct.desktop"_s);
        if (before.isEmpty())
            qunsetenv("XDG_DATA_HOME");
        else
            qputenv("XDG_DATA_HOME", before);
    }

    void openShowsTheCachedCatalog()
    {
        QList<StoreEntry> rows = entries(3);
        rows[0].icon = iconDir.filePath(u"icon.png"_s);
        load(rows, {entryFor(u"org.example.App1"_s, u"app 1"_s)});
        open(3);
        QQuickItem *list = find(u"storelist-rows"_s);
        QVERIFY(list != nullptr);
        QCOMPARE(list->property("count").toInt(), 3);
        QCOMPARE(list->property("focusIndex").toInt(), 0);
        const QVariantList catalog = store->rows();
        QCOMPARE(catalog.at(0).toMap().value(u"name"_s).toString(), u"app 0"_s);
        QCOMPARE(catalog.at(1).toMap().value(u"installed"_s).toBool(), true);
        QCOMPARE(catalog.at(0).toMap().value(u"installed"_s).toBool(), false);
        QVERIFY(find(u"store-empty"_s) != nullptr);
        QVERIFY(!find(u"store-empty"_s)->isVisible());
    }

    void quitReleasesTheCatalog()
    {
        load(entries(2));
        open(2);
        QSignalSpy quit(store, &StoreProcess::quitRequested);
        tap(u"b"_s);
        QTRY_COMPARE(quit.count(), 1);
        QCOMPARE(store->rows().size(), 0);
        QCOMPARE(store->shown(), false);
    }

    void oneSyncPerOpen()
    {
        load(entries(2));
        open(2);
        QCOMPARE(fake->syncCount(), 1);
        store->hide();
        QTest::qWait(50);
        QCOMPARE(store->rows().size(), 0);
        open(2);
        QCOMPARE(fake->syncCount(), 2);
    }

    void installedMarksSurviveHideAndShow()
    {
        load(entries(2), {entryFor(u"org.example.App0"_s, u"app 0"_s)});
        open(2);
        QCOMPARE(store->rows().at(0).toMap().value(u"installed"_s).toBool(), true);
        store->hide();
        QTest::qWait(50);
        open(2);
        QCOMPARE(store->rows().at(0).toMap().value(u"installed"_s).toBool(), true);
        QCOMPARE(store->rows().at(1).toMap().value(u"installed"_s).toBool(), false);
    }

    void resultsAfterHideAreDropped()
    {
        load(entries(2));
        fake->setSyncDelayMs(400);
        store->show();
        QTRY_COMPARE_WITH_TIMEOUT(store->count(), 2, 5000);
        store->hide();
        QTest::qWait(600);
        QCOMPARE(store->rows().size(), 0);
        QCOMPARE(store->offline(), true);
        QCOMPARE(store->shown(), false);
        open(2);
        QCOMPARE(store->rows().size(), 2);
        QCOMPARE(store->offline(), false);
    }

    void quitWhileBusyWaitsForTheQueue()
    {
        load(entries(1));
        open(1);
        openDetail(u"org.example.App0"_s);
        fake->setInstallSteps(20);
        fake->setInstallDelayMs(40);
        QSignalSpy quit(store, &StoreProcess::quitRequested);
        tap(u"a"_s);
        QTRY_VERIFY_WITH_TIMEOUT(store->busy(), 5000);
        store->quit();
        QVERIFY(store->quitting());
        QCOMPARE(quit.count(), 0);
        QCOMPARE(store->shown(), false);
        QTRY_COMPARE_WITH_TIMEOUT(quit.count(), 1, 15000);
        QVERIFY(!store->quitting());
    }

    void rowsWithoutAnIconShowTheLetterTile()
    {
        load({entryFor(u"org.example.App0"_s, u"Alpha"_s), entryFor(u"org.example.App1"_s, u"Beta"_s)});
        open(2);
        QQuickItem *list = find(u"storelist-rows"_s);
        QVERIFY(list != nullptr);
        QCOMPARE(list->property("count").toInt(), 2);
        const QVariantList rows = store->rows();
        QCOMPARE(rows.at(0).toMap().value(u"letter"_s).toString(), u"A"_s);
        QCOMPARE(rows.at(1).toMap().value(u"letter"_s).toString(), u"B"_s);
        QCOMPARE(rows.at(0).toMap().value(u"iconUrl"_s).toString(), QString());
    }

    void iconPathsBecomeUrls()
    {
        QList<StoreEntry> rows = entries(1);
        rows[0].icon = iconDir.filePath(u"a b.png"_s);
        load(rows);
        open(1);
        QCOMPARE(store->rows().at(0).toMap().value(u"iconUrl"_s).toString(),
                 QUrl::fromLocalFile(iconDir.filePath(u"a b.png"_s)).toString());
    }

    void activateAndShowReachTheProcessOverTheBus()
    {
        load(entries(1));
        QVERIFY(startBus());
        QDBusConnection bus = QDBusConnection::connectToBus(address, u"store-bus"_s);
        QVERIFY(bus.isConnected());
        QVERIFY(store->publish(bus));
        QCOMPARE(store->shown(), false);

        QDBusMessage activate = QDBusMessage::createMethodCall(
            StoreProcess::service(), StoreProcess::path(), u"org.freedesktop.Application"_s, u"Activate"_s);
        activate << QVariant::fromValue(QVariantMap());
        const QDBusMessage reply = bus.call(activate, QDBus::Block, 5000);
        QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
        QTRY_COMPARE_WITH_TIMEOUT(store->shown(), true, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(store->count(), 1, 5000);

        store->hide();
        QCOMPARE(store->shown(), false);

        QDBusMessage show = QDBusMessage::createMethodCall(StoreProcess::service(), StoreProcess::path(),
                                                           StoreProcess::interface(), u"Show"_s);
        const QDBusMessage shown = bus.call(show, QDBus::Block, 5000);
        QCOMPARE(shown.type(), QDBusMessage::ReplyMessage);
        QTRY_COMPARE_WITH_TIMEOUT(store->shown(), true, 5000);

        bus.unregisterObject(StoreProcess::path());
        bus.unregisterService(StoreProcess::service());
    }

    void offlineStateWhenNeverSynced()
    {
        load({});
        fake->setSyncFails(true, u"offline"_s);
        store->show();
        QTRY_VERIFY_WITH_TIMEOUT(find(u"store-empty"_s)->isVisible(), 5000);
        QCOMPARE(find(u"store-empty"_s)->property("text").toString(), u"The store is offline."_s);
        QCOMPARE(store->offline(), true);
    }

    void emptySearchResult()
    {
        load(entries(3));
        open(3);
        store->setSearch(u"zzz"_s);
        QTest::qWait(50);
        QCOMPARE(store->count(), 0);
        QVERIFY(find(u"store-empty"_s)->isVisible());
        QCOMPARE(find(u"store-empty"_s)->property("text").toString(), u"No apps found."_s);
    }

    void listMovesByRowAndPage()
    {
        load(entries(20));
        open(20);
        QQuickItem *list = find(u"storelist-rows"_s);
        QVERIFY(list != nullptr);
        tap(u"down"_s);
        tap(u"down"_s);
        QTest::qWait(50);
        QCOMPARE(list->property("focusIndex").toInt(), 2);
        tap(u"up"_s);
        QTest::qWait(50);
        QCOMPARE(list->property("focusIndex").toInt(), 1);
        const int pageRows = list->property("pageRows").toInt();
        QVERIFY(pageRows > 1);
        tap(u"right"_s);
        QTest::qWait(200);
        QCOMPARE(list->property("focusIndex").toInt(), 1 + pageRows);
        tap(u"left"_s);
        QTest::qWait(200);
        QCOMPARE(list->property("focusIndex").toInt(), 1);
    }

    void aOpensTheDetail()
    {
        load(entries(3));
        open(3);
        tap(u"down"_s);
        QTest::qWait(50);
        tap(u"a"_s);
        QTRY_COMPARE_WITH_TIMEOUT(depth(), 2, 5000);
        QCOMPARE(store->current().value(u"id"_s).toString(), u"org.example.App1"_s);
        QCOMPARE(rowKeys(), QStringList({u"action"_s, u"developer"_s, u"summary"_s}));
        QCOMPARE(rowTitle(u"action"_s), u"install"_s);
    }

    void bGoesBackFromTheDetail()
    {
        load(entries(3));
        open(3);
        openDetail(u"org.example.App0"_s);
        tap(u"b"_s);
        QTRY_COMPARE_WITH_TIMEOUT(depth(), 1, 5000);
        QVERIFY(store->current().isEmpty());
        QCOMPARE(store->shown(), true);
    }

    void bOnTheListQuits()
    {
        load(entries(3));
        open(3);
        QSignalSpy quit(store, &StoreProcess::quitRequested);
        tap(u"b"_s);
        QTRY_COMPARE(quit.count(), 1);
    }

    void installRunsProgressAndShowsOpen()
    {
        load(entries(1));
        open(1);
        openDetail(u"org.example.App0"_s);
        fake->setInstallSteps(8);
        fake->setInstallDelayMs(40);
        QSignalSpy toasts(store, &StoreProcess::toast);
        tap(u"a"_s);
        QTRY_VERIFY_WITH_TIMEOUT(store->busy(), 5000);
        QCOMPARE(rowTitle(u"action"_s), u"cancel"_s);
        QTRY_VERIFY_WITH_TIMEOUT(store->percent() > 0, 5000);
        QQuickItem *ring = find(u"store-progress"_s);
        QVERIFY(ring != nullptr);
        QVERIFY(ring->property("visible").toBool());
        QVERIFY(ring->property("arcSweep").toReal() > 0);
        QTRY_COMPARE_WITH_TIMEOUT(store->installed(), true, 10000);
        QVERIFY(!store->busy());
        QCOMPARE(rowTitle(u"action"_s), u"open"_s);
        QVERIFY(toasts.count() >= 1);
        QCOMPARE(toasts.last().at(1).toString(), u"installed"_s);
    }

    void cancelStopsTheInstall()
    {
        load(entries(1));
        open(1);
        openDetail(u"org.example.App0"_s);
        fake->setInstallSteps(20);
        fake->setInstallDelayMs(40);
        QSignalSpy toasts(store, &StoreProcess::toast);
        tap(u"a"_s);
        QTRY_VERIFY_WITH_TIMEOUT(store->busy(), 5000);
        tap(u"a"_s);
        QTRY_VERIFY_WITH_TIMEOUT(!store->busy(), 10000);
        QCOMPARE(rowTitle(u"action"_s), u"install"_s);
        QTRY_VERIFY_WITH_TIMEOUT(toasts.count() >= 1, 5000);
        QCOMPARE(toasts.last().at(1).toString(), u"cancelled"_s);
    }

    void secondInstallShowsQueued()
    {
        load(entries(2));
        open(2);
        openDetail(u"org.example.App0"_s);
        fake->setInstallSteps(20);
        fake->setInstallDelayMs(40);
        tap(u"a"_s);
        QTRY_VERIFY_WITH_TIMEOUT(store->busy(), 5000);
        tap(u"b"_s);
        QTRY_COMPARE_WITH_TIMEOUT(depth(), 1, 5000);
        openDetail(u"org.example.App1"_s);
        tap(u"a"_s);
        QTRY_VERIFY_WITH_TIMEOUT(store->queued(), 5000);
        QVERIFY(rowKeys().contains(u"progress"_s));
        QCOMPARE(rowTitle(u"progress"_s), u"queued"_s);
        QCOMPARE(rowValue(u"progress"_s), QString());
        tap(u"b"_s);
        QTRY_COMPARE_WITH_TIMEOUT(depth(), 1, 5000);
        openDetail(u"org.example.App0"_s);
        QTRY_VERIFY_WITH_TIMEOUT(store->busy(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!store->queued(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(store->percent() >= 0, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(store->installed(), true, 20000);
    }

    void eachAppKeepsItsOwnJob()
    {
        load(entries(2));
        open(2);
        openDetail(u"org.example.App0"_s);
        fake->setInstallSteps(20);
        fake->setInstallDelayMs(40);
        tap(u"a"_s);
        QTRY_VERIFY_WITH_TIMEOUT(store->busy(), 5000);
        tap(u"b"_s);
        QTRY_COMPARE_WITH_TIMEOUT(depth(), 1, 5000);
        openDetail(u"org.example.App1"_s);
        tap(u"a"_s);
        QTRY_VERIFY_WITH_TIMEOUT(store->queued(), 5000);
        tap(u"a"_s);
        QTRY_VERIFY_WITH_TIMEOUT(!store->busy(), 5000);
        QCOMPARE(rowTitle(u"action"_s), u"install"_s);
        tap(u"b"_s);
        QTRY_COMPARE_WITH_TIMEOUT(depth(), 1, 5000);
        openDetail(u"org.example.App0"_s);
        QTRY_VERIFY_WITH_TIMEOUT(store->busy(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(store->percent() >= 0, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(store->installed(), true, 20000);
    }

    void uninstallJobReadsAsUninstalling()
    {
        load(entries(1), {entryFor(u"org.example.App0"_s, u"app 0"_s)});
        open(1);
        openDetail(u"org.example.App0"_s);
        fake->setInstallDelayMs(400);
        tap(u"x"_s);
        QTest::qWait(50);
        tap(u"up"_s);
        QTest::qWait(50);
        tap(u"a"_s);
        QTRY_VERIFY_WITH_TIMEOUT(store->busy(), 5000);
        QCOMPARE(store->kind(), u"uninstall"_s);
        QTRY_VERIFY_WITH_TIMEOUT(!store->queued(), 5000);
        QCOMPARE(rowTitle(u"progress"_s), u"uninstalling"_s);
        QTRY_COMPARE_WITH_TIMEOUT(store->installed(), false, 5000);
    }

    void uninstallAsksFirst()
    {
        load(entries(1), {entryFor(u"org.example.App0"_s, u"app 0"_s)});
        open(1);
        openDetail(u"org.example.App0"_s);
        QCOMPARE(rowTitle(u"action"_s), u"open"_s);
        tap(u"x"_s);
        QTest::qWait(50);
        QQuickItem *confirm = find(u"confirm"_s);
        QVERIFY(confirm != nullptr);
        QVERIFY(confirm->property("shown").toBool());
        QCOMPARE(confirm->property("items").toStringList(),
                 QStringList({u"uninstall app 0"_s, u"keep app 0"_s}));
        QCOMPARE(confirm->property("title").toString(), u"uninstall app 0?"_s);
        QCOMPARE(confirm->property("focusIndex").toInt(), 1);
        QCOMPARE(depth(), 2);
        tap(u"b"_s);
        QTest::qWait(100);
        QVERIFY(!confirm->property("shown").toBool());
        QCOMPARE(fake->uninstalled(), QStringList());
        tap(u"x"_s);
        QTest::qWait(50);
        tap(u"up"_s);
        QTest::qWait(50);
        tap(u"a"_s);
        QTRY_COMPARE_WITH_TIMEOUT(fake->uninstalled(), QStringList{u"org.example.App0"_s}, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(store->installed(), false, 5000);
    }

    void installFailureShowsTheToast()
    {
        load(entries(1));
        open(1);
        openDetail(u"org.example.App0"_s);
        fake->setInstallFails(u"org.example.App0"_s, u"no space"_s);
        QSignalSpy toasts(store, &StoreProcess::toast);
        tap(u"a"_s);
        QTRY_VERIFY_WITH_TIMEOUT(toasts.count() >= 1, 5000);
        QCOMPARE(toasts.last().at(0).toString(), u"app 0"_s);
        QCOMPARE(toasts.last().at(1).toString(), u"no space"_s);
        QQuickItem *toast = find(u"toast"_s);
        QVERIFY(toast != nullptr);
        QTRY_VERIFY_WITH_TIMEOUT(toast->property("shown").toBool(), 5000);
        QCOMPARE(toast->property("title").toString(), u"no space"_s);
    }

    void openLaunchesTheDesktopId()
    {
        load(entries(1), {entryFor(u"org.example.App0"_s, u"app 0"_s)});
        open(1);
        openDetail(u"org.example.App0"_s);
        tap(u"a"_s);
        QTRY_COMPARE_WITH_TIMEOUT(store->starts, QStringList{u"org.example.App0.desktop"_s}, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!store->shown(), 5000);
        QCOMPARE(store->rows().size(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(depth(), 0, 5000);
    }

    void openFailureKeepsTheWindow()
    {
        load(entries(1), {entryFor(u"org.example.App0"_s, u"app 0"_s)});
        open(1);
        openDetail(u"org.example.App0"_s);
        store->failLaunch = true;
        QSignalSpy toasts(store, &StoreProcess::toast);
        tap(u"a"_s);
        QTRY_VERIFY_WITH_TIMEOUT(toasts.count() >= 1, 5000);
        QCOMPARE(toasts.last().at(1).toString(), u"open failed"_s);
        QCOMPARE(store->shown(), true);
        QTRY_COMPARE_WITH_TIMEOUT(depth(), 1, 5000);
    }

    void searchOpensTheKeyboard()
    {
        load(entries(3));
        open(3);
        tap(u"y"_s);
        QTest::qWait(50);
        QQuickItem *keyboard = find(u"keyboard"_s);
        QVERIFY(keyboard != nullptr);
        QVERIFY(keyboard->property("shown").toBool());
        QObject *entry = keyboard->property("entry").value<QObject *>();
        QVERIFY(entry != nullptr);
        const QString typed = u"app 1"_s;
        for (const QChar ch : typed)
            QVERIFY(QMetaObject::invokeMethod(entry, "type", Q_ARG(QString, QString(ch))));
        QVERIFY(QMetaObject::invokeMethod(keyboard, "finish"));
        QTRY_COMPARE_WITH_TIMEOUT(store->search(), u"app 1"_s, 5000);
        QCOMPARE(store->count(), 1);
        tap(u"back"_s);
        QTRY_COMPARE_WITH_TIMEOUT(store->search(), QString(), 5000);
        QCOMPARE(store->count(), 3);
    }

    void keyboardTakesThePressesFirst()
    {
        load(entries(5));
        open(5);
        QQuickItem *list = find(u"storelist-rows"_s);
        const int focus = list->property("focusIndex").toInt();
        tap(u"y"_s);
        QTest::qWait(50);
        QQuickItem *keyboard = find(u"keyboard"_s);
        QVERIFY(keyboard->property("shown").toBool());
        tap(u"down"_s);
        QTest::qWait(50);
        QCOMPARE(list->property("focusIndex").toInt(), focus);
        QCOMPARE(keyboard->property("focusCode").toString(), u"4"_s);
        tap(u"b"_s);
        QTest::qWait(200);
        QVERIFY(!keyboard->property("shown").toBool());
    }

    void detailKeepsTheListFocus()
    {
        load(entries(6));
        open(6);
        QQuickItem *list = find(u"storelist-rows"_s);
        tap(u"down"_s);
        tap(u"down"_s);
        QTest::qWait(50);
        QCOMPARE(list->property("focusIndex").toInt(), 2);
        openDetail(u"org.example.App2"_s);
        tap(u"b"_s);
        QTRY_COMPARE_WITH_TIMEOUT(depth(), 1, 5000);
        QCOMPARE(find(u"storelist-rows"_s)->property("focusIndex").toInt(), 2);
    }

    void longAndManyRowsScroll()
    {
        QList<StoreEntry> rows;
        for (int i = 0; i < 40; ++i)
            rows << entryFor(u"org.example.App%1"_s.arg(i),
                             u"An Extremely Long Application Name That Runs Well Past The Row %1"_s.arg(i));
        load(rows);
        open(40);
        QQuickItem *list = find(u"storelist-rows"_s);
        QVERIFY(list != nullptr);
        QVERIFY(QMetaObject::invokeMethod(list, "setFocus", Q_ARG(int, 39), Q_ARG(int, 0)));
        QTest::qWait(50);
        QCOMPARE(list->property("focusIndex").toInt(), 39);
        QVERIFY(list->property("contentY").toReal() > 0);
    }

    void padTurnTurnsTheButtons()
    {
        load(entries(3));
        open(3);
        QQuickItem *router = find(u"router"_s);
        QVERIFY(router != nullptr);
        store->setPadTurn(1);
        QTest::qWait(20);
        QCOMPARE(router->property("turn").toInt(), 1);
        QString mapped;
        QVERIFY(QMetaObject::invokeMethod(router, "padName", Q_RETURN_ARG(QString, mapped), Q_ARG(QString, u"right"_s)));
        QCOMPARE(mapped, u"down"_s);
        store->setPadTurn(0);
        QVERIFY(QMetaObject::invokeMethod(router, "padName", Q_RETURN_ARG(QString, mapped), Q_ARG(QString, u"right"_s)));
        QCOMPARE(mapped, u"right"_s);
    }

    void pulseOncePerSecond()
    {
        load(entries(1));
        open(1);
        QCOMPARE(idle->pulseCount(), 0);
        store->pulse();
        store->pulse();
        QCOMPARE(idle->pulseCount(), 1);
        QTest::qWait(1100);
        store->pulse();
        QCOMPARE(idle->pulseCount(), 2);
    }

    void detailShowsDeveloperAndSize()
    {
        StoreEntry entry = entryFor(u"org.example.App0"_s, u"app 0"_s, u"a summary line"_s);
        entry.installedSize = 34800000;
        load({entry}, {entry});
        open(1);
        openDetail(u"org.example.App0"_s);
        QCOMPARE(rowValue(u"developer"_s), u"Example Developer"_s);
        QCOMPARE(rowValue(u"summary"_s), u"a summary line"_s);
        QCOMPARE(rowValue(u"size"_s), u"35 MB"_s);
    }
};

QTEST_MAIN(StoreLayoutTest)

#include "tst_storelayout.moc"
