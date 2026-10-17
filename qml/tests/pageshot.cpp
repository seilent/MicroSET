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
#include <QTextStream>
#include <QTimer>
#include <QtQml/qqmlextensionplugin.h>

Q_IMPORT_QML_PLUGIN(MicroSetPlugin)
Q_IMPORT_QML_PLUGIN(MicroSetPageTestPlugin)

using namespace Qt::StringLiterals;

namespace {

constexpr int frameTimeoutMs = 5000;
const QString LongProfile = u"Sennheiser HD 600 ParametricEQ oratory1990 Harman over-ear 2018 target"_s;

void fail(const QString &message)
{
    QTextStream(stderr) << "microset-pageshot: " << message << '\n';
}

const QStringList &states()
{
    static const QStringList names{
        u"top"_s,     u"audio"_s, u"display"_s,  u"rgb"_s,  u"power"_s,      u"system"_s,  u"updates"_s,
        u"edit"_s,    u"hue-edit"_s, u"draft-edit"_s, u"toast"_s, u"long-label"_s, u"long-value"_s, u"merged"_s,
        u"top-sections"_s,
    };
    return names;
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
    parser.addOptions({screenshotOption, sizeOption, stateOption});
    parser.process(app);

    const QString state = parser.value(stateOption);
    const QRegularExpressionMatch size = QRegularExpression(u"^(\\d+)x(\\d+)$"_s).match(parser.value(sizeOption));
    if (!parser.isSet(screenshotOption) || !states().contains(state) || !size.hasMatch()) {
        fail(u"usage: --screenshot <png> [--size WxH] [--state %1]"_s.arg(states().join(u'|')));
        return 2;
    }
    if (qEnvironmentVariableIsEmpty("QT_QUICK_BACKEND"))
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);

    QStringList files{FakeMicroSetd::shipped()};
    if (state == "long-label"_L1 || state == "merged"_L1 || state == "top-sections"_L1)
        files << FakeMicroSetd::fixture(u"50-fixture.json"_s) << FakeMicroSetd::fixture(u"60-microdap.json"_s);
    SettingsModel model;
    model.updates()->setFixture(u"testing-38784f1"_s, u"none"_s, {});
    model.setFixture(FakeMicroSetd::describe(files),
                     {{u"audio.output"_s, u"speaker"_s},
                      {u"audio.eq.headphones.on"_s, true},
                      {u"audio.eq.headphones.name"_s, state == "long-value"_L1 ? LongProfile : u"Sennheiser HD 600 ParametricEQ"_s},
                      {u"fixture.long"_s, u"extended"_s},
                      {u"microdap.accent_hue"_s, 200u},
                      {u"display.rotation"_s, u"landscape-flipped"_s}},
                     {{u"rgb.enabled"_s, true}, {u"rgb.color"_s, 0x40c0ffu}});

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
