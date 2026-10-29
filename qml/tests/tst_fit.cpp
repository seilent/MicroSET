#include "fakemicrosetd.h"
#include "settingsmodel.h"

#include <QFont>
#include <QFontInfo>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QJsonDocument>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>
#include <QtQml/qqmlextensionplugin.h>

#include <memory>

Q_IMPORT_QML_PLUGIN(MicroSetPlugin)
Q_IMPORT_QML_PLUGIN(MicroSetPageTestPlugin)

using namespace Qt::StringLiterals;

class FitTest : public QObject
{
    Q_OBJECT

    struct Count
    {
        int words = 0;
        int titles = 0;
        int rows = 0;
        int leads = 0;
        QStringList misses;
    };

    static QObject *viewOf(QQmlApplicationEngine &engine, SettingsModel &model)
    {
        engine.setInitialProperties({
            {u"model"_s, QVariant::fromValue(static_cast<QObject *>(&model))},
            {u"screenshot"_s, true},
            {u"shotState"_s, u"none"_s},
            {u"shotWidth"_s, 640},
            {u"shotHeight"_s, 960},
        });
        engine.loadFromModule("MicroSetPageTest", "PageRoot");
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
        return window ? window->findChild<QObject *>(u"settingsView"_s) : nullptr;
    }

    static void check(QObject *view, SettingsModel &model, const QString &group, Count &count)
    {
        QVERIFY(QMetaObject::invokeMethod(view, "open", Q_ARG(QString, group)));
        QObject *page = qvariant_cast<QObject *>(qvariant_cast<QObject *>(view->property("stack"))->property("currentItem"));
        auto *sheet = qvariant_cast<QQuickItem *>(page->property("sheet"));
        QVERIFY(sheet);
        for (const QVariant &item : model.rows(group, model.revision())) {
            const QVariantMap row = item.toMap();
            if (row.value(u"pick"_s).toString() != "sheet"_L1)
                continue;
            QVERIFY(QMetaObject::invokeMethod(page, "openValue", Q_ARG(QString, row.value(u"key"_s).toString())));
            QVERIFY(sheet->property("shown").toBool());
            auto *panel = qvariant_cast<QQuickItem *>(sheet->property("panel"));
            QTRY_COMPARE(panel->width(), 640.0);
            const qreal room = panel->width() - 2 * sheet->property("edge").toReal();
            const QStringList words = sheet->property("items").toStringList();
            int texts = 0;
            for (QQuickItem *delegate : qvariant_cast<QQuickItem *>(sheet->property("list"))->childItems()) {
                for (QQuickItem *text : delegate->childItems()) {
                    if (text->implicitWidth() > room)
                        count.misses << u"%1: %2 %3 > %4"_s.arg(row.value(u"key"_s).toString(), text->property("text").toString(),
                                                                QString::number(text->implicitWidth()), QString::number(room));
                    ++texts;
                }
            }
            QCOMPARE(texts, words.size());
            for (QQuickItem *child : panel->childItems()) {
                if (child->property("text").toString() != sheet->property("title").toString())
                    continue;
                if (child->implicitWidth() > room)
                    count.misses << u"%1: title %2 %3 > %4"_s.arg(row.value(u"key"_s).toString(), child->property("text").toString(),
                                                                  QString::number(child->implicitWidth()), QString::number(room));
                ++count.titles;
            }
            count.words += texts;
            auto *lead = qvariant_cast<QQuickItem *>(sheet->property("leadItem"));
            for (int i = 0; i < words.size(); ++i) {
                sheet->setProperty("focusIndex", i);
                if (sheet->property("lead").toString().isEmpty())
                    continue;
                if (lead->implicitWidth() > room)
                    count.misses << u"%1: lead %2 %3 > %4"_s.arg(row.value(u"key"_s).toString(), lead->property("text").toString(),
                                                                 QString::number(lead->implicitWidth()), QString::number(room));
                ++count.leads;
            }
            QVERIFY(QMetaObject::invokeMethod(page, "cancelValue"));
        }
        QVERIFY(QMetaObject::invokeMethod(page, "back"));
    }

