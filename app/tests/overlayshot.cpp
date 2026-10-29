#include "fakemicrosetd.h"
#include "harness.h"
#include "settingsmodel.h"

#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
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

#include <memory>

Q_IMPORT_QML_PLUGIN(MicroSetPlugin)
Q_IMPORT_QML_PLUGIN(MicroSetOverlayPlugin)

using namespace Qt::StringLiterals;

namespace {

constexpr int frameTimeoutMs = 5000;

void fail(const QString &message)
{
    QTextStream(stderr) << "microset-overlayshot: " << message << '\n';
}

const QStringList &states()
{
    static const QStringList names{u"top"_s,  u"audio"_s,         u"portrait"_s,      u"hint-page"_s,
                                   u"hint"_s, u"hint-portrait"_s, u"sheet"_s,         u"sheet-portrait"_s,
                                   u"undervolt-after-sheet"_s, u"game"_s, u"game-missing"_s, u"game-state"_s,
                                   u"game-yield"_s, u"game-other-tool"_s, u"game-gone"_s, u"game-native"_s,
                                   u"game-page"_s, u"game-lsfg"_s, u"game-lsfg-limit"_s, u"game-lsfg-failed"_s,
                                   u"game-lsfg-cap"_s, u"game-lsfg-young"_s, u"game-lsfg-off"_s, u"game-lsfg-no-dll"_s,
                                   u"game-lsfg-env"_s, u"game-lsfg-check"_s, u"game-lsfg-state"_s, u"game-lsfg-page"_s};
    return names;
}

QVariantMap gameOf(const QString &state)
{
    QVariantMap game{{u"appid"_s, u"292120"_s}, {u"unit"_s, u"app-steam-app292120-100.scope"_s}, {u"hook"_s, u"ok"_s},
                     {u"tool"_s, u"Proton 11.0 (ARM64)"_s}, {u"proton"_s, true}, {u"steamFex"_s, false}};
    if (state == "game-missing"_L1)
        game.insert(u"hook"_s, QString());
    else if (state == "game-state"_L1)
        game.insert(u"hook"_s, u"bad-local,no-fex-profiles"_s);
    else if (state == "game-yield"_L1)
        game.insert({{u"hook"_s, u"no-fex-profiles"_s}, {u"yielded"_s, u"FEX_TSOENABLED,FEX_MULTIBLOCK"_s}});
    else if (state == "game-other-tool"_L1)
        game.insert(u"profile"_s, u"other-tool"_s);
    else if (state == "game-gone"_L1)
        game.insert(u"gone"_s, true);
    else if (state == "game-native"_L1)
        game = {{u"appid"_s, u"2238900"_s}, {u"hook"_s, QString()}, {u"tool"_s, QString()}, {u"proton"_s, false}};
    else if (state == "game-page"_L1)
        game.insert(u"steamFex"_s, true);
    if (!state.startsWith("game-lsfg"_L1))
        return game;
    const QMap<QString, QString> lsfg{{u"game-lsfg-cap"_s, u"cap-overridden"_s}, {u"game-lsfg-off"_s, u"off"_s},
                                      {u"game-lsfg-no-dll"_s, u"no-dll"_s}, {u"game-lsfg-env"_s, u"disabled-by-env"_s},
                                      {u"game-lsfg-check"_s, u"check-failed"_s}};
    const QString value = lsfg.value(state, u"on"_s);
    game.insert({{u"lsfg"_s, value}, {u"fpsLimit"_s, state == "game-lsfg-limit"_L1 ? 30 : 60}});
    if (value == "on"_L1 || value == "cap-overridden"_L1)
        game.insert({{u"lsfgHeld"_s, state != "game-lsfg-failed"_L1 && state != "game-lsfg-young"_L1},
                     {u"lsfgYoung"_s, state == "game-lsfg-young"_L1}});
    if (state == "game-lsfg-state"_L1)
        game.insert({{u"hook"_s, u"no-fex-profiles"_s}, {u"yielded"_s, u"FEX_TSOENABLED,FEX_MULTIBLOCK"_s}});
    else if (state == "game-lsfg-page"_L1)
        game.insert(u"steamFex"_s, true);
    return game;
}

class ShotOverlay : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool shown READ shown CONSTANT)
    Q_PROPERTY(bool mapped READ shown CONSTANT)
    Q_PROPERTY(qreal opacity READ opacity CONSTANT)
    Q_PROPERTY(bool hintShown READ hintShown CONSTANT)
    Q_PROPERTY(bool hint READ hint CONSTANT)
    Q_PROPERTY(QString hintDevice READ hintDevice CONSTANT)
    Q_PROPERTY(QString hintKind MEMBER kind CONSTANT)

