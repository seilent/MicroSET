#include "activator.h"
#include "appmodel.h"
#include "glibthread.h"
#include "launch.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QDBusVirtualObject>
#include <QDir>
#include <QFile>
#include <QCoreApplication>
#include <QDBusConnectionInterface>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <glib.h>

#include <functional>

#pragma push_macro("signals")
#undef signals
#include <gio/gdesktopappinfo.h>
#include <gio/gio.h>
#pragma pop_macro("signals")
#include <memory>

using namespace Qt::StringLiterals;

class FakeManagerEnv : public QDBusVirtualObject
{
public:
    static inline const QString Service = QStringLiteral("org.freedesktop.systemd1");
    static inline const QString Path = QStringLiteral("/org/freedesktop/systemd1");
    static inline const QString Manager = QStringLiteral("org.freedesktop.systemd1.Manager");

    explicit FakeManagerEnv(const QString &address)
        : m_address(address)
    {
        QDBusConnection bus = connection();
        m_ok = bus.registerVirtualObject(Path, this) && bus.registerService(Service);
    }

    ~FakeManagerEnv() override
    {
        QDBusConnection bus = connection();
        bus.unregisterObject(Path);
        bus.unregisterService(Service);
        QDBusConnection::disconnectFromBus(u"fake-manager"_s);
    }

    QDBusConnection connection() { return QDBusConnection::connectToBus(m_address, u"fake-manager"_s); }
    bool ok() const { return m_ok; }
    QString introspect(const QString &) const override { return {}; }

    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override
    {
        if (message.member() == u"Subscribe"_s) {
            connection.send(message.createReply());
            return true;
        }
        if (message.member() == u"Get"_s) {
            connection.send(message.createReply(QVariant::fromValue(QDBusVariant(environment))));
            return true;
        }
        if (message.member() != u"StartTransientUnit"_s)
            return false;
        signatures << message.signature();
        if (message.signature() != u"ssa(sv)a(sa(sv))"_s) {
            rejects << message.signature();
            connection.send(message.createErrorReply(
                u"org.freedesktop.DBus.Error.InvalidArgs"_s,
                u"Invalid arguments '%1' to call org.freedesktop.systemd1.Manager.StartTransientUnit(), expecting "
                u"'ssa(sv)a(sa(sv))'"_s.arg(message.signature())));
            return true;
        }
        units << message.arguments().value(0).toString();
        modes << message.arguments().value(1).toString();
        QVariantMap rows;
        for (const UnitProperty &property : qdbus_cast<QList<UnitProperty>>(message.arguments().value(2)))
            rows.insert(property.name, property.value.variant());
        properties << rows;
        QDBusArgument aux = message.arguments().value(3).value<QDBusArgument>();
        aux.beginArray();
        while (!aux.atEnd()) {
            QString name;
            QList<UnitProperty> values;
            aux.beginStructure();
            aux >> name >> values;
            aux.endStructure();
            auxNames << name;
        }
        aux.endArray();
        connection.send(
            message.createReply(QVariant::fromValue(QDBusObjectPath(u"/org/freedesktop/systemd1/job/1"_s))));
        return true;
    }

    void sendReloading()
    {
        QDBusMessage signal = QDBusMessage::createSignal(Path, Manager, u"Reloading"_s);
        signal << true;
        connection().send(signal);
    }

    void sendPropertiesChanged()
    {
        QDBusMessage signal =
            QDBusMessage::createSignal(Path, u"org.freedesktop.DBus.Properties"_s, u"PropertiesChanged"_s);
        signal << Manager << QVariantMap() << QStringList();
        connection().send(signal);
    }

    QString m_address;
    bool m_ok = false;
    QStringList environment;
    QStringList units;
    QStringList modes;
    QStringList signatures;
    QStringList rejects;
    QStringList auxNames;
    QList<QVariantMap> properties;
};

class AppsTest : public QObject
{
    Q_OBJECT

    QTemporaryDir m_data;
    QTemporaryDir m_user;
    QString m_address;
    QProcess m_bus;
    std::unique_ptr<FakeManagerEnv> m_manager;

