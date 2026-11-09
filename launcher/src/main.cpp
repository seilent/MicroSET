#include "appmodel.h"
#include "battery.h"
#include "clock.h"
#include "dbuspad.h"
#include "desktophome.h"
#include "env.h"
#include "favoritesmodel.h"
#include "glibthread.h"
#include "launch.h"
#include "launcher.h"
#include "poweraction.h"
#include "sleepinhibitor.h"
#include "waylandidle.h"
#include "wifi.h"
#ifdef MICROSET_STORE_CORE
#include "flatpakbackend.h"
#include "storeworker.h"
#endif

#include <QDBusConnection>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QScreen>
#include <QTextStream>
#include <QtQml/qqmlextensionplugin.h>

#include <LayerShellQt/Window>

#include <qpa/qplatformwindow_p.h>

#include <memory>

Q_IMPORT_QML_PLUGIN(MicroSetPlugin)
Q_IMPORT_QML_PLUGIN(MicroSetLauncherPlugin)

using namespace Qt::StringLiterals;

namespace {

constexpr const char *OutputName = "DSI-1";

void fail(const QString &message)
{
    QTextStream(stderr) << "microset-launcher: " << message << '\n';
}

QScreen *panel()
{
    for (QScreen *screen : QGuiApplication::screens()) {
        if (screen->name() == QLatin1String(OutputName))
            return screen;
    }
    return QGuiApplication::primaryScreen();
}

}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(u"microset-launcher"_s);

    if (qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE")) {
        fail(u"HYPRLAND_INSTANCE_SIGNATURE is empty, not on the desktop"_s);
        return 1;
    }
    if (QGuiApplication::platformName() != u"wayland"_s) {
        fail(u"the launcher needs the wayland platform"_s);
        return 1;
    }

    QDBusConnection session = QDBusConnection::sessionBus();
    QDBusConnection system = QDBusConnection::systemBus();

    GlibThread glib;
    glib.startAndWait();

    AppModel apps;
    AppIcons::attach(&apps);
    ManagerEnv manager(session);
    manager.watch();
    LauncherLaunch launch(session, &apps);
    launch.setManager(&manager);
    LogindPower power(system);
    apps.startMonitor(&glib);
    apps.refresh();

    FavoritesModel favorites(system, &apps);

    HomeClient home(session);
    DbusPad pad(session, system, nullptr, false);
    SleepInhibitor sleep(system, u"the launcher home"_s);
    sleep.take();
    Launcher launcher(&home, &apps, &favorites, &launch, &power, &pad, &sleep, Env().landscapeTurn());
#ifdef MICROSET_STORE_CORE
    StoreWorker store(std::make_unique<FlatpakBackend>());
    store.start();
    launcher.setStore(&store);
#endif

    WaylandIdlePulse idle;
    launcher.setIdle(&idle);

    Clock clock;
    Battery battery(system);
    Wifi wifi(system);
    QObject::connect(&clock, &Clock::textChanged, &launcher, [&clock, &launcher] { launcher.setClock(clock.text()); });
    QObject::connect(&battery, &Battery::percentChanged, &launcher,
                     [&battery, &launcher] { launcher.setBattery(battery.present(), battery.percent()); });
    QObject::connect(&battery, &Battery::presentChanged, &launcher,
                     [&battery, &launcher] { launcher.setBattery(battery.present(), battery.percent()); });
    QObject::connect(&wifi, &Wifi::connectedChanged, &launcher,
                     [&wifi, &launcher] { launcher.setWifi(wifi.connected(), wifi.ssid()); });
    QObject::connect(&wifi, &Wifi::ssidChanged, &launcher,
                     [&wifi, &launcher] { launcher.setWifi(wifi.connected(), wifi.ssid()); });
    clock.start();

    if (!launcher.publish(session)) {
        fail(u"cannot own %1 on the session bus"_s.arg(Launcher::service()));
        return 1;
    }

    QQmlApplicationEngine engine;
    engine.addImageProvider(u"microset-appicon"_s, new AppIconProvider());
    QObject::connect(&engine, &QQmlApplicationEngine::warnings, &engine, [](const QList<QQmlError> &list) {
        for (const QQmlError &error : list)
            fail(error.toString());
    });
    engine.setInitialProperties({
        {u"launcher"_s, QVariant::fromValue(static_cast<QObject *>(&launcher))},
        {u"pad"_s, QVariant::fromValue(static_cast<QObject *>(&pad))},
    });
    engine.loadFromModule("MicroSetLauncher", "Home");
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
    if (window == nullptr) {
        fail(u"MicroSetLauncher.Home did not produce a window"_s);
        return 1;
    }

    LayerShellQt::Window *layer = LayerShellQt::Window::get(window);
    if (layer == nullptr) {
        fail(u"no layer shell, the compositor has no zwlr_layer_shell_v1"_s);
        return 1;
    }
    layer->setLayer(LayerShellQt::Window::LayerBackground);
    layer->setAnchors(LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorBottom
                                                    | LayerShellQt::Window::AnchorLeft
                                                    | LayerShellQt::Window::AnchorRight));
    layer->setExclusiveZone(-1);
    layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
    layer->setScope(u"microset-launcher"_s);
    layer->setActivateOnShow(false);
    layer->setCloseOnDismissed(false);
    if (QScreen *screen = panel())
        layer->setScreen(screen);
    layer->setDesiredSize(QSize(0, 0));

    window->show();
    const auto attachSurface = [window, &idle] {
        auto *wayland = window->nativeInterface<QNativeInterface::Private::QWaylandWindow>();
        if (wayland != nullptr)
            idle.attach(wayland->surface());
    };
    attachSurface();
    QObject::connect(window, &QQuickWindow::sceneGraphInitialized, window, attachSurface);
    QObject::connect(window, &QWindow::visibleChanged, window, attachSurface);

    QObject::connect(&app, &QCoreApplication::aboutToQuit, &apps, &AppModel::shutdown);
    const int code = QCoreApplication::exec();
    glib.stop();
    return code;
}
