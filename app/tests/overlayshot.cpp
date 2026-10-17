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
    static const QStringList names{u"top"_s, u"audio"_s, u"portrait"_s, u"hint-page"_s, u"hint"_s, u"hint-portrait"_s};
    return names;
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

public:
    bool shown() const { return true; }
    qreal opacity() const { return 1.0; }
    bool hintShown() const { return false; }
    bool hint() const { return true; }
    QString hintDevice() const { return QStringLiteral("/dev/mmcblk0"); }
    Q_INVOKABLE void hide() {}
    Q_INVOKABLE void hintDone() {}

signals:
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

    SettingsModel model;
    model.updates()->setFixture(u"testing-38784f1"_s, u"none"_s, {});
    model.setFixture(FakeMicroSetd::describe({FakeMicroSetd::shipped()}),
                     {{u"audio.output"_s, u"speaker"_s},
                      {u"audio.eq.headphones.on"_s, true},
                      {u"audio.eq.headphones.name"_s, u"Sennheiser HD 600 ParametricEQ"_s},
                      {u"display.rotation"_s, state == "portrait"_L1 || state == "hint-portrait"_L1 ? u"portrait"_s : u"landscape-flipped"_s},
                      {u"rgb.brightness"_s, 50u}},
                     {{u"rgb.enabled"_s, true}, {u"rgb.hue"_s, 200u}, {u"rgb.saturation"_s, 60u}});
    ShotOverlay overlay;

    QQmlApplicationEngine engine;
    bool warned = false;
    QObject::connect(&engine, &QQmlApplicationEngine::warnings, &engine, [&warned](const QList<QQmlError> &list) {
        for (const QQmlError &e : list)
            fail(e.toString());
        warned = true;
    });
    engine.setInitialProperties({
        {u"overlay"_s, QVariant::fromValue(static_cast<QObject *>(&overlay))},
        {u"model"_s, QVariant::fromValue(static_cast<QObject *>(&model))},
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

    const QString out = parser.value(screenshotOption);
    auto *idle = new QTimer(window);
    idle->setSingleShot(true);
    idle->setInterval(root->property("shotSettle").toInt());
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

#include "overlayshot.moc"
