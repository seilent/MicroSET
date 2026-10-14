#include "gamescope.h"
#include "recovery.h"

#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QFontInfo>
#include <QGuiApplication>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QTextStream>
#include <QTimer>
#include <QtQml/qqmlextensionplugin.h>

Q_IMPORT_QML_PLUGIN(MicroSetRecoveryPlugin)

using namespace Qt::StringLiterals;

namespace {

constexpr int frameTimeoutMs = 5000;

void fail(const QString &message)
{
    QTextStream(stderr) << "microset-recovery: " << message << '\n';
}

bool parseSize(const QString &size, int &width, int &height)
{
    static const QRegularExpression pattern(u"^(\\d+)x(\\d+)$"_s);
    const QRegularExpressionMatch match = pattern.match(size);
    if (!match.hasMatch())
        return false;
    width = match.captured(1).toInt();
    height = match.captured(2).toInt();
    return width > 0 && height > 0;
}

const QStringList &states()
{
    static const QStringList names{
        u"browse"_s,
        u"rollback"_s,
        u"long-reason"_s,
        u"empty-journal"_s,
        u"short-journal"_s,
        u"dialog"_s,
        u"toast"_s,
        u"no-previous"_s,
        u"long-line"_s,
        u"requested"_s,
        u"hold"_s,
    };
    return names;
}

QStringList sampleJournal(int count)
{
    static const QStringList lines{
        u"gamescope-session-plus: starting steam"_s,
        u"[gamescope] [Info]  console: gamescope version 3.16.31-ogc2 (gcc 16.0.1)"_s,
        u"[gamescope] [Info]  scriptmgr: Loading scripts from: '/usr/share/gamescope/scripts'"_s,
        u"[gamescope] [Info]  drm: selecting connector DSI-1"_s,
        u"[gamescope] [Info]  drm: Connectors: DSI-1 (connected)"_s,
        u"[gamescope] [Info]  vulkan: selecting physical device 'Turnip Adreno (TM) 650': queue family 0"_s,
        u"[gamescope] [Info]  wlserver: Running compositor on wayland display 'gamescope-0'"_s,
        u"[gamescope] [Info]  xwm: Embedded, no cursor set. Using left_ptr by default."_s,
        u"steam.sh[2141]: Running Steam on fedora 44 64-bit"_s,
        u"steam.sh[2141]: STEAM_RUNTIME is enabled automatically"_s,
        u"steam.sh[2141]: Steam client's requirements are satisfied"_s,
        u"CApplicationManagerPopulateThread took 412 milliseconds to initialize"_s,
        u"[2026-10-14 21:04:11] Startup - updater built Oct 10 2026 18:22:48"_s,
        u"[2026-10-14 21:04:12] Verifying installation..."_s,
        u"[2026-10-14 21:04:13] Verification complete"_s,
        u"Steam Runtime Launch Service: starting steam-runtime-launcher-service"_s,
        u"BuildCompleteAppOverviewChange: 214 apps"_s,
        u"RegisterForAppOverviewChanges: 214 apps"_s,
        u"[gamescope] [Error] xwm: got the same buffer committed twice, ignoring."_s,
        u"steamwebhelper.sh[2388]: Running steamwebhelper"_s,
        u"steamwebhelper: Received signal 11 (SIGSEGV), code 1, addr 0x0000000000000018"_s,
        u"steam.sh[2141]: Steam exited with code 139"_s,
        u"gamescope-session-plus: client exited, stopping gamescope"_s,
        u"[gamescope] [Info]  wlserver: Shutting down"_s,
        u"gamescope-session-plus@steam.service: Main process exited, code=exited, status=139/n/a"_s,
        u"gamescope-session-plus@steam.service: Failed with result 'exit-code'."_s,
        u"gamescope-session-plus@steam.service: Consumed 21.402s CPU time over 18.311s wall clock time, 812.4M memory peak."_s,
    };
    QStringList out;
    for (int i = 0; i < count; ++i)
        out.append(lines.at(i % lines.size()));
    return out;
}

void applyFixture(Recovery &recovery, const QString &state)
{
    const bool rollback = state == u"rollback"_s || state == u"dialog"_s || state == u"hold"_s;
    QStringList journal = state == u"empty-journal"_s ? QStringList()
        : state == u"short-journal"_s ? sampleJournal(6)
        : sampleJournal(Recovery::journalLimit);
    if (state == u"long-line"_s) {
        journal.removeFirst();
        journal.append(u"steamwebhelper: [1014/210413.512:ERROR:gpu_init.cc(523)] Passthrough is not supported, GL is "
                       "egl, ANGLE is vulkan; failed to create the GPU process host for "
                       "/home/micro/.local/share/Steam/ubuntu12_64/steamwebhelper_sniper_wrap.sh --type=gpu-process "
                       "--enable-crash-reporter=,steam-client --use-angle=vulkan"_s);
    }
    if (state == u"long-reason"_s) {
        recovery.setFixture(u"microdap"_s, u"MicroDAP Hi-Res Audio Player Session"_s,
                            u"gamescope-session-plus@microdap.service"_s, true, u"steam"_s, true, journal);
        return;
    }
    const bool failed = state != u"no-previous"_s && state != u"requested"_s;
    recovery.setFixture(u"steam"_s, u"Steam"_s, u"gamescope-session-plus@steam.service"_s, failed,
                        u"steam"_s, rollback, journal);
}

void grab(QQuickWindow *window, const QString &out)
{
    if (QFontInfo(QFont(u"Selawik"_s)).family() != u"Selawik"_s)
        fail(u"warning: Selawik not loaded, set MICROSET_FONT_DIR"_s);
    const QImage image = window->grabWindow();
    if (image.isNull()) {
        fail(u"grabWindow returned no image"_s);
        QCoreApplication::exit(1);
        return;
    }
    QDir().mkpath(QFileInfo(out).absolutePath());
    if (!image.save(out, "PNG")) {
        fail(u"cannot write %1"_s.arg(out));
        QCoreApplication::exit(1);
        return;
    }
    QCoreApplication::exit(0);
}

}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(u"microset-recovery"_s);

    QCommandLineParser parser;
    parser.addHelpOption();
    const QCommandLineOption screenshotOption(u"screenshot"_s, u"Render offscreen to <png> and exit."_s, u"png"_s);
    const QCommandLineOption sizeOption(u"size"_s, u"Window size for --screenshot."_s, u"WxH"_s, u"640x960"_s);
    const QCommandLineOption stateOption(u"state"_s, u"Fixture state for --screenshot, default browse."_s,
                                         u"name"_s, u"browse"_s);
    const QCommandLineOption focusOption(u"focus"_s, u"Focused action index for --screenshot."_s, u"index"_s,
                                         u"0"_s);
    parser.addOptions({screenshotOption, sizeOption, stateOption, focusOption});
    parser.process(app);

    const bool screenshot = parser.isSet(screenshotOption);
    Recovery recovery;
    int width = 0;
    int height = 0;
    if (screenshot) {
        if (!states().contains(parser.value(stateOption))) {
            fail(u"unknown state %1, expected one of %2"_s.arg(parser.value(stateOption), states().join(u' ')));
            return 2;
        }
        if (!parseSize(parser.value(sizeOption), width, height)) {
            fail(u"bad size %1, expected WxH"_s.arg(parser.value(sizeOption)));
            return 2;
        }
        if (qEnvironmentVariableIsEmpty("QT_QUICK_BACKEND"))
            QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
        applyFixture(recovery, parser.value(stateOption));
    }

    QQmlApplicationEngine engine;
    bool warned = false;
    QObject::connect(&engine, &QQmlApplicationEngine::warnings, &engine, [&warned](const QList<QQmlError> &list) {
        for (const QQmlError &e : list)
            fail(e.toString());
        warned = true;
    });
    engine.setInitialProperties({
        {u"recovery"_s, QVariant::fromValue(static_cast<QObject *>(&recovery))},
        {u"screenshot"_s, screenshot},
        {u"shotWidth"_s, width},
        {u"shotHeight"_s, height},
        {u"shotState"_s, parser.value(stateOption)},
        {u"shotFocus"_s, parser.value(focusOption).toInt()},
    });
    engine.loadFromModule("MicroSetRecovery", "Main");

    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
    if (!window) {
        fail(u"MicroSetRecovery.Main did not produce a window"_s);
        return 1;
    }

    if (!screenshot) {
        microset::claimGamescopeFocus(window);
        recovery.load();
        return QCoreApplication::exec();
    }

    const QString out = parser.value(screenshotOption);
    const int settle = window->property("shotSettle").toInt();
    QTimer::singleShot(settle, window, [window, out, &warned] {
        QObject::connect(window, &QQuickWindow::frameSwapped, window, [window, out, &warned] {
            QTimer::singleShot(0, window, [window, out, &warned] {
                if (warned) {
                    fail(u"QML warnings during the render"_s);
                    QCoreApplication::exit(1);
                    return;
                }
                grab(window, out);
            });
        }, Qt::SingleShotConnection);
        window->update();
    });
    QTimer::singleShot(frameTimeoutMs, window, [] {
        fail(u"no frame within %1 ms"_s.arg(frameTimeoutMs));
        QCoreApplication::exit(1);
    });
    return QCoreApplication::exec();
}
