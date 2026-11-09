#include "fakebackend.h"
#include "storeprocess.h"
#include "storeworker.h"

#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDir>
#include <QElapsedTimer>
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
#include <QtQml/qqmlextensionplugin.h>

#include <functional>
#include <memory>

Q_IMPORT_QML_PLUGIN(MicroSetPlugin)
Q_IMPORT_QML_PLUGIN(MicroSetStorePlugin)

using namespace Qt::StringLiterals;

namespace {

constexpr int loadTimeoutMs = 8000;

const QString LongName =
    u"The Legend of Heroes: Trails in the Sky the 1st Chapter Deluxe Edition with Every Bonus"_s;
const QString LongSummary =
    u"A long running role playing game with a story that runs well past the row and keeps going for the summary line"_s;

void fail(const QString &message)
{
    QTextStream(stderr) << "microset-storeshot: " << message << '\n';
}

const QStringList &states()
{
    static const QStringList names{
        u"list"_s, u"list-installed"_s, u"list-many"_s, u"list-noicon"_s, u"detail"_s, u"detail-installed"_s,
        u"detail-installing"_s, u"detail-queued"_s, u"detail-long"_s, u"search"_s, u"search-empty"_s, u"offline"_s,
        u"confirm"_s, u"toast"_s,
    };
    return names;
}

bool waitFor(const std::function<bool()> &ready, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!ready() && timer.elapsed() < timeoutMs)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return ready();
}

StoreEntry entryFor(const QString &id, const QString &name, const QString &summary, const QString &developer)
{
    StoreEntry entry;
    entry.id = id;
    entry.name = name;
    entry.summary = summary;
    entry.developer = developer;
    return entry;
}