    static void checkRows(QObject *view, SettingsModel &model, const QString &group, Count &count,
                          const QMap<QString, QStringList> &extra = {})
    {
        QVERIFY(QMetaObject::invokeMethod(view, "open", Q_ARG(QString, group)));
        QObject *page = qvariant_cast<QObject *>(qvariant_cast<QObject *>(view->property("stack"))->property("currentItem"));
        auto *repeater = qvariant_cast<QObject *>(page->property("repeater"));
        QVERIFY(repeater);
        const int rows = repeater->property("count").toInt();
        QVERIFY(rows > 0);
        for (int i = 0; i < rows; ++i) {
            QQuickItem *item = nullptr;
            QVERIFY(QMetaObject::invokeMethod(repeater, "itemAt", Q_RETURN_ARG(QQuickItem *, item), Q_ARG(int, i)));
            QVERIFY(item);
            const QVariantMap row = item->property("modelData").toMap();
            const QString key = row.value(u"key"_s).toString();
            const QString option = key.mid(group.size() + 1);
            const bool game = key.startsWith(group + u'.')
                && (option.startsWith("fex."_L1) || option.startsWith("lsfg."_L1) || option == "wine.sync"_L1);
            if (!game && !extra.contains(u"*"_s))
                continue;
            auto *title = qvariant_cast<QQuickItem *>(item->property("titleItem"));
            auto *value = qvariant_cast<QQuickItem *>(item->property("valueItem"));
            QTRY_COMPARE(item->width(), 640.0);
            QStringList values{row.value(u"value"_s).toString()};
            values << model.sheet(key).value(u"words"_s).toStringList() << extra.value(key);
            for (const QString &text : std::as_const(values)) {
                item->setProperty("shownValue", text);
                const qreal room = value->property("room").toReal();
                if (title->width() < title->implicitWidth() || value->implicitWidth() > room)
                    count.misses << u"%1: %2 %3 beside %4 %5 > %6"_s.arg(key, text, QString::number(value->implicitWidth()),
                                                                         title->property("text").toString(),
                                                                         QString::number(title->implicitWidth()),
                                                                         QString::number(room));
                ++count.rows;
            }
        }
        QVERIFY(QMetaObject::invokeMethod(page, "back"));
    }

private slots:
    void sheetWordsFitAt640()
    {
        QVERIFY(qEnvironmentVariableIsSet("MICROSET_FONT_DIR"));
        SettingsModel model;
        model.setFixture(FakeMicroSetd::describe({FakeMicroSetd::shipped(), FakeMicroSetd::fixture(u"50-microdap.json"_s),
                                                  FakeMicroSetd::fixture(u"50-microemu.json"_s)}),
                         {{u"power.gpu_level"_s, u"manual"_s}, {u"display.auto_rotate"_s, false}},
                         {{u"rgb.enabled"_s, true}});
        QQmlApplicationEngine engine;
        QObject *view = viewOf(engine, model);
        QVERIFY(view);
        QCOMPARE(QFontInfo(QFont(u"Selawik"_s)).family(), u"Selawik"_s);
        Count count;
        for (const QVariant &group : model.groups(0)) {
            const QString title = group.toMap().value(u"group"_s).toString();
            if (!title.isEmpty())
                check(view, model, title, count);
        }
        QVERIFY(count.words > 200);
        QVERIFY(count.titles > 30);
        QVERIFY(count.leads >= 1);
        QVERIFY2(count.misses.isEmpty(),
                 qPrintable(u"sheet words, titles or leads wider than the panel at 640 wide:\n"_s + count.misses.join(u'\n')));
    }