    void writeEntry(const QString &root, const QString &id, const QString &body) const
    {
        QDir().mkpath(root);
        QFile file(QDir(root).filePath(id));
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(body.toUtf8());
        file.close();
    }

    void entry(const QString &id, const QString &name, const QString &extra) const
    {
        writeEntry(QDir(m_data.path()).filePath(u"applications"_s), id,
                   u"[Desktop Entry]\nType=Application\nName=%1\nExec=%2\n%3\n"_s.arg(name, u"true"_s, extra));
    }

    QStringList names() const
    {
        QStringList out;
        for (const QVariant &row : m_apps->entries())
            out << row.toMap().value(u"name"_s).toString();
        return out;
    }

    QStringList ids() const
    {
        QStringList out;
        for (const QVariant &row : m_apps->entries())
            out << row.toMap().value(u"id"_s).toString();
        return out;
    }

    QString flatpakOf(const QString &id) const
    {
        for (const QVariant &row : m_apps->entries()) {
            const QVariantMap map = row.toMap();
            if (map.value(u"id"_s).toString() == id)
                return map.value(u"flatpak"_s).toString();
        }
        return u"missing"_s;
    }

    AppModel *m_apps = nullptr;
    GlibThread *m_glib = nullptr;

private slots:
    void initTestCase()
    {
        const QString path = QDir(m_user.path()).filePath(u"bus"_s);
        m_bus.setStandardOutputFile(QProcess::nullDevice());
        m_bus.setStandardErrorFile(QProcess::nullDevice());
        m_bus.start(u"dbus-daemon"_s,
                    {u"--session"_s, u"--nofork"_s, u"--nopidfile"_s, u"--address=unix:path="_s + path});
        QVERIFY(m_bus.waitForStarted());
        QElapsedTimer ready;
        ready.start();
        while (!QFile::exists(path) && ready.elapsed() < 10000)
            QTest::qWait(20);
        QVERIFY(QFile::exists(path));
        m_address = u"unix:path="_s + path;
        qputenv("DBUS_SESSION_BUS_ADDRESS", m_address.toUtf8());
        qputenv("XDG_DATA_HOME", m_user.path().toUtf8());
        qputenv("XDG_DATA_DIRS", m_data.path().toUtf8());
        qputenv("XDG_CURRENT_DESKTOP", "Hyprland");
        qputenv("XDG_CONFIG_HOME", m_user.path().toUtf8());
        QDir().mkpath(QDir(m_data.path()).filePath(u"applications"_s));
    }

    void cleanupTestCase()
    {
        qunsetenv("XDG_DATA_HOME");
        qunsetenv("XDG_DATA_DIRS");
        qunsetenv("XDG_CURRENT_DESKTOP");
        qunsetenv("XDG_CONFIG_HOME");
        qunsetenv("DBUS_SESSION_BUS_ADDRESS");
        m_bus.terminate();
        if (!m_bus.waitForFinished(3000)) {
            m_bus.kill();
            m_bus.waitForFinished();
        }
    }

    void directorySettles()
    {
        const QString dir = QDir(m_data.path()).filePath(u"applications"_s);
        QVERIFY(QFileInfo::exists(dir));
        QTest::qWait(200);
    }

    void waitForFreeManager()
    {
        QDBusConnection bus = QDBusConnection::connectToBus(m_address, u"launcher-probe"_s);
        QElapsedTimer timer;
        timer.start();
        while (bus.interface()->isServiceRegistered(FakeManagerEnv::Service).value() && timer.elapsed() < 5000)
            QTest::qWait(20);
        QVERIFY(!bus.interface()->isServiceRegistered(FakeManagerEnv::Service).value());
        QDBusConnection::disconnectFromBus(u"launcher-probe"_s);
    }

