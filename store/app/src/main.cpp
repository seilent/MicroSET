#include "battery.h"
#include "clock.h"
#include "dbuspad.h"
#include "desktophome.h"
#include "env.h"
#include "flatpakbackend.h"
#include "homeclient.h"
#include "launcher.h"
#include "storeprocess.h"
#include "storeworker.h"
#include "waylandidle.h"
#include "wifi.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
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
Q_IMPORT_QML_PLUGIN(MicroSetStorePlugin)

using namespace Qt::StringLiterals;

namespace {

constexpr const char *OutputName = "DSI-1";

void fail(const QString &message)
{
    QTextStream(stderr) << "microset-store: " << message << '\n';
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
    QGuiApplication::setApplicationName(u"microset-store"_s);

    if (qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE")) {
        fail(u"HYPRLAND_INSTANCE_SIGNATURE is empty, not on the desktop"_s);
        return 1;
    }
    if (QGuiApplication::platformName() != u"wayland"_s) {
        fail(u"the store needs the wayland platform"_s);
        return 1;
    }

    QDBusConnection session = QDBusConnection::sessionBus();
    QDBusConnection system = QDBusConnection::systemBus();
    if (!session.isConnected()) {
        fail(u"no session bus"_s);
        return 1;
    }
    if (session.interface()->isServiceRegistered(StoreProcess::service())) {
        QDBusMessage call = QDBusMessage::createMethodCall(StoreProcess::service(), StoreProcess::path(),
                                                           StoreProcess::interface(), u"Show"_s);
        session.call(call, QDBus::Block, 2000);
        return 0;
    }

    DbusPad pad(session, system, nullptr, false);
    WaylandIdlePulse idle;

    StoreWorker worker(std::make_unique<FlatpakBackend>());
    worker.start();
    StoreProcess store(&worker, session);
    if (!store.publish(session))
        return 1;
    store.setPad(&pad);
    store.setIdle(&idle);
    QObject::connect(&pad, &DbusPad::pressed, &store, [&store](DbusPad::Action) { store.pulse(); });

    HomeClient home(session);
    const auto applyTurn = [&home, &store] {
        store.setPadTurn(Launcher::turnFor(home.transform(), Env().landscapeTurn()));
    };
    QObject::connect(&home, &DesktopHome::transformChanged, &store, applyTurn);
    QObject::connect(&home, &DesktopHome::availableChanged, &store, applyTurn);
    applyTurn();

    Clock clock;
    Battery battery(system);
    Wifi wifi(system);
    QObject::connect(&clock, &Clock::textChanged, &store, [&clock, &store] { store.setClock(clock.text()); });
    QObject::connect(&battery, &Battery::percentChanged, &store,
                     [&battery, &store] { store.setBattery(battery.present(), battery.percent()); });
    QObject::connect(&battery, &Battery::presentChanged, &store,
                     [&battery, &store] { store.setBattery(battery.present(), battery.percent()); });
    QObject::connect(&wifi, &Wifi::connectedChanged, &store,
                     [&wifi, &store] { store.setWifi(wifi.connected(), wifi.ssid()); });
    QObject::connect(&wifi, &Wifi::ssidChanged, &store,
                     [&wifi, &store] { store.setWifi(wifi.connected(), wifi.ssid()); });
    clock.start();

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::warnings, &engine, [](const QList<QQmlError> &list) {
        for (const QQmlError &error : list)
            fail(error.toString());
    });
    engine.setInitialProperties({
        {u"store"_s, QVariant::fromValue(static_cast<QObject *>(&store))},
        {u"pad"_s, QVariant::fromValue(static_cast<QObject *>(&pad))},
    });
    engine.loadFromModule("MicroSetStore", "Store");
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
    if (window == nullptr) {
        fail(u"MicroSetStore.Store did not produce a window"_s);
        return 1;
    }

    LayerShellQt::Window *layer = LayerShellQt::Window::get(window);
    if (layer == nullptr) {
        fail(u"no layer shell, the compositor has no zwlr_layer_shell_v1"_s);
        return 1;
    }
    layer->setLayer(LayerShellQt::Window::LayerTop);
    layer->setAnchors(LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorBottom
                                                    | LayerShellQt::Window::AnchorLeft
                                                    | LayerShellQt::Window::AnchorRight));
    layer->setExclusiveZone(-1);
    layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
    layer->setScope(u"microset-store"_s);
    layer->setActivateOnShow(false);
    layer->setCloseOnDismissed(false);
    if (QScreen *screen = panel())
        layer->setScreen(screen);
    layer->setDesiredSize(QSize(0, 0));

    store.setWindow(window);
    const auto attachSurface = [window, &idle] {
        auto *wayland = window->nativeInterface<QNativeInterface::Private::QWaylandWindow>();
        if (wayland != nullptr)
            idle.attach(wayland->surface());
    };
    attachSurface();
    QObject::connect(window, &QQuickWindow::sceneGraphInitialized, window, attachSurface);
    QObject::connect(window, &QWindow::visibleChanged, window, attachSurface);
    QObject::connect(&store, &StoreProcess::quitRequested, &app, &QCoreApplication::quit);
    store.show();

    return QCoreApplication::exec();
}