public:
    bool shown() const { return true; }
    qreal opacity() const { return 1.0; }
    bool hintShown() const { return false; }
    bool hint() const { return true; }
    QString hintDevice() const { return QStringLiteral("/dev/mmcblk0"); }
    Q_INVOKABLE void hide() {}
    Q_INVOKABLE void hintDone() {}

    QString kind;

signals:
    void shownChanged();
    void pageHint();
    void hintShownChanged();
    void hintChanged();
};

}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    const QCommandLineOption screenshotOption(u"screenshot"_s, u"Render offscreen to <png> and exit."_s, u"png"_s);
    const QCommandLineOption sizeOption(u"size"_s, u"X screen size."_s, u"WxH"_s, u"960x640"_s);
    const QCommandLineOption stateOption(u"state"_s, u"Fixture state."_s, u"name"_s, u"top"_s);
    const QCommandLineOption kindOption(u"hint-kind"_s, u"Hold hint kind."_s, u"format|undervolt"_s, u"format"_s);
    parser.addOptions({screenshotOption, sizeOption, stateOption, kindOption});
    parser.process(app);

    const QString state = parser.value(stateOption);
    const QRegularExpressionMatch size = QRegularExpression(u"^(\\d+)x(\\d+)$"_s).match(parser.value(sizeOption));
    if (!parser.isSet(screenshotOption) || !states().contains(state) || !size.hasMatch()) {
        fail(u"usage: --screenshot <png> [--size WxH] [--state %1]"_s.arg(states().join(u'|')));
        return 2;
    }
    if (qEnvironmentVariableIsEmpty("QT_QUICK_BACKEND"))
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);

    const QVariantMap values{
        {u"audio.output"_s, u"speaker"_s},
        {u"audio.eq.headphones.on"_s, true},
        {u"audio.eq.headphones.name"_s, u"Sennheiser HD 600 ParametricEQ"_s},
        {u"display.rotation"_s, state == "portrait"_L1 || state == "hint-portrait"_L1 || state == "sheet-portrait"_L1
                                    || (state.startsWith("game"_L1) && size.captured(2).toInt() > size.captured(1).toInt())
                                    ? u"portrait"_s : u"landscape-flipped"_s},
        {u"rgb.brightness"_s, 50u},
        {u"power.gpu_undervolt"_s, u"off"_s},
        {u"power.gpu_undervolt_active"_s, u"stock"_s}};
    const QVariantMap stored{{u"rgb.enabled"_s, true}, {u"rgb.hue"_s, 200u}, {u"rgb.saturation"_s, 60u}};
    const bool hold = state == "undervolt-after-sheet"_L1;
    Harness harness;
    if (hold && !harness.startBus()) {
        fail(u"cannot start the private bus"_s);
        return 1;
    }
    FakeMicroSetd fake(hold ? harness.address() : QString());
    std::unique_ptr<SettingsModel> model;
    if (hold) {
        fake.setSchema(FakeMicroSetd::describe({FakeMicroSetd::shipped()}));
        fake.setValues(values);
        fake.setStored(stored);
        fake.refuse(u"power.gpu_undervolt"_s, u"net.seilent.MicroSet1.Error.HoldPending"_s);
        if (!fake.start()) {
            fail(u"cannot start the fake microsetd"_s);
            return 1;
        }
        model = std::make_unique<SettingsModel>(harness.client());
        model->load();
        QElapsedTimer waited;
        waited.start();
        while (!model->available() && waited.elapsed() < frameTimeoutMs)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        if (!model->available()) {
            fail(u"the fake microsetd never answered"_s);
            return 1;
        }
    } else {
        model = std::make_unique<SettingsModel>();
        model->setFixture(FakeMicroSetd::describe({FakeMicroSetd::shipped()}), values, stored);
    }
    QTemporaryDir steam;
    if (state.startsWith("game"_L1)) {
        const QStringList files{FakeMicroSetd::shipped()};
        qputenv("MICROSET_STEAM_DIR", steam.path().toUtf8());
        FakeMicroSetd::writeSteam(steam.path(), {{u"292120"_s, u"FINAL FANTASY XIII"_s},
                                                 {u"2238900"_s, u"STAR OCEAN THE SECOND STORY R"_s}});
        model->setProtonFixture({{u"292120"_s, FakeMicroSetd::effective(files, {}, FakeMicroSetd::Profiles, u"292120"_s)},
                                 {u"2238900"_s, FakeMicroSetd::effective(files, {}, FakeMicroSetd::Profiles, u"2238900"_s)}});
        if (state.startsWith("game-lsfg"_L1))
            model->lsfg()->setFixture(QJsonObject{{u"owned"_s, u"yes"_s}, {u"install"_s, u"installed"_s}}, true, u"steam"_s);
        model->setGame(gameOf(state));
    }
    model->updates()->setFixture(u"testing-38784f1"_s, u"none"_s, {});
    ShotOverlay overlay;
    overlay.kind = hold ? u"undervolt"_s : parser.value(kindOption);

    QQmlApplicationEngine engine;
    bool warned = false;
    QObject::connect(&engine, &QQmlApplicationEngine::warnings, &engine, [&warned](const QList<QQmlError> &list) {
        for (const QQmlError &e : list)
            fail(e.toString());
        warned = true;
    });
    engine.setInitialProperties({
        {u"overlay"_s, QVariant::fromValue(static_cast<QObject *>(&overlay))},
        {u"model"_s, QVariant::fromValue(static_cast<QObject *>(model.get()))},
        {u"screenshot"_s, true},
        {u"shotWidth"_s, size.captured(1).toInt()},
        {u"shotHeight"_s, size.captured(2).toInt()},
        {u"shotState"_s, state},
    });
    engine.loadFromModule("MicroSetOverlay", "Overlay");
    auto *root = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
    if (!root) {
        fail(u"MicroSetOverlay.Overlay did not produce a window"_s);
        return 1;
    }
    auto *window = state.startsWith("hint"_L1) && state != "hint-page"_L1 ? qvariant_cast<QQuickWindow *>(root->property("toastWindow")) : root;
    if (!window) {
        fail(u"no toast window"_s);
        return 1;
    }

    bool hinted = !hold;
    if (hold) {
        auto *powerd = new QTimer(window);
        powerd->setInterval(10);
        QObject::connect(powerd, &QTimer::timeout, window, [powerd, &fake, &overlay, &hinted] {
            if (fake.sets().isEmpty())
                return;
            powerd->stop();
            hinted = true;
            emit overlay.pageHint();
        });
        powerd->start();
    }

    const QString out = parser.value(screenshotOption);
    auto *idle = new QTimer(window);
    idle->setSingleShot(true);
    idle->setInterval(root->property("shotSettle").toInt());
    bool swapped = false;
    QObject::connect(window, &QQuickWindow::frameSwapped, idle, [idle, &swapped] {
        swapped = true;
        idle->start();
    });
    QObject::connect(idle, &QTimer::timeout, window, [window, idle, out, &warned, &hinted] {
        if (!hinted) {
            idle->start();
            return;
        }
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

#include "overlayshot.moc"
