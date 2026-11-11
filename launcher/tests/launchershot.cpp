#include "activator.h"
#include "appmodel.h"
#include "desktophome.h"
#include "favoritesmodel.h"
#include "idlepulse.h"
#include "launcher.h"
#include "poweraction.h"

#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QFontInfo>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <QtQml/qqmlextensionplugin.h>

#include <memory>

Q_IMPORT_QML_PLUGIN(MicroSetPlugin)
Q_IMPORT_QML_PLUGIN(MicroSetLauncherPlugin)

using namespace Qt::StringLiterals;

namespace {

constexpr int frameTimeoutMs = 5000;

const QString LongName = u"The Legend of Heroes: Trails in the Sky the 1st Chapter Deluxe Edition with Every Bonus"_s;

void fail(const QString &message)
{
    QTextStream(stderr) << "microset-launchershot: " << message << '\n';
}

const QStringList &states()
{
    static const QStringList names{
        u"home"_s, u"home-empty"_s, u"home-running"_s, u"home-long"_s, u"allapps"_s, u"allapps-zero"_s,
        u"allapps-many"_s, u"allapps-long"_s, u"allapps-deep"_s, u"sheet"_s, u"sheet-allapps"_s, u"jump"_s,
        u"jump-letters"_s, u"jump-end"_s,
        u"power-home"_s, u"power-allapps"_s, u"toast"_s, u"confirm"_s, u"home-battery-low"_s,
    };
    return names;
}

class FakeHome : public DesktopHome
{
    Q_OBJECT

public:
    explicit FakeHome(QObject *parent = nullptr)
        : DesktopHome(parent)
    {
    }

    void enter() override {}
    void toggle() override {}
    void leave() override {}
    void focusWindow(const QString &) override {}

    void publish(bool isShown, const QVariantList &rows, uint turn = 1)
    {
        setAvailable(true);
        setWindows(rows);
        setTransform(turn);
        setShown(isShown);
    }
};

class FakeActivator : public Activator
{
    Q_OBJECT

public:
    using Activator::Activator;

    void start(const QString &id) override { reportStarted(id, 4242); }
    void reportStarted(const QString &id, qint64 pid) override { emit started(id, pid); }
    void reportFailed(const QString &id, const QString &message) override { emit failed(id, message); }
    QMap<QString, QString> environment() const override { return {}; }
};

class FakePower : public PowerAction
{
    Q_OBJECT

public:
    using PowerAction::PowerAction;

    void run(const QString &) override {}
};

class FakeIdle : public IdlePulse
{
public:
    using IdlePulse::IdlePulse;