    void init()
    {
        if (m_manager != nullptr) {
            m_manager.reset();
            QTest::qWait(200);
        }
        waitForFreeManager();
        QDir dir(QDir(m_data.path()).filePath(u"applications"_s));
        for (const QString &name : dir.entryList(QDir::Files))
            dir.remove(name);
        QDir user(QDir(m_user.path()).filePath(u"applications"_s));
        for (const QString &name : user.entryList(QDir::Files))
            user.remove(name);

        entry(u"foot.desktop"_s, u"Foot"_s, u"Icon=foot"_s);
        writeEntry(QDir(m_data.path()).filePath(u"applications"_s), u"runner.desktop"_s,
                   u"[Desktop Entry]\nType=Application\nName=Runner\nExec=/usr/bin/env\nIcon=runner\n"_s);
        entry(u"hidden.desktop"_s, u"Hidden App"_s, u"NoDisplay=true"_s);
        entry(u"gone.desktop"_s, u"Gone App"_s, u"Hidden=true"_s);
        entry(u"other.desktop"_s, u"Other Only"_s, u"OnlyShowIn=GNOME;"_s);
        entry(u"notours.desktop"_s, u"Not Ours"_s, u"NotShowIn=Hyprland;"_s);
        entry(u"missing.desktop"_s, u"Missing Tool"_s, u"TryExec=no-such-tool-anywhere"_s);
        entry(u"zed.desktop"_s, u"Zed"_s, u"Icon=/usr/share/pixmaps/zed.png"_s);
        entry(u"plain.desktop"_s, u"Plain"_s, u"Categories=Utility;"_s);
        entry(u"org.example.Flat.desktop"_s, u"Flat"_s, u"X-Flatpak=org.example.Flat"_s);
        entry(u"steam_http_loader.desktop"_s, u"Steam HTTP Handler"_s, u"Exec=steam-http-loader %u"_s);
        writeEntry(QDir(m_data.path()).filePath(u"applications"_s), u"org.example.App.desktop"_s,
                   u"[Desktop Entry]\nType=Application\nName=App\nExec=true\nDBusActivatable=true\n"_s);
        writeEntry(QDir(m_data.path()).filePath(u"applications"_s), u"org.example.Inherit.desktop"_s,
                   u"[Desktop Entry]\nType=Application\nName=Inherit\nExec=sh -c 'env > %1'\n"_s.arg(
                       QDir(m_data.path()).filePath(u"inherit.env"_s)));
        writeEntry(QDir(m_data.path()).filePath(u"applications"_s), u"game.desktop"_s,
                   u"[Desktop Entry]\nType=Application\nName=Steam Game\nExec=steam steam://rungameid/993090\n"
                   u"Icon=steam_icon_993090\n"_s);
        writeEntry(QDir(m_user.path()).filePath(u"applications"_s), u"foot.desktop"_s,
                   u"[Desktop Entry]\nType=Application\nName=Foot Local\nExec=true\n"_s);

        m_glib = new GlibThread(this);
        m_glib->startAndWait();
        m_apps = new AppModel(this);
        AppIcons::attach(m_apps);
        m_apps->refresh();
        m_apps->startMonitor(m_glib);
    }

    void cleanup()
    {
        AppIcons::attach(nullptr);
        delete m_apps;
        m_apps = nullptr;
        m_glib->stop();
        delete m_glib;
        m_glib = nullptr;
    }

    void noIconGivesNoSource()
    {
        QVERIFY(AppModel::iconSource(u"plain.desktop"_s).isEmpty());
        QCOMPARE(AppModel::iconSource(u"runner.desktop"_s), u"image://microset-appicon/runner.desktop"_s);
        QVERIFY(AppModel::iconSource(u"absent.desktop"_s).isEmpty());
    }

    void filtersAndSorts()
    {
        const QStringList found = ids();
        QCOMPARE(found, QStringList({u"org.example.App.desktop"_s, u"org.example.Flat.desktop"_s, u"foot.desktop"_s,
                                     u"org.example.Inherit.desktop"_s, u"plain.desktop"_s, u"runner.desktop"_s,
                                     u"zed.desktop"_s}));
        QCOMPARE(names(), QStringList({u"App"_s, u"Flat"_s, u"Foot Local"_s, u"Inherit"_s, u"Plain"_s, u"Runner"_s,
                                       u"Zed"_s}));
    }

    void duplicateIdPrefersTheUserDir()
    {
        QCOMPARE(m_apps->nameOf(u"foot.desktop"_s), u"Foot Local"_s);
    }