QString icon(const QString &dir, int index, int size)
{
    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor::fromHsv((index * 47) % 360, 170, 205));
    const int inset = size / 16;
    painter.drawRoundedRect(inset, inset, size - 2 * inset, size - 2 * inset, size / 5, size / 5);
    painter.setPen(Qt::white);
    painter.setFont(QFont(u"Selawik"_s, size / 3, QFont::Bold));
    painter.drawText(image.rect(), Qt::AlignCenter, QString::number(index + 1));
    painter.end();
    const QString path = dir + u"/icon%1-%2.png"_s.arg(index).arg(size);
    image.save(path, "PNG");
    return path;
}

}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    const QCommandLineOption screenshotOption(u"screenshot"_s, u"Render offscreen to <png> and exit."_s, u"png"_s);
    const QCommandLineOption sizeOption(u"size"_s, u"Window size."_s, u"WxH"_s, u"480x320"_s);
    const QCommandLineOption stateOption(u"state"_s, u"Fixture state."_s, u"name"_s, u"list"_s);
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
    if (QFontInfo(QFont(u"Selawik"_s)).family() != u"Selawik"_s)
        fail(u"warning: Selawik not loaded, set MICROSET_FONT_DIR"_s);

    QTemporaryDir iconDir;
    QList<StoreEntry> entries;
    const QStringList names{u"Calculator"_s, u"Chess"_s, u"Files"_s, u"Image Viewer"_s, u"Maps"_s, u"Music"_s,
                            u"Notes"_s, u"Terminal"_s};
    for (int i = 0; i < names.size(); ++i) {
        StoreEntry entry = entryFor(u"org.example.App%1"_s.arg(i), names.at(i), names.at(i) + u" for the desktop"_s,
                                    u"Example Developer"_s);
        entry.icon = icon(iconDir.path(), i, 64);
        entry.iconLarge = icon(iconDir.path(), i, 128);
        entries << entry;
    }
    StoreEntry longEntry = entryFor(u"org.example.Long"_s, LongName, LongSummary,
                                    u"An Extremely Long Developer Name That Runs Past The Row"_s);
    longEntry.icon = icon(iconDir.path(), 8, 64);
    longEntry.iconLarge = icon(iconDir.path(), 8, 128);
    longEntry.installedSize = 2840000000;

    QList<StoreEntry> installed{entries.at(1), entries.at(3)};
    installed[0].installedSize = 12800000;
    installed[1].installedSize = 9200000;
    if (state == "list-many"_L1) {
        entries.clear();
        for (int i = 0; i < 40; ++i) {
            StoreEntry entry = entryFor(u"org.example.Many%1"_s.arg(i), u"app %1"_s.arg(i),
                                        u"the %1th app in the catalog"_s.arg(i), u"Example Developer"_s);
            entry.icon = icon(iconDir.path(), i % 9, 64);
            entry.iconLarge = icon(iconDir.path(), i % 9, 128);
            entries << entry;
        }
    }
    if (state == "list-installed"_L1)
        installed = entries;
    if (state == "list-noicon"_L1) {
        for (StoreEntry &entry : entries) {
            entry.icon.clear();
            entry.iconLarge.clear();
        }
        for (StoreEntry &entry : installed) {
            entry.icon.clear();
            entry.iconLarge.clear();
        }
    }
    if (state == "detail-long"_L1)
        entries = {longEntry};
    if (state == "confirm"_L1 || state == "detail-installed"_L1) {
        StoreEntry first = entries.at(0);
        first.installedSize = 12800000;
        installed = {first};
    }
    if (state == "detail-installing"_L1 || state == "detail-queued"_L1 || state == "detail"_L1)
        installed.clear();

    auto backend = std::make_unique<FakeBackend>();
    FakeBackend *fake = backend.get();
    fake->setEntries(entries);
    fake->setInstalled(installed);
    if (state == "offline"_L1) {
        fake->setEntries({});
        fake->setInstalled({});
        fake->setSyncFails(true, u"offline"_s);
    }
    StoreWorker worker(std::move(backend));
    worker.start();
    StoreProcess store(&worker, QDBusConnection(u"store-shot"_s));

    QQmlApplicationEngine engine;
    bool warned = false;
    QObject::connect(&engine, &QQmlApplicationEngine::warnings, &engine, [&warned](const QList<QQmlError> &list) {
        for (const QQmlError &error : list)
            fail(error.toString());
        warned = true;
    });
    engine.setInitialProperties({
        {u"store"_s, QVariant::fromValue(static_cast<QObject *>(&store))},
        {u"animate"_s, false},
    });
    engine.loadFromModule("MicroSetStore", "Store");
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
    if (window == nullptr) {
        fail(u"MicroSetStore.Store did not produce a window"_s);
        return 1;
    }
    window->resize(size.captured(1).toInt(), size.captured(2).toInt());
    window->show();
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

    store.show();
    const bool empty = state == "offline"_L1;
    if (!waitFor([&store, empty] { return empty || store.count() > 0; }, loadTimeoutMs)) {
        fail(u"the catalog did not load"_s);
        return 1;
    }

    if (state == "search"_L1 || state == "search-empty"_L1) {
        store.setSearch(state == "search"_L1 ? u"a"_s : u"zzz"_s);
        if (state == "search"_L1) {
            QQuickItem *keyboard = find(u"keyboard"_s);
            const QVariantMap request{{u"title"_s, u"search"_s}, {u"text"_s, store.search()}, {u"kind"_s, u"text"_s}};
            if (keyboard != nullptr) {
                QMetaObject::invokeMethod(keyboard, "open", Q_ARG(QVariant, QVariant::fromValue(request)));
                QMetaObject::invokeMethod(keyboard, "complete");
            }
        }
    }

    const QString detailId = state == "detail-long"_L1 ? longEntry.id : entries.at(0).id;
    if (state.startsWith(u"detail"_s) || state == "confirm"_L1) {
        store.open(detailId);
        QMetaObject::invokeMethod(window, "openDetail", Q_ARG(QString, detailId));
        waitFor([&store] { return !store.current().isEmpty(); }, loadTimeoutMs);
    }

    if (state == "detail-installing"_L1) {
        fake->setInstallSteps(4);
        fake->setInstallDelayMs(1200);
        store.activate();
        waitFor([&store] { return store.percent() >= 50; }, loadTimeoutMs);
    }

    if (state == "detail-queued"_L1) {
        fake->setInstallSteps(4);
        fake->setInstallDelayMs(1200);
        store.open(entries.at(0).id);
        store.activate();
        waitFor([&store] { return store.busy(); }, loadTimeoutMs);
        store.open(entries.at(1).id);
        QMetaObject::invokeMethod(window, "openDetail", Q_ARG(QString, entries.at(1).id));
        store.activate();
        waitFor([&store] { return store.queued(); }, loadTimeoutMs);
    }

    if (state == "confirm"_L1) {
        QMetaObject::invokeMethod(window, "askUninstall", Q_ARG(QString, store.current().value(u"name"_s).toString()));
        QQuickItem *confirm = find(u"confirm"_s);
        if (confirm != nullptr)
            QMetaObject::invokeMethod(confirm, "complete");
    }

    if (state == "toast"_L1) {
        QQuickItem *toast = find(u"toast"_s);
        if (toast != nullptr) {
            QMetaObject::invokeMethod(toast, "show", Q_ARG(QString, u"Calculator"_s), Q_ARG(QString, u"installed"_s));
            QMetaObject::invokeMethod(toast, "complete");
        }
    }

    QElapsedTimer settle;
    settle.start();
    while (settle.elapsed() < 400)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);

    if (warned) {
        fail(u"QML warnings during the render"_s);
        return 1;
    }
    const QString out = parser.value(screenshotOption);
    QDir().mkpath(QFileInfo(out).absolutePath());
    const QImage image = window->grabWindow();
    if (image.isNull() || !image.save(out, "PNG")) {
        fail(u"cannot write %1"_s.arg(out));
        return 1;
    }
    return 0;
}