    void pulse() override { counted(); }
    bool ready() const override { return true; }
};

QVariantMap iconRow(const QString &id, const QString &name, const QString &dir, int index)
{
    QImage image(96, 96, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor::fromHsv((index * 47) % 360, 170, 205));
    painter.drawRoundedRect(4, 4, 88, 88, 20, 20);
    painter.setPen(Qt::white);
    painter.setFont(QFont(u"Selawik"_s, 40, QFont::Bold));
    painter.drawText(image.rect(), Qt::AlignCenter, QString::number(index + 1));
    const QString path = dir + u"/icon%1.png"_s.arg(index);
    image.save(path, "PNG");
    return QVariantMap{{u"id"_s, id}, {u"name"_s, name}, {u"iconFile"_s, path}};
}

QVariantList entries(int count, const QString &firstFlatpak, const QString &dir)
{
    QVariantList out;
    for (int i = 0; i < count; ++i) {
        QVariantMap row = iconRow(u"app%1.desktop"_s.arg(i), u"app %1"_s.arg(i), dir, i);
        if (i == 0 && !firstFlatpak.isEmpty())
            row.insert(u"flatpak"_s, firstFlatpak);
        out << row;
    }
    return out;
}

QVariantList namedEntries(const QStringList &names, const QString &dir)
{
    QVariantList out;
    for (int i = 0; i < names.size(); ++i)
        out << iconRow(names.at(i).toLower().replace(u' ', u'-') + u".desktop"_s, names.at(i), dir, i);
    return out;
}

QVariantList windows(int count)
{
    QVariantList out;
    for (int i = 0; i < count; ++i)
        out << QVariantMap{{u"address"_s, u"0x%1"_s.arg(i + 1, 0, 16)},
                           {u"class"_s, u"foot"_s},
                           {u"title"_s, u"window %1"_s.arg(i + 1)}};
    return out;
}

QStringList ids(const QVariantList &rows, int count)
{
    QStringList out;
    for (int i = 0; i < count && i < rows.size(); ++i)
        out << rows.at(i).toMap().value(u"id"_s).toString();
    return out;
}

}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    const QCommandLineOption screenshotOption(u"screenshot"_s, u"Render offscreen to <png> and exit."_s, u"png"_s);
    const QCommandLineOption sizeOption(u"size"_s, u"Window size."_s, u"WxH"_s, u"480x320"_s);
    const QCommandLineOption stateOption(u"state"_s, u"Fixture state."_s, u"name"_s, u"home"_s);
    const QCommandLineOption accentOffOption(u"accent-off"_s, u"Draw with the accent off."_s);
    parser.addOptions({screenshotOption, sizeOption, stateOption, accentOffOption});
    parser.process(app);

    const QString state = parser.value(stateOption);
    const QRegularExpressionMatch size = QRegularExpression(u"^(\\d+)x(\\d+)$"_s).match(parser.value(sizeOption));
    if (!parser.isSet(screenshotOption) || !states().contains(state) || !size.hasMatch()) {
        fail(u"usage: --screenshot <png> [--size WxH] [--state %1]"_s.arg(states().join(u'|')));
        return 2;
    }
    if (qEnvironmentVariableIsEmpty("QT_QUICK_BACKEND"))
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    if (QFontInfo(QFont(u"Selawik"_s)).family() != u"Selawik"_s)
        fail(u"warning: Selawik not loaded, set MICROSET_FONT_DIR"_s);

    FakeHome home;
    AppModel model;
    FavoritesModel favorites(QDBusConnection(u"launcher-shot"_s), &model);
    FakeActivator activator;
    FakePower power;
    Launcher launcher(&home, &model, &favorites, &activator, &power, nullptr, nullptr, 1, 80);
    FakeIdle idle;
    launcher.setIdle(&idle);

    QTemporaryDir iconDir;
    QVariantList rows = entries(12, u"org.example.App"_s, iconDir.path());
    QStringList favoriteIds = ids(rows, 6);
    bool allApps = false;
    bool jump = false;
    bool sheet = false;
    bool sheetApps = false;
    bool deep = false;
    bool powerPage = false;
    bool toast = false;
    bool confirm = false;
    if (state == "home-empty"_L1) {
        favoriteIds.clear();
    } else if (state == "home-running"_L1) {
        home.publish(true, windows(3), 1);
    } else if (state == "home-battery-low"_L1) {
        launcher.setBattery(true, 5);
    } else if (state == "home-long"_L1) {
        rows = namedEntries({LongName, u"Bluetooth Manager"_s,
                             u"An Extremely Long Application Name That Runs Well Past The Row"_s,
                             u"Archive Tool"_s, u"Calculator"_s, u"Zed"_s}, iconDir.path());
        favoriteIds = ids(rows, 6);
    } else if (state == "allapps"_L1 || state == "sheet"_L1 || state == "jump"_L1) {
        allApps = state != "sheet"_L1;
        jump = state == "jump"_L1;
        sheet = state == "sheet"_L1;
    } else if (state == "jump-letters"_L1 || state == "jump-end"_L1) {
        rows = namedEntries({u"Archive Tool"_s, u"Bluetooth Manager"_s, u"Calculator"_s, u"Files"_s, u"Music"_s,
                             u"Terminal"_s, u"Zed"_s, u"かな入力"_s, u"漢字辞典"_s}, iconDir.path());
        favoriteIds = ids(rows, 2);
        allApps = true;
        jump = true;
    } else if (state == "allapps-zero"_L1) {
        rows.clear();
        favoriteIds.clear();
        allApps = true;
    } else if (state == "allapps-many"_L1) {
        rows = entries(40, u"org.example.App"_s, iconDir.path());
        favoriteIds = ids(rows, 12);
        allApps = true;
    } else if (state == "allapps-long"_L1) {
        rows = namedEntries({u"Bluetooth Manager"_s, LongName,
                             u"An Extremely Long Application Name That Runs Well Past The Row"_s,
                             u"Zed"_s, u"Archive Tool"_s, u"Calculator"_s}, iconDir.path());
        favoriteIds = ids(rows, 2);
        allApps = true;
    } else if (state == "allapps-deep"_L1) {
        rows = entries(40, u"org.example.App"_s, iconDir.path());
        favoriteIds = ids(rows, 30);
        allApps = true;
        deep = true;
    } else if (state == "sheet-allapps"_L1) {
        allApps = true;
        sheetApps = true;
    } else if (state == "power-home"_L1) {
        powerPage = true;
    } else if (state == "power-allapps"_L1) {
        allApps = true;
        powerPage = true;
    } else if (state == "toast"_L1) {
        toast = true;
    } else if (state == "confirm"_L1) {
        confirm = true;
    }
    if (state != "home-running"_L1)
        home.publish(true, {}, 1);
    model.setEntriesForTest(rows);
    favorites.setIdsForTest(favoriteIds);

    QQmlApplicationEngine engine;
    bool warned = false;
    QObject::connect(&engine, &QQmlApplicationEngine::warnings, &engine, [&warned](const QList<QQmlError> &list) {
        for (const QQmlError &error : list)
            fail(error.toString());
        warned = true;
    });
    engine.setInitialProperties({
        {u"launcher"_s, QVariant::fromValue(static_cast<QObject *>(&launcher))},
        {u"shotAccentOff"_s, parser.isSet(accentOffOption)},
    });
    engine.addImageProvider(u"microset-appicon"_s, new AppIconProvider());
    engine.loadFromModule("MicroSetLauncher", "Home");
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
    if (window == nullptr) {
        fail(u"MicroSetLauncher.Home did not produce a window"_s);
        return 1;
    }
    window->resize(size.captured(1).toInt(), size.captured(2).toInt());
    window->show();

    if (allApps)
        launcher.openAllApps();
    QQuickItem *root = window->contentItem();
    const auto find = [root](const QString &name) {
        QList<QQuickItem *> pending{root};
        while (!pending.isEmpty()) {
            QQuickItem *item = pending.takeFirst();
            if (item->objectName() == name)
                return item;
            pending += item->childItems();
        }
        return static_cast<QQuickItem *>(nullptr);
    };

    launcher.focusGridAt(0);
    if (sheet)
        QMetaObject::invokeMethod(&launcher, "sheetRequested");
    if (jump) {
        QQuickItem *apps = find(u"apps"_s);
        if (apps != nullptr)
            QMetaObject::invokeMethod(apps, "openJump");
        QQuickItem *grid = find(u"jump"_s);
        if (grid != nullptr && state == "jump-end"_L1)
            QMetaObject::invokeMethod(grid, "open", Q_ARG(QVariant, grid->property("available")), Q_ARG(QString, u"漢"_s));
    }
    if (sheetApps) {
        bool handled = false;
        QQuickItem *apps = find(u"apps"_s);
        if (apps != nullptr)
            QMetaObject::invokeMethod(apps, "handleAction", Q_RETURN_ARG(bool, handled),
                                      Q_ARG(QString, u"x"_s), Q_ARG(bool, false));
    }
    if (deep)
        launcher.toggleFavorite(rows.at(20).toMap().value(u"id"_s).toString());
    if (powerPage)
        launcher.ShowPower();
    if (toast)
        QMetaObject::invokeMethod(&launcher, "uninstallFailed", Q_ARG(QString, u"Bluetooth Manager"_s),
                                  Q_ARG(QString, u"not installed"_s));
    if (confirm)
        QMetaObject::invokeMethod(window, "askUninstall", Q_ARG(QString, u"org.example.App.desktop"_s),
                                  Q_ARG(QString, u"Bluetooth Manager"_s));

    const QString out = parser.value(screenshotOption);
    QQuickItem *deepList = deep ? find(u"applist"_s) : nullptr;
    auto *settle = new QTimer(window);
    settle->setSingleShot(true);
    settle->setInterval(600);
    QObject::connect(settle, &QTimer::timeout, window, [window, out, &warned, deepList] {
        if (warned) {
            fail(u"QML warnings during the render"_s);
            QCoreApplication::exit(1);
            return;
        }
        if (deepList != nullptr)
            QMetaObject::invokeMethod(deepList, "snapTo", Q_ARG(int, 20));
        const QImage image = window->grabWindow();
        QDir().mkpath(QFileInfo(out).absolutePath());
        if (image.isNull() || !image.save(out, "PNG")) {
            fail(u"cannot write %1"_s.arg(out));
            QCoreApplication::exit(1);
            return;
        }
        QCoreApplication::exit(0);
    });
    settle->start();
    QTimer::singleShot(frameTimeoutMs, window, [&warned] {
        fail(warned ? u"QML warnings during the render"_s : u"no frame within %1 ms"_s.arg(frameTimeoutMs));
        QCoreApplication::exit(1);
    });
    return QCoreApplication::exec();
}

#include "launchershot.moc"
