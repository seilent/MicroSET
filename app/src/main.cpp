#include "auth.h"
#include "dbuspad.h"
#include "gamescope.h"
#include "holdhint.h"
#include "overlay.h"
#include "overlayservice.h"
#include "runninggame.h"
#include "settingsmodel.h"
#include "sleepinhibitor.h"
#include "steamlibrary.h"
#include "uirelease.h"

#include <QByteArray>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QSurfaceFormat>
#include <QTextStream>
#include <QTimer>
#include <QtQml/qqmlextensionplugin.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstddef>
#include <cstring>
#include <memory>

Q_IMPORT_QML_PLUGIN(MicroSetPlugin)
Q_IMPORT_QML_PLUGIN(MicroSetOverlayPlugin)

using namespace Qt::StringLiterals;

namespace {

constexpr int watchdogMs = 7000;

void fail(const QString &message)
{
    QTextStream(stderr) << "microset-settings: " << message << '\n';
}

bool notify(const QByteArray &state)
{
    const QByteArray path = qgetenv("NOTIFY_SOCKET");
    if (path.isEmpty())
        return false;
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (path.size() >= static_cast<qsizetype>(sizeof(address.sun_path)))
        return false;
    std::memcpy(address.sun_path, path.constData(), static_cast<size_t>(path.size()));
    socklen_t length = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + path.size() + 1);
    if (path.at(0) == '@') {
        address.sun_path[0] = '\0';
        length = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + path.size());
    }
    const int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return false;
    const ssize_t sent = sendto(fd, state.constData(), static_cast<size_t>(state.size()), MSG_NOSIGNAL,
                                reinterpret_cast<const sockaddr *>(&address), length);
    close(fd);
    return sent == state.size();
}

}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(u"microset-settings"_s);

    QCommandLineParser parser;
    parser.addHelpOption();
    const QCommandLineOption overlayOption(u"overlay"_s, u"Run as the gamescope settings overlay."_s);
    parser.addOption(overlayOption);
    parser.process(app);
    if (!parser.isSet(overlayOption)) {
        fail(u"only --overlay is supported"_s);
        return 2;
    }
    if (QGuiApplication::platformName() != u"xcb"_s) {
        fail(u"--overlay needs the xcb platform"_s);
        return 1;
    }
    microset::exitCleanlyOnDisplayLoss();

    QDBusConnection session = QDBusConnection::sessionBus();
    QDBusConnection system = QDBusConnection::systemBus();
    DbusPad pad(session, system, nullptr, false);
    SleepInhibitor sleep(system, u"hide the settings overlay"_s);
    sleep.take();
    Overlay overlay(&pad, &sleep);
    overlay.setHooks(
        [](QWindow *window) {
            QSurfaceFormat format = window->requestedFormat();
            format.setSwapInterval(0);
            window->setFormat(format);
            window->create();
            window->setWindowState(Qt::WindowFullScreen);
            if (!microset::markOverlay(window))
                fail(u"cannot set STEAM_OVERLAY"_s);
        },
        [](QWindow *window) {
            window->create();
            if (!microset::markOverlay(window))
                fail(u"cannot set STEAM_OVERLAY on the hint"_s);
        },
        [](QWindow *window) { window->lower(); });

    RunningGame game(session, u"/proc"_s);
    game.skip(SteamLibrary::excluded());
    std::unique_ptr<SettingsModel> model;
    std::unique_ptr<QQmlApplicationEngine> engine;
    const auto pushGame = [&model, &game] {
        if (!model)
            return;
        QVariantMap info = game.info();
        const int limit = microset::fpsLimit();
        if (!info.isEmpty() && limit >= 0)
            info.insert(u"fpsLimit"_s, limit);
        model->setGame(info);
    };
    QObject::connect(&overlay, &Overlay::shownChanged, &game, [&overlay, &game, pushGame] {
        if (!overlay.shown())
            return;
        game.refresh();
        pushGame();
    });
    overlay.setUi([&] {
        if (engine)
            return;
        model = std::make_unique<SettingsModel>(system);
        model->load();
        pushGame();
        QObject::connect(&game, &RunningGame::changed, model.get(), pushGame);
        engine = std::make_unique<QQmlApplicationEngine>();
        QObject::connect(engine.get(), &QQmlApplicationEngine::warnings, engine.get(),
                         [](const QList<QQmlError> &list) {
                             for (const QQmlError &e : list)
                                 fail(e.toString());
                         });
        engine->setInitialProperties({
            {u"overlay"_s, QVariant::fromValue(static_cast<QObject *>(&overlay))},
            {u"model"_s, QVariant::fromValue(static_cast<QObject *>(model.get()))},
            {u"pad"_s, QVariant::fromValue(static_cast<QObject *>(&pad))},
        });
        engine->loadFromModule("MicroSetOverlay", "Overlay");
        auto *window = qobject_cast<QQuickWindow *>(engine->rootObjects().value(0));
        if (!window) {
            fail(u"MicroSetOverlay.Overlay did not produce a window"_s);
            return;
        }
        overlay.setWindow(window);
        overlay.setToastWindow(qvariant_cast<QWindow *>(window->property("toastWindow")));
    });

    QObject::connect(&overlay, &Overlay::unused, &overlay, [&] { releaseUi(overlay, model.get(), engine); });

    HoldHint hint(u"/run/microos/hold"_s, u"/proc"_s);
    QObject::connect(&hint, &HoldHint::activeChanged, &overlay, [&overlay, &hint] {
        overlay.setHint(hint.active(), hint.device(), hint.kind());
    });

    Auth auth(session, u"/proc"_s);
    OverlayService service(&overlay, auth);
    if (!service.publish(session)) {
        fail(u"cannot own %1 on the session bus"_s.arg(OverlayService::service()));
        return 1;
    }
    overlay.setHint(hint.active(), hint.device(), hint.kind());

    QTimer watchdog;
    watchdog.setTimerType(Qt::CoarseTimer);
    watchdog.setInterval(watchdogMs);
    QObject::connect(&watchdog, &QTimer::timeout, [] { notify(QByteArrayLiteral("WATCHDOG=1\n")); });
    if (notify(QByteArrayLiteral("READY=1\n"))) {
        notify(QByteArrayLiteral("WATCHDOG=1\n"));
        watchdog.start();
    }
    const int code = QCoreApplication::exec();
    if (code != 0 && microset::displayGone()) {
        QTextStream(stderr) << "microset-settings: X display gone, exiting\n";
        return 0;
    }
    return code;
}