    void lsfgFitsAt640()
    {
        QVERIFY(qEnvironmentVariableIsSet("MICROSET_FONT_DIR"));
        QTemporaryDir steam;
        qputenv("MICROSET_STEAM_DIR", steam.path().toUtf8());
        FakeMicroSetd::writeSteam(steam.path(), {{u"292120"_s, u"FINAL FANTASY XIII"_s}, {u"2238900"_s, u"SO2R"_s}});
        const QStringList files{FakeMicroSetd::shipped()};
        const QVariantMap stored{{u"app.77.lsfg.state"_s, u"on"_s}, {u"app.2238900.lsfg.multiplier"_s, u"3"_s}};
        SettingsModel model;
        model.setFixture(FakeMicroSetd::describe(files), stored, {});
        QMap<QString, QJsonObject> effective;
        for (const QString &appid : {u"292120"_s, u"2238900"_s, u"77"_s})
            effective.insert(appid, FakeMicroSetd::effective(files, stored, FakeMicroSetd::Profiles, appid));
        model.setProtonFixture(effective);
        model.lsfg()->setFixture(QJsonDocument::fromJson(R"({"owned": "yes", "install": "none"})").object(), true, u"steam"_s);
        QQmlApplicationEngine engine;
        QObject *view = viewOf(engine, model);
        QVERIFY(view);
        const QString lsfg = u"Frame generation"_s;
        Count count;
        check(view, model, lsfg, count);
        const int words = count.words;
        model.toggle(u"lsfg-games"_s);
        const QMap<QString, QStringList> extra{
            {u"*"_s, {}},
            {u"lsfg-status"_s,
             {u"installed"_s, u"installing"_s, u"install sent"_s, u"no Steam client"_s, u"game running"_s,
              u"not installed"_s, u"on microSD"_s, u"ownership unknown"_s, u"old layer in home"_s,
              u"switch to the lsfg-vk beta"_s}},
            {u"lsfg-games"_s, {u"99 on"_s, u"off"_s}},
            {u"lsfg-game-292120"_s, {u"on"_s, u"off"_s, u"not installed"_s}},
            {u"lsfg.enabled"_s, {u"on"_s, u"off"_s}},
        };
        checkRows(view, model, lsfg, count, extra);
        for (const QString &appid : {u"292120"_s, u"2238900"_s}) {
            check(view, model, u"app."_s + appid, count);
            checkRows(view, model, u"app."_s + appid, count);
        }
        model.lsfg()->setFixture({}, true, u"steam"_s);
        QVariantMap off = stored;
        off.insert(u"lsfg.enabled"_s, false);
        model.setFixture(FakeMicroSetd::describe(files), off, {});
        checkRows(view, model, lsfg, count, extra);
        QVERIFY(words >= 12);
        QVERIFY(count.rows >= 60);
        QVERIFY2(count.misses.isEmpty(),
                 qPrintable(u"frame generation words, titles or row values wider than their room at 640 wide:\n"_s
                            + count.misses.join(u'\n')));
        qunsetenv("MICROSET_STEAM_DIR");
    }

    void gameSheetsFitAt640_data()
    {
        QTest::addColumn<QVariantMap>("stored");
        QTest::newRow("defaults and profile") << QVariantMap{};
        QTest::newRow("game values") << QVariantMap{{u"app.2238900.fex.preset"_s, u"performance"_s},
                                                    {u"app.292120.fex.tso"_s, u"on"_s},
                                                    {u"app.292120.wine.sync"_s, u"ntsync"_s}};
        QTest::newRow("global values") << QVariantMap{{u"fex.preset"_s, u"compatibility"_s}, {u"wine.sync"_s, u"fsync"_s}};
        QTest::newRow("profile off") << QVariantMap{{u"app.292120.profile"_s, u"off"_s}, {u"proton.profiles"_s, u"off"_s}};
    }

    void gameSheetsFitAt640()
    {
        QFETCH(QVariantMap, stored);
        QVERIFY(qEnvironmentVariableIsSet("MICROSET_FONT_DIR"));
        const QStringList files{FakeMicroSetd::shipped()};
        const QStringList games{u"292120"_s, u"2238900"_s};
        SettingsModel model;
        model.setFixture(FakeMicroSetd::describe(files), stored, {});
        QMap<QString, QJsonObject> effective;
        for (const QString &appid : games)
            effective.insert(appid, FakeMicroSetd::effective(files, stored, FakeMicroSetd::Profiles, appid));
        model.setProtonFixture(effective);
        QQmlApplicationEngine engine;
        QObject *view = viewOf(engine, model);
        QVERIFY(view);
        Count count;
        for (const QString &appid : games) {
            check(view, model, u"app."_s + appid, count);
            checkRows(view, model, u"app."_s + appid, count);
        }
        QVERIFY(count.words >= 2 * 37);
        QVERIFY(count.titles >= 2 * 12);
        QVERIFY(count.rows >= 2 * 52);
        QVERIFY(count.leads >= 2 * 9);
        QVERIFY2(count.misses.isEmpty(),
                 qPrintable(u"game sheet words, titles, leads or row values wider than their room at 640 wide:\n"_s
                            + count.misses.join(u'\n')));
    }
};

QTEST_MAIN(FitTest)
#include "tst_fit.moc"