    void absoluteIconIsAFile()
    {
        AppIcons::attach(m_apps);
        const AppEntry *zed = m_apps->find(u"zed.desktop"_s);
        QVERIFY(zed != nullptr);
        QCOMPARE(zed->iconFile, u"/usr/share/pixmaps/zed.png"_s);
        QVERIFY(!AppModel::iconSource(u"zed.desktop"_s).isEmpty());
    }

    void flatpakEntryKeepsItsAppId()
    {
        QCOMPARE(flatpakOf(u"org.example.Flat.desktop"_s), u"org.example.Flat"_s);
        QCOMPARE(flatpakOf(u"foot.desktop"_s), QString());
    }

    void liveAdd()
    {
        directorySettles();
        m_apps->stopMonitor();
        m_apps->startMonitor(m_glib);
        QTest::qWait(200);
        QSignalSpy spy(m_apps, &AppModel::entriesChanged);
        const int before = spy.count();
        entry(u"added.desktop"_s, u"Added"_s, u"Icon=added"_s);
        QTRY_VERIFY_WITH_TIMEOUT(ids().contains(u"added.desktop"_s), 15000);
        QVERIFY(spy.count() > before);
    }

    void childEnvironment()
    {
        const QString out = QDir(m_data.path()).filePath(u"child.env"_s);
        QFile::remove(out);
        writeEntry(QDir(m_data.path()).filePath(u"applications"_s), u"env.desktop"_s,
                   u"[Desktop Entry]\nType=Application\nName=Env\nExec=sh -c 'env > %1'\nIcon=env\n"_s.arg(out));
        qputenv("QT_QPA_PLATFORM", "offscreen");
        qputenv("QT_QUICK_BACKEND", "software");
        qputenv("QSG_RHI_BACKEND", "software");
        qputenv("LIBGL_ALWAYS_SOFTWARE", "1");
        m_manager = std::make_unique<FakeManagerEnv>(m_address);
        QVERIFY(m_manager->ok());
        m_manager->environment = {u"HOME=/var/home/micro"_s, u"PATH=/usr/bin:/bin"_s,
                                  u"XDG_RUNTIME_DIR=/run/user/1000"_s,
                                  u"DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus"_s,
                                  u"WAYLAND_DISPLAY=wayland-1"_s};
        QDBusConnection bus = QDBusConnection::connectToBus(m_address, u"launcher-env"_s);
        AppModel model;
        model.refresh();
        LauncherLaunch context(bus, &model, this);
        ManagerEnv env(bus);
        QSignalSpy done(&env, &ManagerEnv::readDone);
        env.read();
        QTRY_VERIFY_WITH_TIMEOUT(!done.isEmpty(), 5000);
        context.setEnvironment(env.environment());
        QSignalSpy failed(&context, &Activator::failed);
        context.start(u"env.desktop"_s);
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(out), 15000);
        QVERIFY2(failed.isEmpty(), failed.isEmpty() ? "" : qPrintable(failed.at(0).at(1).toString()));
        QTest::qWait(200);
        QFile file(out);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QStringList lines = QString::fromUtf8(file.readAll()).split(u'\n', Qt::SkipEmptyParts);
        file.close();
        QFile::remove(out);
        QVERIFY(!lines.isEmpty());
        QMap<QString, QString> values;
        for (const QString &line : lines)
            values.insert(line.left(line.indexOf(u'=')), line.mid(line.indexOf(u'=') + 1));
        for (const auto &expected : m_manager->environment) {
            const qsizetype at = expected.indexOf(u'=');
            const QString name = expected.left(at);
            QCOMPARE(values.value(name), expected.mid(at + 1));
        }
        const QStringList names = values.keys();
        QVERIFY(names.contains(u"WAYLAND_DISPLAY"_s));
        QVERIFY(!names.contains(u"QT_QPA_PLATFORM"_s));
        QVERIFY(!names.contains(u"QT_QUICK_BACKEND"_s));
        QVERIFY(!names.contains(u"QSG_RHI_BACKEND"_s));
        QVERIFY(!names.contains(u"LIBGL_ALWAYS_SOFTWARE"_s));
        qunsetenv("QT_QPA_PLATFORM");
        qunsetenv("QT_QUICK_BACKEND");
        qunsetenv("QSG_RHI_BACKEND");
        qunsetenv("LIBGL_ALWAYS_SOFTWARE");
        QDBusConnection::disconnectFromBus(u"launcher-env"_s);
    }

    void emptyManagerEnvInheritsTheProcess()
    {
        const QString out = QDir(m_data.path()).filePath(u"inherit.env"_s);
        QFile::remove(out);
        LauncherLaunch context(QDBusConnection(QString()), m_apps, this);
        QVERIFY(context.environment().isEmpty());
        qputenv("MICROSET_LAUNCH_MARK", "kept");
        context.start(u"org.example.Inherit.desktop"_s);
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(out), 15000);
        QTest::qWait(200);
        QFile file(out);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QMap<QString, QString> values = ManagerEnv::parse(
            QString::fromUtf8(file.readAll()).split(u'\n', Qt::SkipEmptyParts));
        file.close();
        QFile::remove(out);
        qunsetenv("MICROSET_LAUNCH_MARK");
        QVERIFY(values.contains(u"HOME"_s));
        QVERIFY(values.contains(u"PATH"_s));
        QCOMPARE(values.value(u"MICROSET_LAUNCH_MARK"_s), u"kept"_s);
    }

    void managerEnvironment()
    {
        m_manager = std::make_unique<FakeManagerEnv>(m_address);
        QVERIFY(m_manager->ok());
        m_manager->environment = {u"HOME=/var/home/micro"_s, u"PATH=/usr/local/bin:/usr/bin"_s,
                                  u"XDG_RUNTIME_DIR=/run/user/1000"_s,
                                  u"DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus"_s};
        ManagerEnv env(QDBusConnection::connectToBus(m_address, u"launcher-test"_s));
        QSignalSpy done(&env, &ManagerEnv::readDone);
        env.read();
        QTRY_VERIFY_WITH_TIMEOUT(!done.isEmpty(), 5000);
        const QMap<QString, QString> values = env.environment();
        QCOMPARE(values.value(u"HOME"_s), u"/var/home/micro"_s);
        QCOMPARE(values.value(u"PATH"_s), u"/usr/local/bin:/usr/bin"_s);
        for (const QString &name : LauncherLaunch::requiredNames())
            QVERIFY2(values.contains(name), qPrintable(name));
        QDBusConnection::disconnectFromBus(u"launcher-test"_s);
    }

    void reloadingReadsTheEnvironmentAgain()
    {
        m_manager = std::make_unique<FakeManagerEnv>(m_address);
        QVERIFY(m_manager->ok());
        m_manager->environment = {u"HOME=/var/home/old"_s, u"PATH=/usr/bin:/bin"_s,
                                  u"XDG_RUNTIME_DIR=/run/user/1000"_s,
                                  u"DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus"_s};
        QDBusConnection bus = QDBusConnection::connectToBus(m_address, u"launcher-reload"_s);
        ManagerEnv env(bus);
        QSignalSpy done(&env, &ManagerEnv::readDone);
        env.watch();
        QTRY_VERIFY_WITH_TIMEOUT(!done.isEmpty(), 5000);
        QCOMPARE(env.environment().value(u"HOME"_s), u"/var/home/old"_s);
        QVERIFY(!env.stale());
        m_manager->environment = {u"HOME=/var/home/new"_s, u"PATH=/usr/bin:/bin"_s,
                                  u"XDG_RUNTIME_DIR=/run/user/1000"_s,
                                  u"DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus"_s};
        m_manager->sendReloading();
        QTRY_VERIFY_WITH_TIMEOUT(env.environment().value(u"HOME"_s) == u"/var/home/new"_s, 5000);
        QVERIFY(!env.stale());
        QDBusConnection::disconnectFromBus(u"launcher-reload"_s);
    }

    void propertiesChangedReadsTheEnvironmentAgain()
    {
        m_manager = std::make_unique<FakeManagerEnv>(m_address);
        QVERIFY(m_manager->ok());
        m_manager->environment = {u"HOME=/var/home/old"_s, u"PATH=/usr/bin:/bin"_s,
                                  u"XDG_RUNTIME_DIR=/run/user/1000"_s,
                                  u"DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus"_s};
        QDBusConnection bus = QDBusConnection::connectToBus(m_address, u"launcher-props"_s);
        ManagerEnv env(bus);
        QSignalSpy done(&env, &ManagerEnv::readDone);
        env.watch();
        QTRY_VERIFY_WITH_TIMEOUT(!done.isEmpty(), 5000);
        QCOMPARE(env.environment().value(u"HOME"_s), u"/var/home/old"_s);
        m_manager->environment = {u"HOME=/var/home/new"_s, u"PATH=/usr/bin:/bin"_s,
                                  u"XDG_RUNTIME_DIR=/run/user/1000"_s,
                                  u"DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus"_s};
        m_manager->sendPropertiesChanged();
        QTRY_VERIFY_WITH_TIMEOUT(env.environment().value(u"HOME"_s) == u"/var/home/new"_s, 5000);
        QDBusConnection::disconnectFromBus(u"launcher-props"_s);
    }

    void startRefreshesAStaleEnvironment()
    {
        m_manager = std::make_unique<FakeManagerEnv>(m_address);
        QVERIFY(m_manager->ok());
        m_manager->environment = {u"HOME=/var/home/old"_s, u"PATH=/usr/bin:/bin"_s,
                                  u"XDG_RUNTIME_DIR=/run/user/1000"_s,
                                  u"DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus"_s};
        QDBusConnection bus = QDBusConnection::connectToBus(m_address, u"launcher-stale"_s);
        ManagerEnv env(bus);
        QSignalSpy done(&env, &ManagerEnv::readDone);
        env.read();
        QTRY_VERIFY_WITH_TIMEOUT(!done.isEmpty(), 5000);
        QCOMPARE(env.environment().value(u"HOME"_s), u"/var/home/old"_s);
        m_manager->environment = {u"HOME=/var/home/new"_s, u"PATH=/usr/bin:/bin"_s,
                                  u"XDG_RUNTIME_DIR=/run/user/1000"_s,
                                  u"DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus"_s};
        AppModel model;
        LauncherLaunch context(bus, &model, this);
        context.setManager(&env);
        env.refresh();
        QVERIFY(env.stale());
        QSignalSpy failed(&context, &Activator::failed);
        context.start(u"absent.desktop"_s);
        QCOMPARE(context.environment().value(u"HOME"_s), u"/var/home/new"_s);
        QVERIFY(!env.stale());
        QVERIFY(!failed.isEmpty());
        QDBusConnection::disconnectFromBus(u"launcher-stale"_s);
    }

    void nonAsciiIdEscapesPerByte()
    {
        QCOMPARE(LauncherLaunch::escapeId(u"café.desktop"_s), u"caf\\xc3\\xa9.desktop"_s);
        writeEntry(QDir(m_data.path()).filePath(u"applications"_s), u"café.desktop"_s,
                   u"[Desktop Entry]\nType=Application\nName=Cafe\nExec=true\n"_s);
        m_manager = std::make_unique<FakeManagerEnv>(m_address);
        QVERIFY(m_manager->ok());
        m_manager->environment = {u"HOME=/var/home/micro"_s, u"PATH=/usr/bin:/bin"_s,
                                  u"XDG_RUNTIME_DIR=/run/user/1000"_s,
                                  u"DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus"_s};
        QDBusConnection bus = QDBusConnection::connectToBus(m_address, u"launcher-utf8"_s);
        AppModel model;
        model.refresh();
        QVERIFY(model.find(u"café.desktop"_s) != nullptr);
        LauncherLaunch context(bus, &model, this);
        ManagerEnv env(bus);
        QSignalSpy done(&env, &ManagerEnv::readDone);
        env.read();
        QTRY_VERIFY_WITH_TIMEOUT(!done.isEmpty(), 5000);
        context.setManager(&env);
        QSignalSpy launched(&context, &Activator::started);
        QSignalSpy failed(&context, &Activator::failed);
        context.start(u"café.desktop"_s);
        QTRY_VERIFY_WITH_TIMEOUT(!launched.isEmpty() || !failed.isEmpty(), 15000);
        QVERIFY2(failed.isEmpty(), failed.isEmpty() ? "" : qPrintable(failed.at(0).at(1).toString()));
        const qint64 pid = launched.at(0).at(1).toLongLong();
        QVERIFY(pid > 0);
        QTRY_VERIFY_WITH_TIMEOUT(!m_manager->units.isEmpty(), 5000);
        QCOMPARE(m_manager->units.last(), u"app-microset\\x2dlauncher-caf\\xc3\\xa9.desktop-%1.scope"_s.arg(pid));
        QDBusConnection::disconnectFromBus(u"launcher-utf8"_s);
    }

    void launchPutsTheChildInAScope()
    {
        m_manager = std::make_unique<FakeManagerEnv>(m_address);
        QVERIFY(m_manager->ok());
        m_manager->environment = {u"HOME=/var/home/micro"_s, u"PATH=/usr/bin:/bin"_s,
                                  u"XDG_RUNTIME_DIR=/run/user/1000"_s,
                                  u"DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus"_s};
        QDBusConnection bus = QDBusConnection::connectToBus(m_address, u"launcher-launch"_s);
        AppModel model;
        model.refresh();
        LauncherLaunch context(bus, &model, this);
        ManagerEnv env(bus);
        QSignalSpy done(&env, &ManagerEnv::readDone);
        env.read();
        QTRY_VERIFY_WITH_TIMEOUT(!done.isEmpty(), 5000);
        context.setEnvironment(env.environment());
        QSignalSpy launched(&context, &Activator::started);
        QSignalSpy failed(&context, &Activator::failed);
        context.start(u"runner.desktop"_s);
        QTRY_VERIFY_WITH_TIMEOUT(!launched.isEmpty() || !failed.isEmpty(), 15000);
        QVERIFY2(failed.isEmpty(), failed.isEmpty() ? "" : qPrintable(failed.at(0).at(1).toString()));
        const qint64 pid = launched.at(0).at(1).toLongLong();
        QVERIFY(pid > 0);
        QTRY_VERIFY_WITH_TIMEOUT(!m_manager->units.isEmpty(), 5000);
        QCOMPARE(m_manager->units.last(),
                 QString::fromLatin1("app-microset\\x2dlauncher-runner.desktop-%1.scope").arg(pid));
        QCOMPARE(m_manager->modes.last(), u"fail"_s);
        QCOMPARE(m_manager->signatures.last(), u"ssa(sv)a(sa(sv))"_s);
        QVERIFY(m_manager->rejects.isEmpty());
        QVERIFY(m_manager->auxNames.isEmpty());
        QVERIFY(!m_manager->properties.isEmpty());
        const QVariantMap props = m_manager->properties.last();
        QCOMPARE(props.value(u"Slice"_s).toString(), u"app.slice"_s);
        QCOMPARE(props.value(u"Description"_s).toString(), u"MicroSET launcher: runner.desktop"_s);
        const QList<uint> pids = qdbus_cast<QList<uint>>(props.value(u"PIDs"_s));
        QCOMPARE(pids.size(), 1);
        QCOMPARE(pids.constFirst(), uint(pid));
        QDBusConnection::disconnectFromBus(u"launcher-launch"_s);
    }

    void dbusActivatableHasNoScope()
    {
        m_apps->refresh();
        m_manager = std::make_unique<FakeManagerEnv>(m_address);
        QVERIFY(m_manager->ok());
        QDBusConnection bus = QDBusConnection::connectToBus(m_address, u"launcher-dbus"_s);
        LauncherLaunch context(bus, m_apps, this);
        context.setEnvironment({{u"HOME"_s, u"/var/home/micro"_s}});
        QSignalSpy launched(&context, &Activator::started);
        QVERIFY(m_apps->find(u"org.example.App.desktop"_s) != nullptr);
        context.start(u"org.example.App.desktop"_s);
        QTest::qWait(500);
        QVERIFY(launched.isEmpty());
        QVERIFY(m_manager->units.isEmpty());
        QDBusConnection::disconnectFromBus(u"launcher-dbus"_s);
    }
};

QTEST_GUILESS_MAIN(AppsTest)

#include "tst_launcherapps.moc"
