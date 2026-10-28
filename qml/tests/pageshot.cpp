#include "fakemicrosetd.h"
#include "settingsmodel.h"

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
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <QtQml/qqmlextensionplugin.h>

Q_IMPORT_QML_PLUGIN(MicroSetPlugin)
Q_IMPORT_QML_PLUGIN(MicroSetPageTestPlugin)

using namespace Qt::StringLiterals;

namespace {

constexpr int frameTimeoutMs = 5000;
constexpr int motionFrameMs = 33;
constexpr int motionFrames = 40;
const QString LongProfile = u"Sennheiser HD 600 ParametricEQ oratory1990 Harman over-ear 2018 target"_s;

void fail(const QString &message)
{
    QTextStream(stderr) << "microset-pageshot: " << message << '\n';
}

const QStringList &states()
{
    static const QStringList names{
        u"top"_s,     u"audio"_s, u"display"_s,  u"rgb"_s,  u"rgb-off"_s,  u"power"_s,  u"system"_s,  u"updates"_s,
        u"edit-balance"_s, u"hue-edit"_s, u"toast"_s, u"long-label"_s, u"long-value"_s, u"merged"_s,
        u"top-sections"_s, u"undervolt-off"_s, u"undervolt"_s, u"undervolt-next"_s, u"undervolt-waiting"_s, u"undervolt-fallback"_s,
        u"undervolt-long"_s, u"undervolt-changed"_s, u"undervolt-reason-focus"_s, u"undervolt-hold"_s,
        u"undervolt-refused"_s, u"undervolt-busy"_s, u"undervolt-trip"_s, u"undervolt-trip-long"_s,
        u"undervolt-both"_s, u"sheet-volume-step"_s,
        u"sheet-gpu-clock"_s, u"sheet-power-on"_s, u"sheet-brightness-off-list"_s, u"sheet-saturation-moved"_s,
        u"sheet-game-floor"_s, u"sheet-undervolt"_s, u"sheet-refused"_s, u"sheet-emu-scheme"_s, u"sheet-emu-scheme-end"_s,
        u"sheet-emu-variant"_s, u"sheet-long-label"_s, u"flip-before"_s, u"flip-after"_s, u"sheet-motion"_s,
        u"games"_s, u"games-empty"_s, u"games-many"_s, u"games-long"_s, u"games-bottom"_s, u"game-profile"_s,
        u"game-overridden"_s, u"game-profile-off"_s, u"game-preset"_s, u"game-global"_s, u"game-bottom"_s,
        u"game-long"_s, u"sheet-tso"_s, u"sheet-tso-on"_s, u"sheet-preset"_s, u"sheet-profile"_s, u"sheet-smc"_s,
        u"sheet-global-preset"_s,
    };
    return names;
}

struct Games
{
    QList<QPair<QString, QString>> library;
    QList<QPair<quint32, QString>> shortcuts;
    QVariantMap stored;
};

Games gamesOf(const QString &state)
{
    Games out;
    if (state == "games"_L1 || state == "games-bottom"_L1) {
        out.library = {{u"292120"_s, u"FINAL FANTASY XIII"_s}, {u"2238900"_s, u"STAR OCEAN THE SECOND STORY R"_s},
                       {u"2161700"_s, u"Persona 3 Reload"_s}, {u"1621310"_s, u"Atelier Sophie 2: The Alchemist of the Mysterious Dream"_s},
                       {u"2050650"_s, u"Resident Evil 4"_s}};
        out.shortcuts = {{2763258687u, u"Octopath Traveler"_s}, {2342494927u, u"Trails in the Sky 1st Chapter"_s}};
        out.stored = {{u"app.2238900.fex.preset"_s, u"performance"_s}, {u"app.1145360.fex.tso"_s, u"on"_s}};
    } else if (state == "games-many"_L1) {
        for (int i = 1; i <= 40; ++i)
            out.library << qMakePair(QString::number(1000 + i), u"Game %1"_s.arg(i, 2, 10, QChar(u'0')));
    } else if (state == "games-long"_L1 || state.startsWith("game-"_L1) || state.startsWith("sheet-"_L1)) {
        out.library = {{u"292120"_s, u"FINAL FANTASY XIII"_s}, {u"2238900"_s, u"STAR OCEAN THE SECOND STORY R"_s},
                       {u"251150"_s, u"The Legend of Heroes: Trails in the Sky the 1st Chapter Deluxe Edition with Every Bonus"_s}};
    }
    const QMap<QString, QVariantMap> stored{
        {u"game-overridden"_s, {{u"app.292120.fex.tso"_s, u"on"_s}}},
        {u"game-profile-off"_s, {{u"app.292120.profile"_s, u"off"_s}}},
        {u"game-preset"_s, {{u"app.2238900.fex.preset"_s, u"performance"_s}}},
        {u"sheet-preset"_s, {{u"app.2238900.fex.preset"_s, u"performance"_s}}},
        {u"game-global"_s, {{u"fex.preset"_s, u"compatibility"_s}, {u"wine.sync"_s, u"fsync"_s}}},
    };
    if (stored.contains(state))
        out.stored = stored.value(state);
    return out;
}

}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    const QCommandLineOption screenshotOption(u"screenshot"_s, u"Render offscreen to <png> and exit."_s, u"png"_s);
    const QCommandLineOption sizeOption(u"size"_s, u"Window size."_s, u"WxH"_s, u"960x640"_s);
    const QCommandLineOption stateOption(u"state"_s, u"Fixture state."_s, u"name"_s, u"top"_s);
    const QCommandLineOption framesOption(u"frames"_s, u"Write f0000.png and on into <dir> every 33 ms."_s, u"dir"_s);
    parser.addOptions({screenshotOption, sizeOption, stateOption, framesOption});
    parser.process(app);

    const QString state = parser.value(stateOption);
    const QRegularExpressionMatch size = QRegularExpression(u"^(\\d+)x(\\d+)$"_s).match(parser.value(sizeOption));
    if ((!parser.isSet(screenshotOption) && !parser.isSet(framesOption)) || !states().contains(state) || !size.hasMatch()) {
        fail(u"usage: --screenshot <png> | --frames <dir> [--size WxH] [--state %1]"_s.arg(states().join(u'|')));
        return 2;
    }
    if (qEnvironmentVariableIsEmpty("QT_QUICK_BACKEND"))
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);

    QStringList files{FakeMicroSetd::shipped()};
    if (state == "long-label"_L1 || state == "merged"_L1 || state == "top-sections"_L1 || state == "sheet-long-label"_L1)
        files << FakeMicroSetd::fixture(u"50-fixture.json"_s) << FakeMicroSetd::fixture(u"60-microdap.json"_s);
    if (state.startsWith("sheet-emu"_L1))
        files << FakeMicroSetd::fixture(u"50-microemu.json"_s);
    const QMap<QString, QStringList> undervolt{
        {u"undervolt"_s, {u"low"_s, u"low"_s, QString(), QString()}},
        {u"undervolt-next"_s, {u"low"_s, u"stock"_s, QString(), QString()}},
        {u"undervolt-waiting"_s, {u"low"_s, u"waiting"_s, QString(), QString()}},
        {u"undervolt-fallback"_s, {u"off"_s, u"stock"_s, u"hang"_s, QString()}},
        {u"undervolt-long"_s, {u"off"_s, u"stock"_s, u"unclean"_s, QString()}},
        {u"undervolt-changed"_s, {u"off"_s, u"stock"_s, u"changed"_s, QString()}},
        {u"undervolt-reason-focus"_s, {u"off"_s, u"stock"_s, u"changed"_s, QString()}},
        {u"undervolt-hold"_s, {u"off"_s, u"stock"_s, QString(), QString()}},
        {u"undervolt-refused"_s, {u"off"_s, u"stock"_s, QString(), QString()}},
        {u"undervolt-busy"_s, {u"off"_s, u"stock"_s, QString(), QString()}},
        {u"undervolt-trip"_s, {u"low"_s, u"stock"_s, QString(), u"hang"_s}},
        {u"undervolt-trip-long"_s, {u"low"_s, u"stock"_s, QString(), u"start"_s}},
        {u"undervolt-both"_s, {u"off"_s, u"stock"_s, u"start"_s, u"start"_s}},
    };
    const QStringList preset = undervolt.value(state, {u"off"_s, u"stock"_s, QString(), QString()});
    const Games games = gamesOf(state);
    QTemporaryDir steam;
    qputenv("MICROSET_STEAM_DIR", steam.path().toUtf8());
    FakeMicroSetd::writeSteam(steam.path(), games.library, games.shortcuts);
    QMap<QString, QJsonObject> effective;
    for (const auto &[appid, name] : games.library)
        effective.insert(appid, FakeMicroSetd::effective(files, games.stored, FakeMicroSetd::Profiles, appid));
    for (const auto &[appid, name] : games.shortcuts)
        effective.insert(QString::number(appid),
                         FakeMicroSetd::effective(files, games.stored, FakeMicroSetd::Profiles, QString::number(appid)));
    QVariantMap values{{u"audio.output"_s, u"speaker"_s},
                       {u"audio.eq.headphones.on"_s, true},
                       {u"audio.eq.headphones.name"_s, state == "long-value"_L1 ? LongProfile : u"Sennheiser HD 600 ParametricEQ"_s},
                       {u"fixture.long"_s, u"extended"_s},
                       {u"microdap.accent_hue"_s, 200u},
                       {u"display.rotation"_s, u"landscape-flipped"_s},
                       {u"rgb.brightness"_s, 50u},
                       {u"power.gpu_undervolt"_s, preset.at(0)},
                       {u"power.gpu_undervolt_active"_s, preset.at(1)},
                       {u"power.gpu_tuning_fallback"_s, preset.at(2)},
                       {u"power.gpu_tuning_trip"_s, preset.at(3)},
                       {u"power.gpu_level"_s, state == "sheet-gpu-clock"_L1 ? u"manual"_s : u"auto"_s},
                       {u"power.gpu_clock"_s, 490u},
                       {u"audio.power_on_volume"_s, state == "sheet-power-on"_L1 ? 15u : 0u},
                       {u"microemu.esde.theme_variant"_s, u"gamelist-list-metadata-screenshot-marquee-nh"_s}};
    values.insert(games.stored);
    SettingsModel model;
    model.updates()->setFixture(u"testing-38784f1"_s, u"none"_s, {});
    model.setFixture(FakeMicroSetd::describe(files), values,
                     {{u"rgb.enabled"_s, state != "rgb-off"_L1}, {u"rgb.hue"_s, 200u}, {u"rgb.saturation"_s, 60u}});
    model.setProtonFixture(effective);

    QQmlApplicationEngine engine;
    bool warned = false;
    QObject::connect(&engine, &QQmlApplicationEngine::warnings, &engine, [&warned](const QList<QQmlError> &list) {
        for (const QQmlError &e : list)
            fail(e.toString());
        warned = true;
    });
    engine.setInitialProperties({
        {u"model"_s, QVariant::fromValue(static_cast<QObject *>(&model))},
        {u"screenshot"_s, true},
        {u"shotWidth"_s, size.captured(1).toInt()},
        {u"shotHeight"_s, size.captured(2).toInt()},
        {u"shotState"_s, state},
    });
    engine.loadFromModule("MicroSetPageTest", "PageRoot");
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
    if (!window) {
        fail(u"MicroSetPageTest.PageRoot did not produce a window"_s);
        return 1;
    }

    if (parser.isSet(framesOption)) {
        const QString dir = parser.value(framesOption);
        QDir().mkpath(dir);
        auto *tick = new QTimer(window);
        tick->setTimerType(Qt::PreciseTimer);
        tick->setInterval(motionFrameMs);
        int frame = 0;
        QObject::connect(tick, &QTimer::timeout, window, [window, dir, &frame, &warned] {
            const QImage image = window->grabWindow();
            if (warned || image.isNull() || !image.save(u"%1/f%2.png"_s.arg(dir).arg(frame, 4, 10, QChar(u'0')), "PNG")) {
                fail(warned ? u"QML warnings during the render"_s : u"cannot write a frame into %1"_s.arg(dir));
                QCoreApplication::exit(1);
                return;
            }
            if (++frame == motionFrames)
                QCoreApplication::exit(0);
        });
        tick->start();
        return QCoreApplication::exec();
    }

    const QString out = parser.value(screenshotOption);
    auto *idle = new QTimer(window);
    idle->setSingleShot(true);
    idle->setInterval(window->property("shotSettle").toInt());
    bool swapped = false;
    QObject::connect(window, &QQuickWindow::frameSwapped, idle, [idle, &swapped] {
        swapped = true;
        idle->start();
    });
    QObject::connect(idle, &QTimer::timeout, window, [window, out, &warned] {
        if (warned) {
            fail(u"QML warnings during the render"_s);
            QCoreApplication::exit(1);
            return;
        }
        if (QFontInfo(QFont(u"Selawik"_s)).family() != u"Selawik"_s)
            fail(u"warning: Selawik not loaded, set MICROSET_FONT_DIR"_s);
        const QImage image = window->grabWindow();
        QDir().mkpath(QFileInfo(out).absolutePath());
        if (image.isNull() || !image.save(out, "PNG")) {
            fail(u"cannot write %1"_s.arg(out));
            QCoreApplication::exit(1);
            return;
        }
        QCoreApplication::exit(0);
    });
    window->update();
    QTimer::singleShot(frameTimeoutMs, window, [&swapped] {
        fail((swapped ? u"frames never settled within %1 ms"_s : u"no frame within %1 ms"_s).arg(frameTimeoutMs));
        QCoreApplication::exit(1);
    });
    return QCoreApplication::exec();
}
