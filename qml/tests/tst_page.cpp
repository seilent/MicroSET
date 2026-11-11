#include "fakemicrosetd.h"
#include "harness.h"
#include "settingsmodel.h"

#include <QColor>
#include <QFile>
#include <QFont>
#include <QHash>
#include <QJSValue>
#include <QJsonDocument>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QtQml/qqmlextensionplugin.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

Q_IMPORT_QML_PLUGIN(MicroSetPlugin)
Q_IMPORT_QML_PLUGIN(MicroSetPageTestPlugin)

using namespace Qt::StringLiterals;

class PageTest : public QObject
{
    Q_OBJECT

    std::unique_ptr<Harness> h;
    std::unique_ptr<FakeMicroSetd> fake;
    std::unique_ptr<SettingsModel> model;
    std::unique_ptr<QQmlApplicationEngine> engine;
    QQuickWindow *window = nullptr;
    QObject *view = nullptr;

    void key(Qt::Key k)
    {
        QTest::keyPress(window, k);
        QTest::keyRelease(window, k);
    }

    QObject *page() const
    {
        return qvariant_cast<QObject *>(qvariant_cast<QObject *>(view->property("stack"))->property("currentItem"));
    }

    QObject *toast() const
    {
        return qvariant_cast<QObject *>(view->property("toast"));
    }

    QVariantMap row(const QString &key) const
    {
        for (const QVariant &r : page()->property("rows").toList())
            if (r.toMap().value(u"key"_s).toString() == key)
                return r.toMap();
        return {};
    }

    QString value(const QString &key) const
    {
        return row(key).value(u"value"_s).toString();
    }

    void open(const QString &group)
    {
        QVERIFY(QMetaObject::invokeMethod(view, "open", Q_ARG(QString, group)));
        QCOMPARE(page()->property("group").toString(), group);
    }

    void focus(const QString &key)
    {
        int index = -1;
        QVERIFY(QMetaObject::invokeMethod(page(), "indexOfKey", Q_RETURN_ARG(int, index), Q_ARG(QString, key)));
        QVERIFY(index >= 0);
        page()->setProperty("focusIndex", index);
    }

    void reachable(const QString &k)
    {
        int index = -1;
        QVERIFY(QMetaObject::invokeMethod(page(), "indexOfKey", Q_RETURN_ARG(int, index), Q_ARG(QString, k)));
        QTRY_COMPARE(page()->property("focusIndex").toInt(), index);
        QVERIFY(QMetaObject::invokeMethod(page(), "completeScroll"));
        qreal top = 0;
        qreal height = 0;
        QVERIFY(QMetaObject::invokeMethod(page(), "rowTopOf", Q_RETURN_ARG(qreal, top), Q_ARG(int, index)));
        QVERIFY(QMetaObject::invokeMethod(page(), "rowHeightOf", Q_RETURN_ARG(qreal, height), Q_ARG(QVariant, QVariant(row(k)))));
        auto *flick = qvariant_cast<QObject *>(page()->property("flick"));
        QVERIFY(flick);
        const qreal y = flick->property("contentY").toReal();
        QVERIFY2(top >= y && top + height <= y + flick->property("height").toReal(), qPrintable(k));
    }

    QObject *sheet() const
    {
        return qvariant_cast<QObject *>(page()->property("sheet"));
    }

    bool sheetShown() const
    {
        return sheet()->property("shown").toBool();
    }

    int sheetFocus() const
    {
        return sheet()->property("focusIndex").toInt();
    }

    QObject *adjust() const
    {
        return qvariant_cast<QObject *>(page()->property("adjust"));
    }

    QQuickItem *adjustColumn(int index) const
    {
        QQuickItem *column = nullptr;
        QMetaObject::invokeMethod(qvariant_cast<QObject *>(adjust()->property("columns")), "itemAt",
                                  Q_RETURN_ARG(QQuickItem *, column), Q_ARG(int, index));
        return column;
    }

    QString adjustNumber(int index) const
    {
        return qvariant_cast<QQuickItem *>(adjustColumn(index)->property("numberItem"))->property("text").toString();
    }

    bool adjustLit(int index, const char *sign) const
    {
        return qvariant_cast<QQuickItem *>(adjustColumn(index)->property(sign))->opacity() == 1.0;
    }

    void held(const QString &name)
    {
        bool handled = false;
        QVERIFY(QMetaObject::invokeMethod(page(), "handleAction", Q_RETURN_ARG(bool, handled), Q_ARG(QString, name),
                                          Q_ARG(bool, true)));
    }

    void writeAtomic(const QString &name, const QByteArray &data) const
    {
        h->writeFile(name + u".tmp"_s, data);
        QCOMPARE(std::rename(QFile::encodeName(h->path(name + u".tmp"_s)).constData(), QFile::encodeName(h->path(name)).constData()),
                 0);
    }

    void writeLsfg(const QByteArray &state) const
    {
        writeAtomic(u"lsfg/state.json"_s, state);
    }

    void openLsfg()
    {
        QTRY_VERIFY(!model->rows(Lsfg, model->revision()).isEmpty());
        open(Lsfg);
    }

    static inline const QString Lsfg = u"Frame generation"_s;

    void ledsOn()
    {
        fake->setStored({{u"rgb.enabled"_s, true}, {u"rgb.saturation"_s, 60u}});
        fake->setValues({{u"rgb.enabled"_s, true}, {u"rgb.saturation"_s, 60u}, {u"rgb.brightness"_s, 50u}});
        model->load();
        QTRY_VERIFY(!model->rows(u"stick leds"_s, model->revision()).at(2).toMap().value(u"readOnly"_s).toBool());
    }

    void startPage()
    {
        engine = std::make_unique<QQmlApplicationEngine>();
        engine->setInitialProperties({
            {u"model"_s, QVariant::fromValue(static_cast<QObject *>(model.get()))},
            {u"screenshot"_s, true},
            {u"shotState"_s, u"none"_s},
        });
        engine->loadFromModule("MicroSetPageTest", "PageRoot");
        window = qobject_cast<QQuickWindow *>(engine->rootObjects().value(0));
        QVERIFY(window);
        view = window->findChild<QObject *>(u"settingsView"_s);
        QVERIFY(view);
    }

    QQuickItem *rowItem(const QString &key) const
    {
        int index = -1;
        QMetaObject::invokeMethod(page(), "indexOfKey", Q_RETURN_ARG(int, index), Q_ARG(QString, key));
        QQuickItem *item = nullptr;
        QMetaObject::invokeMethod(qvariant_cast<QObject *>(page()->property("repeater")), "itemAt",
                                  Q_RETURN_ARG(QQuickItem *, item), Q_ARG(int, index));
        return item;
    }

    static QPoint centerOf(QQuickItem *item)
    {
        return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
    }

    static QQuickItem *sheetItem(QObject *sheet, int index)
    {
        for (QQuickItem *row : qvariant_cast<QQuickItem *>(sheet->property("list"))->childItems())
            if (row->property("touchIndex").isValid() && row->property("touchIndex").toInt() == index)
                return row;
        return nullptr;
    }

    static QPoint settledPoint(QObject *sheet, int index)
    {
        auto *root = qobject_cast<QQuickItem *>(sheet);
        auto *panel = qvariant_cast<QQuickItem *>(sheet->property("panel"));
        auto *list = qvariant_cast<QQuickItem *>(sheet->property("window"));
        const qreal y = root->height() - panel->height() + list->y()
            + (index + 0.5) * sheet->property("pitch").toReal() - list->property("contentY").toReal();
        return root->mapToScene(QPointF(root->width() / 2, y)).toPoint();
    }

    static void rising(QObject *sheet)
    {
        for (int i = 0; i < 2000 && !sheet->property("animating").toBool(); ++i)
            QTest::qWait(1);
        QVERIFY(sheet->property("animating").toBool());
    }

    static bool laidOutNow(QObject *sheet)
    {
        const qreal pitch = sheet->property("pitch").toReal();
        for (QQuickItem *row : qvariant_cast<QQuickItem *>(sheet->property("list"))->childItems())
            if (row->property("touchIndex").isValid() && qAbs(row->y() - row->property("touchIndex").toInt() * pitch) > 0.5)
                return false;
        return true;
    }

    static void laidOut(QObject *sheet)
    {
        for (int i = 0; i < 2000 && !laidOutNow(sheet); ++i)
            QTest::qWait(1);
        QVERIFY(laidOutNow(sheet));
    }

    void tap(const QPoint &at)
    {
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, at);
    }

    void tapRow(const QString &key)
    {
        QQuickItem *item = rowItem(key);
        QVERIFY2(item, qPrintable(key));
        tap(centerOf(item));
    }

    void show(const QString &key, const QString &focused)
    {
        focus(key);
        QVERIFY(QMetaObject::invokeMethod(page(), "keepFocusVisible"));
        focus(focused);
    }

    QPoint headerPoint() const
    {
        auto *header = qvariant_cast<QQuickItem *>(page()->property("header"));
        auto *label = qvariant_cast<QQuickItem *>(header->property("labelItem"));
        const QRectF both = header->mapRectToScene(QRectF(0, 0, header->width(), header->height()))
                                .intersected(label->mapRectToScene(QRectF(0, 0, label->width(), label->height())));
        return both.center().toPoint();
    }

    void drag(const QPoint &from, const QPoint &by, int steps)
    {
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, from);
        for (int i = 1; i <= steps; ++i)
            QTest::mouseMove(window, from + by * i / steps, 8);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, from + by);
    }

    static qreal alphaOf(QQuickItem *text)
    {
        return text->property("color").value<QColor>().alphaF();
    }

    static bool near(qreal a, qreal b)
    {
        return qAbs(a - b) < 0.001;
    }

    qreal titleAlpha(const QString &key) const
    {
        return alphaOf(qvariant_cast<QQuickItem *>(rowItem(key)->property("titleItem")));
    }

    QQuickItem *cellTitle(int index) const
    {
        QQuickItem *cell = nullptr;
        QMetaObject::invokeMethod(qvariant_cast<QObject *>(rowSheet()->property("cells")), "itemAt",
                                  Q_RETURN_ARG(QQuickItem *, cell), Q_ARG(int, index));
        return cell ? qvariant_cast<QQuickItem *>(cell->property("titleItem")) : nullptr;
    }

    QString focusedKey() const
    {
        return page()->property("focusedRow").toMap().value(u"key"_s).toString();
    }

    void atTop(const QString &key)
    {
        QTRY_COMPARE(focusedKey(), key);
        QCOMPARE(page()->property("focusIndex").toInt(), 1);
        QCOMPARE(qvariant_cast<QObject *>(page()->property("flick"))->property("contentY").toReal(), 0.0);
    }

private slots:
    void init()
    {
        QTest::failOnWarning(QRegularExpression(u"TypeError|ReferenceError|Unable to assign|Binding loop"_s));
        h = std::make_unique<Harness>();
        qputenv("MICROSET_LSFG_STATE", h->path(u"lsfg/state.json"_s).toUtf8());
        qputenv("MICROSET_SESSION_CURRENT", h->path(u"run/session-current"_s).toUtf8());
        QVERIFY(h->startBus());
        fake = std::make_unique<FakeMicroSetd>(h->address());
        fake->setSchema(FakeMicroSetd::describe({FakeMicroSetd::shipped()}));
        QVERIFY(fake->start());
        model = std::make_unique<SettingsModel>(h->client());
        model->load();
        QTRY_VERIFY(model->available());
        startPage();
        QVERIFY(window && view);
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(window));
    }

    void cleanup()
    {
        engine.reset();
        model.reset();
        fake.reset();
        h.reset();
    }

    void groupsFocusFirstWhileLoading()
    {
        engine.reset();
        model = std::make_unique<SettingsModel>(h->client());
        model->load();
        startPage();
        QVERIFY(window && view);
        QVERIFY(!model->available());
        QCOMPARE(page()->property("rows").toList().size(), 2);
        QCOMPARE(focusedKey(), u"group-updates"_s);
        QTRY_VERIFY(model->available());
        QTRY_VERIFY(!row(u"group-audio"_s).isEmpty());
        atTop(u"group-audio"_s);
    }

    void androidFocusFirstWhenTheAppComesLate()
    {
        engine.reset();
        model = std::make_unique<SettingsModel>(h->client());
        model->lsfg()->setFixture({}, true, u"android"_s);
        model->load();
        startPage();
        QVERIFY(window && view);
        QTRY_VERIFY(model->available());
        QTRY_VERIFY(!row(u"perf.android_overlay"_s).isEmpty());
        atTop(u"perf.android_overlay"_s);
        model->setAndroidApp(u"com.example.game"_s);
        QTRY_VERIFY(!row(u"android-app"_s).isEmpty());
        atTop(u"android-app"_s);
    }

    void groupsAndBack()
    {
        QCOMPARE(page()->property("title").toString(), u"settings"_s);
        QCOMPARE(page()->property("focusIndex").toInt(), 1);
        key(Qt::Key_Return);
        QCOMPARE(page()->property("group").toString(), u"audio"_s);
        QCOMPARE(page()->property("title").toString(), u"audio"_s);
        key(Qt::Key_Escape);
        QCOMPARE(page()->property("title").toString(), u"settings"_s);
        key(Qt::Key_Down);
        key(Qt::Key_Down);
        key(Qt::Key_Return);
        QCOMPARE(page()->property("title").toString(), u"stick leds"_s);
        QCOMPARE(page()->property("focusIndex").toInt(), 1);
    }

    void gamesOpenAndBack()
    {
        QTemporaryDir steam;
        qputenv("MICROSET_STEAM_DIR", steam.path().toUtf8());
        FakeMicroSetd::writeSteam(steam.path(), {{u"292120"_s, u"FINAL FANTASY XIII"_s}, {u"5"_s, u"<b>bold</b>"_s}});
        fake->setProton({FakeMicroSetd::shipped()}, FakeMicroSetd::Profiles);
        fake->setValues({{u"app.77.fex.tso"_s, u"on"_s}, {u"app.5.fex.tso"_s, u"on"_s}});
        model->load();
        QTRY_COMPARE(model->value(u"app.77.fex.tso"_s, model->revision()).toString(), u"on"_s);
        focus(u"group-games"_s);
        key(Qt::Key_Return);
        QCOMPARE(page()->property("group").toString(), u"games"_s);
        QCOMPARE(page()->property("title").toString(), u"games"_s);
        QTRY_VERIFY(!row(u"game-292120"_s).isEmpty() && !row(u"game-5"_s).isEmpty());
        QCOMPARE(page()->property("focusIndex").toInt(), 1);
        for (int i = 0; i < 4; ++i)
            key(Qt::Key_Down);
        reachable(u"game-292120"_s);
        int bold = -1;
        QVERIFY(QMetaObject::invokeMethod(page(), "indexOfKey", Q_RETURN_ARG(int, bold), Q_ARG(QString, u"game-5"_s)));
        int games = -1;
        QVERIFY(QMetaObject::invokeMethod(page(), "indexOfKey", Q_RETURN_ARG(int, games), Q_ARG(QString, u"label-games"_s)));
        QCOMPARE(games, bold - 1);
        QCOMPARE(row(u"label-games"_s).value(u"kind"_s).toString(), u"label"_s);
        QQuickItem *delegate = nullptr;
        QVERIFY(QMetaObject::invokeMethod(qvariant_cast<QObject *>(page()->property("repeater")), "itemAt",
                                          Q_RETURN_ARG(QQuickItem *, delegate), Q_ARG(int, bold)));
        QVERIFY(delegate);
        QObject *title = qvariant_cast<QObject *>(delegate->property("titleItem"));
        QCOMPARE(title->property("text").toString(), u"<b>bold</b>"_s);
        QCOMPARE(title->property("textFormat").toInt(), int(Qt::PlainText));
        for (int i = 0; i < 4; ++i)
            key(Qt::Key_Down);
        reachable(u"game-77"_s);
        QCOMPARE(row(u"game-77"_s).value(u"value"_s).toString(), u"not installed"_s);
        QVERIFY(row(u"games-add"_s).isEmpty());
        key(Qt::Key_Return);
        QCOMPARE(page()->property("group").toString(), u"app.77"_s);
        QCOMPARE(page()->property("title").toString(), u"77"_s);
        QVERIFY(!sheetShown());
        key(Qt::Key_Escape);
        QCOMPARE(page()->property("group").toString(), u"games"_s);
        focus(u"game-292120"_s);
        key(Qt::Key_Return);
        QCOMPARE(page()->property("group").toString(), u"app.292120"_s);
        QCOMPARE(page()->property("title").toString(), u"FINAL FANTASY XIII"_s);
        QCOMPARE(qvariant_cast<QObject *>(page()->property("header"))->property("text").toString(), u"FINAL FANTASY XIII"_s);
        key(Qt::Key_Escape);
        QCOMPARE(page()->property("group").toString(), u"games"_s);
        focus(u"game-5"_s);
        key(Qt::Key_Return);
        QCOMPARE(page()->property("group").toString(), u"app.5"_s);
        QQuickItem *header = qvariant_cast<QQuickItem *>(page()->property("header"));
        QVERIFY(header && header->childItems().size() == 1);
        QCOMPARE(header->childItems().first()->property("text").toString(), u"<b>bold</b>"_s);
        QCOMPARE(header->childItems().first()->property("textFormat").toInt(), int(Qt::PlainText));
        key(Qt::Key_Escape);
        QCOMPARE(page()->property("group").toString(), u"games"_s);
        key(Qt::Key_Escape);
        QCOMPARE(page()->property("title").toString(), u"settings"_s);
        qunsetenv("MICROSET_STEAM_DIR");
    }

    void gameDraftWritesOneSet()
    {
        QTemporaryDir steam;
        qputenv("MICROSET_STEAM_DIR", steam.path().toUtf8());
        FakeMicroSetd::writeSteam(steam.path(), {{u"292120"_s, u"FINAL FANTASY XIII"_s}});
        fake->setProton({FakeMicroSetd::shipped()}, FakeMicroSetd::Profiles);
        model->load();
        focus(u"group-games"_s);
        key(Qt::Key_Return);
        QTRY_VERIFY(!row(u"game-292120"_s).isEmpty());
        focus(u"game-292120"_s);
        key(Qt::Key_Return);
        QCOMPARE(page()->property("group").toString(), u"app.292120"_s);
        QTRY_COMPARE(value(u"app.292120.fex.tso"_s), u"auto (off, profile)"_s);
        QCOMPARE(page()->property("focusIndex").toInt(), 1);
        QCOMPARE(row(u"app.292120.profile"_s).value(u"value"_s).toString(), u"on (TSO off, fsync)"_s);
        key(Qt::Key_Down);
        key(Qt::Key_Down);
        reachable(u"app.292120.fex.tso"_s);
        key(Qt::Key_Return);
        QVERIFY(sheetShown());
        QCOMPARE(sheet()->property("title").toString(), u"TSO"_s);
        QCOMPARE(sheet()->property("items").toStringList(), (QStringList{u"auto (off, profile)"_s, u"on"_s, u"off"_s}));
        QCOMPARE(sheet()->property("mark").toInt(), 0);
        key(Qt::Key_Down);
        QCOMPARE(value(u"app.292120.fex.tso"_s), u"on"_s);
        key(Qt::Key_Escape);
        QVERIFY(!sheetShown());
        QCOMPARE(value(u"app.292120.fex.tso"_s), u"auto (off, profile)"_s);
        key(Qt::Key_Return);
        key(Qt::Key_Down);
        key(Qt::Key_Down);
        QTest::qWait(100);
        QVERIFY(fake->calls().isEmpty());
        key(Qt::Key_Return);
        QVERIFY(!sheetShown());
        QTRY_COMPARE(fake->calls(), QStringList{u"Set app.292120.fex.tso off"_s});
        QTRY_COMPARE(value(u"app.292120.fex.tso"_s), u"off"_s);
        QTRY_COMPARE(row(u"app.292120.profile"_s).value(u"value"_s).toString(), u"on (TSO off overridden, fsync)"_s);
        key(Qt::Key_Up);
        reachable(u"app.292120.fex.preset"_s);
        key(Qt::Key_Up);
        reachable(u"app.292120.profile"_s);
        key(Qt::Key_Return);
        QVERIFY(sheetShown());
        QCOMPARE(sheet()->property("items").toStringList(), (QStringList{u"on"_s, u"off"_s}));
        key(Qt::Key_Down);
        QCOMPARE(row(u"app.292120.profile"_s).value(u"value"_s).toString(), u"off"_s);
        key(Qt::Key_Return);
        QTRY_COMPARE(fake->calls().size(), 2);
        QCOMPARE(fake->calls().last(), u"Set app.292120.profile off"_s);
        for (int i = 0; i < 20; ++i)
            key(Qt::Key_Down);
        reachable(u"game-next"_s);
        key(Qt::Key_Return);
        QVERIFY(!sheetShown());
        QCOMPARE(fake->calls().size(), 2);
        key(Qt::Key_Escape);
        QCOMPARE(page()->property("group").toString(), u"games"_s);
        qunsetenv("MICROSET_STEAM_DIR");
    }

    void leadFollowsFocus()
    {
        fake->setProton({FakeMicroSetd::shipped()}, FakeMicroSetd::Profiles);
        open(u"app.292120"_s);
        QTRY_COMPARE(value(u"app.292120.fex.tso"_s), u"auto (off, profile)"_s);
        focus(u"app.292120.fex.tso"_s);
        key(Qt::Key_Return);
        QVERIFY(sheetShown());
        QObject *lead = qvariant_cast<QObject *>(sheet()->property("leadItem"));
        const QString tso = u"may crash, hang or damage saves"_s;
        QCOMPARE(sheet()->property("lead").toString(), tso);
        QVERIFY(lead->property("visible").toBool());
        const qreal edge = sheet()->property("edge").toReal();
        const qreal pitch = sheet()->property("pitch").toReal();
        QCOMPARE(lead->property("x").toReal(), edge);
        QCOMPARE(lead->property("y").toReal(), edge + sheet()->property("titleHeight").toReal());
        QCOMPARE(lead->property("height").toReal(), pitch);
        QCOMPARE(sheet()->property("leadHeight").toReal(), pitch);
        QCOMPARE(lead->property("font").value<QFont>().capitalization(), QFont::AllUppercase);
        QVERIFY(qAbs(qvariant_cast<QColor>(lead->property("color")).alphaF() - 0.6) < 0.01);
        QVERIFY(sheet()->property("keepLead").toBool());
        QObject *panel = qvariant_cast<QObject *>(sheet()->property("panel"));
        const qreal top = panel->property("y").toReal();
        key(Qt::Key_Down);
        QCOMPARE(sheet()->property("lead").toString(), QString());
        QVERIFY(!lead->property("visible").toBool());
        QCOMPARE(sheet()->property("leadHeight").toReal(), pitch);
        QCOMPARE(panel->property("y").toReal(), top);
        key(Qt::Key_Down);
        QCOMPARE(sheet()->property("lead").toString(), tso);
        QCOMPARE(panel->property("y").toReal(), top);
        key(Qt::Key_Escape);
        QVERIFY(!sheetShown());
        QTRY_COMPARE(sheet()->property("lead").toString(), QString());
        QVERIFY(!sheet()->property("keepLead").toBool());
        QVERIFY(fake->calls().isEmpty());
        open(u"audio"_s);
        focus(u"audio.volume_step"_s);
        key(Qt::Key_Return);
        QVERIFY(sheetShown());
        QCOMPARE(sheet()->property("lead").toString(), QString());
        QVERIFY(!sheet()->property("keepLead").toBool());
        QCOMPARE(sheet()->property("leadHeight").toReal(), 0.0);
        key(Qt::Key_Escape);
    }

    void thisAppKeepsFocus()
    {
        model->lsfg()->setFixture({}, true, u"android"_s);
        const QString kept = u"group-audio"_s;
        QTRY_VERIFY(!row(u"perf.android_overlay"_s).isEmpty());
        focus(kept);
        model->setAndroidApp(u"com.example.game"_s);
        QTRY_VERIFY(!row(u"android-app"_s).isEmpty());
        reachable(kept);
        model->setAndroidApp({});
        QTRY_VERIFY(row(u"android-app"_s).isEmpty());
        reachable(kept);
    }

    void thisAppKeepsLastRow()
    {
        model->lsfg()->setFixture({}, true, u"android"_s);
        QTRY_VERIFY(!row(u"perf.android_overlay"_s).isEmpty());
        int last = page()->property("rows").toList().size() - 1;
        bool focusable = false;
        while (last >= 0) {
            QVERIFY(QMetaObject::invokeMethod(page(), "isFocusable", Q_RETURN_ARG(bool, focusable), Q_ARG(int, last)));
            if (focusable)
                break;
            --last;
        }
        QVERIFY(last >= 0);
        const QString kept = page()->property("rows").toList().at(last).toMap().value(u"key"_s).toString();
        focus(kept);
        QVERIFY(QMetaObject::invokeMethod(page(), "ensureVisible", Q_ARG(int, 0)));
        reachable(kept);
        model->setAndroidApp(u"com.example.game"_s);
        QTRY_VERIFY(!row(u"android-app"_s).isEmpty());
        reachable(kept);
    }

    void thisGameOpensItsPage()
    {
        fake->setProton({FakeMicroSetd::shipped()}, FakeMicroSetd::Profiles);
        model->setGame({{u"appid"_s, u"292120"_s}, {u"hook"_s, u"bad-local"_s}, {u"tool"_s, u"Proton 11.0 (ARM64)"_s},
                        {u"proton"_s, true}});
        QTRY_VERIFY(!row(u"game-profile"_s).isEmpty());
        QStringList keys;
        for (const QVariant &r : page()->property("rows").toList())
            keys << r.toMap().value(u"key"_s).toString();
        QCOMPARE(keys.mid(0, 5), (QStringList{u"label-this game"_s, u"game-292120"_s, u"game-state"_s, u"game-profile"_s,
                                              u"label-microset"_s}));
        focus(u"game-292120"_s);
        key(Qt::Key_Return);
        QCOMPARE(page()->property("group").toString(), u"app.292120"_s);
        QTRY_VERIFY(!row(u"app.292120.fex.tso"_s).isEmpty());
        key(Qt::Key_Escape);
        QCOMPARE(page()->property("title").toString(), u"settings"_s);
        focus(u"game-292120"_s);
        key(Qt::Key_Down);
        reachable(u"game-state"_s);
        key(Qt::Key_Return);
        key(Qt::Key_Down);
        reachable(u"game-profile"_s);
        key(Qt::Key_Return);
        QCOMPARE(page()->property("title").toString(), u"settings"_s);
        QVERIFY(!sheetShown());
        model->setGame({{u"appid"_s, u"292120"_s}, {u"tool"_s, u"<i>Proton</i>"_s}, {u"proton"_s, true}});
        QTRY_COMPARE(value(u"game-missing"_s), u"<i>Proton</i>"_s);
        int missing = -1;
        QVERIFY(QMetaObject::invokeMethod(page(), "indexOfKey", Q_RETURN_ARG(int, missing), Q_ARG(QString, u"game-missing"_s)));
        QQuickItem *delegate = nullptr;
        QVERIFY(QMetaObject::invokeMethod(qvariant_cast<QObject *>(page()->property("repeater")), "itemAt",
                                          Q_RETURN_ARG(QQuickItem *, delegate), Q_ARG(int, missing)));
        QVERIFY(delegate);
        QObject *shown = qvariant_cast<QObject *>(delegate->property("valueItem"));
        QCOMPARE(shown->property("text").toString(), u"<i>Proton</i>"_s);
        QCOMPARE(shown->property("textFormat").toInt(), int(Qt::PlainText));
        model->setGame({});
        QTRY_VERIFY(row(u"game-292120"_s).isEmpty());
        bool focusable = false;
        QVERIFY(QMetaObject::invokeMethod(page(), "isFocusable", Q_RETURN_ARG(bool, focusable),
                                          Q_ARG(int, page()->property("focusIndex").toInt())));
        QVERIFY(focusable);
    }

    void actionRowsAskFirst()
    {
        open(u"display"_s);
        const QVariantMap base{{u"kind"_s, u"row"_s}, {u"value"_s, QString()}, {u"readOnly"_s, true},
                               {u"widget"_s, QString()}, {u"swatch"_s, QString()}, {u"hue"_s, -1},
                               {u"confirm"_s, false}, {u"pick"_s, QString()}, {u"bounded"_s, false}};
        QVariantMap ask = base;
        ask.insert({{u"key"_s, u"test-ask"_s}, {u"title"_s, u"Ask first"_s}, {u"action"_s, u"test-ask"_s},
                    {u"ask"_s, u"ask first"_s}, {u"accept"_s, u"go"_s}});
        QVariantMap now = base;
        now.insert({{u"key"_s, u"test-now"_s}, {u"title"_s, u"Right away"_s}, {u"action"_s, u"test-now"_s}});
        const QVariantList rows = page()->property("modelRows").toList();
        page()->setProperty("modelRows", QVariantList(rows) << ask << now);
        auto *dialog = qvariant_cast<QObject *>(page()->property("confirm"));
        QVERIFY(dialog);
        QSignalSpy acted(page(), SIGNAL(acted(QString)));
        focus(u"test-ask"_s);
        key(Qt::Key_Return);
        QVERIFY(dialog->property("shown").toBool());
        QCOMPARE(dialog->property("title").toString(), u"ask first"_s);
        QCOMPARE(dialog->property("items").toStringList(), (QStringList{u"go"_s, u"cancel"_s}));
        QCOMPARE(dialog->property("focusIndex").toInt(), 1);
        key(Qt::Key_Down);
        QCOMPARE(dialog->property("focusIndex").toInt(), 1);
        key(Qt::Key_Return);
        QVERIFY(!dialog->property("shown").toBool());
        QCOMPARE(acted.count(), 0);
        key(Qt::Key_Return);
        QCOMPARE(dialog->property("focusIndex").toInt(), 1);
        key(Qt::Key_Escape);
        QVERIFY(!dialog->property("shown").toBool());
        QCOMPARE(page()->property("group").toString(), u"display"_s);
        QCOMPARE(acted.count(), 0);
        key(Qt::Key_Return);
        key(Qt::Key_Up);
        QCOMPARE(dialog->property("focusIndex").toInt(), 0);
        key(Qt::Key_Return);
        QVERIFY(!dialog->property("shown").toBool());
        QCOMPARE(acted.count(), 1);
        QCOMPARE(acted.takeFirst().value(0).toString(), u"test-ask"_s);
        QVERIFY(fake->calls().isEmpty());
        key(Qt::Key_Return);
        QVERIFY(dialog->property("shown").toBool());
        page()->setProperty("modelRows", QVariantList(rows) << now);
        QVERIFY(!dialog->property("shown").toBool());
        focus(u"test-now"_s);
        key(Qt::Key_Return);
        QVERIFY(!dialog->property("shown").toBool());
        QCOMPARE(acted.count(), 1);
        QCOMPARE(acted.takeFirst().value(0).toString(), u"test-now"_s);
        page()->setProperty("modelRows", QVariantList(rows) << ask);
        focus(u"test-ask"_s);
        key(Qt::Key_Return);
        QVERIFY(dialog->property("shown").toBool());
        page()->setProperty("screenOff", true);
        QVERIFY(!dialog->property("shown").toBool());
        QCOMPARE(acted.count(), 0);
    }

    void lsfgInstallAsksFirst()
    {
        const QString log = h->path(u"systemctl.log"_s);
        h->writeFile(u"bin/systemctl"_s, "#!/bin/sh\necho \"$*\" >> '" + QFile::encodeName(log) + "'\n");
        QFile::setPermissions(h->path(u"bin/systemctl"_s), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        const QByteArray path = qgetenv("PATH");
        qputenv("PATH", QFile::encodeName(h->path(u"bin"_s)) + ':' + path);
        writeAtomic(u"run/session-current"_s, "steam gamescope-session-plus@steam.service 3 120\n");
        writeLsfg(R"({"version": 1, "owned": "yes", "install": "none", "send": {"result": null}})");
        openLsfg();
        QTRY_VERIFY(!row(u"lsfg-install"_s).isEmpty());
        auto *dialog = qvariant_cast<QObject *>(page()->property("confirm"));
        focus(u"lsfg-install"_s);
        key(Qt::Key_Return);
        QVERIFY(dialog->property("shown").toBool());
        QCOMPARE(dialog->property("title").toString(), u"lossless scaling"_s);
        QCOMPARE(dialog->property("items").toStringList(), (QStringList{u"install"_s, u"cancel"_s}));
        QCOMPARE(dialog->property("focusIndex").toInt(), 1);
        key(Qt::Key_Return);
        QVERIFY(!dialog->property("shown").toBool());
        QTest::qWait(100);
        QVERIFY(!QFile::exists(log));
        key(Qt::Key_Return);
        key(Qt::Key_Up);
        key(Qt::Key_Return);
        QVERIFY(!dialog->property("shown").toBool());
        QTRY_VERIFY(QFile::exists(log));
        QFile file(log);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), "--user start --job-mode=fail microos-lsfg-send@manual.service\n");
        const QJsonObject send{{u"result"_s, u"sent"_s}, {u"last_boot"_s, LsfgState::bootId()},
                               {u"last_mono"_s, LsfgState::monotonic()}};
        writeLsfg(QJsonDocument(QJsonObject{{u"version"_s, 1}, {u"owned"_s, u"yes"_s}, {u"install"_s, u"none"_s}, {u"send"_s, send}})
                      .toJson(QJsonDocument::Compact));
        QTRY_COMPARE(value(u"lsfg-status"_s), u"install sent"_s);
        key(Qt::Key_Return);
        QVERIFY(dialog->property("shown").toBool());
        writeLsfg(R"({"version": 1, "owned": "yes", "install": "installing", "send": {"result": "sent"}})");
        QTRY_VERIFY(!dialog->property("shown").toBool());
        QTRY_COMPARE(value(u"lsfg-status"_s), u"installing"_s);
        QVERIFY(row(u"lsfg-install"_s).isEmpty());
        qputenv("PATH", path);
    }

    QObject *rowSheet() const
    {
        return qvariant_cast<QObject *>(page()->property("rowSheet"));
    }

    QObject *gameSheet() const
    {
        return qvariant_cast<QObject *>(page()->property("gameSheet"));
    }

    QStringList entryKeys() const
    {
        QStringList out;
        for (const QVariant &entry : rowSheet()->property("entries").toList())
            out << entry.toMap().value(u"key"_s).toString();
        return out;
    }

    QString entryValue(const QString &key) const
    {
        for (const QVariant &entry : rowSheet()->property("entries").toList())
            if (entry.toMap().value(u"key"_s).toString() == key)
                return entry.toMap().value(u"value"_s).toString();
        return {};
    }

    void lsfgSheet()
    {
        QTemporaryDir steam;
        qputenv("MICROSET_STEAM_DIR", steam.path().toUtf8());
        FakeMicroSetd::writeSteam(steam.path(), {{u"292120"_s, u"FINAL FANTASY XIII"_s}, {u"2238900"_s, u"SO2R"_s}});
        fake->setProton({FakeMicroSetd::shipped()}, FakeMicroSetd::Profiles);
        fake->setValues({{u"app.292120.lsfg.state"_s, u"off"_s}});
        fake->setStored({{u"app.292120.lsfg.state"_s, u"off"_s}});
        model->load();
        QTRY_COMPARE(model->value(u"app.292120.lsfg.state"_s, model->revision()).toString(), u"off"_s);
        writeLsfg(R"({"version": 1, "owned": "yes", "install": "installed"})");
        openLsfg();
        QTRY_COMPARE(value(u"lsfg-games"_s), u"0 on"_s);
        QCOMPARE(value(u"lsfg-status"_s), u"installed"_s);
        QVERIFY(row(u"lsfg-proton"_s).isEmpty());
        focus(u"lsfg-games"_s);
        key(Qt::Key_Return);
        QVERIFY(rowSheet()->property("shown").toBool());
        QCOMPARE(rowSheet()->property("title").toString(), u"Games"_s);
        QCOMPARE(qvariant_cast<QObject *>(rowSheet()->property("leadLine"))->property("text").toString(),
                 u"Proton games only"_s);
        QTRY_COMPARE(entryKeys(), (QStringList{u"lsfg-game-292120"_s, u"lsfg-add"_s}));
        QCOMPARE(entryValue(u"lsfg-game-292120"_s), u"off"_s);
        QCOMPARE(rowSheet()->property("focusIndex").toInt(), 0);
        key(Qt::Key_Return);
        QVERIFY(gameSheet()->property("shown").toBool());
        QCOMPARE(gameSheet()->property("title").toString(), u"FINAL FANTASY XIII"_s);
        QCOMPARE(gameSheet()->property("items").toStringList(), (QStringList{u"off"_s, u"on"_s}));
        QCOMPARE(gameSheet()->property("focusIndex").toInt(), 0);
        QCOMPARE(gameSheet()->property("mark").toInt(), 0);
        QVERIFY(rowSheet()->property("covered").toBool());
        QCOMPARE(rowSheet()->property("opacity").toReal(), 0.0);
        QVERIFY(fake->calls().isEmpty());
        key(Qt::Key_Down);
        key(Qt::Key_Return);
        QTRY_COMPARE(fake->calls(), QStringList{u"Set app.292120.lsfg.state on"_s});
        QVERIFY(!gameSheet()->property("shown").toBool());
        QVERIFY(!rowSheet()->property("covered").toBool());
        QVERIFY(rowSheet()->property("shown").toBool());
        QTRY_COMPARE(entryValue(u"lsfg-game-292120"_s), u"on"_s);
        QCOMPARE(rowSheet()->property("focusIndex").toInt(), 0);
        QTRY_COMPARE(value(u"lsfg-games"_s), u"1 on"_s);
        key(Qt::Key_Return);
        QCOMPARE(gameSheet()->property("focusIndex").toInt(), 1);
        key(Qt::Key_Up);
        key(Qt::Key_Return);
        QTRY_COMPARE(fake->calls(), (QStringList{u"Set app.292120.lsfg.state on"_s, u"Set app.292120.lsfg.state auto"_s}));
        QTRY_COMPARE(entryValue(u"lsfg-game-292120"_s), u"off"_s);
        key(Qt::Key_Return);
        key(Qt::Key_Return);
        QTest::qWait(100);
        QCOMPARE(fake->calls().size(), 2);
        key(Qt::Key_Down);
        QCOMPARE(rowSheet()->property("focusIndex").toInt(), 1);
        key(Qt::Key_Return);
        QVERIFY(!gameSheet()->property("shown").toBool());
        QCOMPARE(fake->calls().size(), 2);
        key(Qt::Key_Escape);
        QVERIFY(!rowSheet()->property("shown").toBool());
        QCOMPARE(page()->property("group").toString(), Lsfg);
        focus(u"lsfg.enabled"_s);
        key(Qt::Key_Return);
        QVERIFY(sheetShown());
        key(Qt::Key_Up);
        QTRY_COMPARE(fake->calls().last(), u"Set lsfg.enabled false"_s);
        key(Qt::Key_Return);
        QTRY_COMPARE(value(u"lsfg-games"_s), u"off"_s);
        QCOMPARE(row(u"lsfg-off"_s).value(u"title"_s).toString(), u"Frame generation is off"_s);
        focus(u"lsfg-games"_s);
        key(Qt::Key_Return);
        QVERIFY(!rowSheet()->property("shown").toBool());
        QCOMPARE(page()->property("group").toString(), Lsfg);
        qunsetenv("MICROSET_STEAM_DIR");
    }

    void lsfgSheetFollowsList()
    {
        QTemporaryDir steam;
        qputenv("MICROSET_STEAM_DIR", steam.path().toUtf8());
        FakeMicroSetd::writeSteam(steam.path(), {{u"292120"_s, u"FINAL FANTASY XIII"_s}, {u"2238900"_s, u"SO2R"_s},
                                                 {u"1621310"_s, u"Atelier Sophie 2"_s}});
        fake->setProton({FakeMicroSetd::shipped()}, FakeMicroSetd::Profiles);
        QVariantMap stored{{u"app.2238900.lsfg.state"_s, u"off"_s}};
        fake->setValues(stored);
        fake->setStored(stored);
        model->load();
        QTRY_COMPARE(model->value(u"app.2238900.lsfg.state"_s, model->revision()).toString(), u"off"_s);
        writeLsfg(R"({"version": 1, "owned": "yes", "install": "installed"})");
        openLsfg();
        QTRY_COMPARE(value(u"lsfg-games"_s), u"0 on"_s);
        focus(u"lsfg-games"_s);
        key(Qt::Key_Return);
        QTRY_COMPARE(entryKeys(), (QStringList{u"lsfg-game-2238900"_s, u"lsfg-add"_s}));
        key(Qt::Key_Return);
        QVERIFY(gameSheet()->property("shown").toBool());
        key(Qt::Key_Escape);
        QVERIFY(!gameSheet()->property("shown").toBool());
        QVERIFY(rowSheet()->property("shown").toBool());
        QVERIFY(!rowSheet()->property("covered").toBool());
        QCOMPARE(rowSheet()->property("focusIndex").toInt(), 0);
        QVERIFY(fake->calls().isEmpty());
        stored.insert(u"app.1621310.lsfg.state"_s, u"on"_s);
        fake->setStored(stored);
        fake->emitChanged(u"app.1621310.lsfg.state"_s, u"on"_s);
        QTRY_COMPARE(entryKeys().size(), 3);
        QCOMPARE(entryKeys().first(), u"lsfg-game-1621310"_s);
        QCOMPARE(rowSheet()->property("focusIndex").toInt(), 1);
        QCOMPARE(entryKeys().at(1), u"lsfg-game-2238900"_s);
        stored.insert(u"lsfg.enabled"_s, false);
        fake->setStored(stored);
        fake->emitChanged(u"lsfg.enabled"_s, false);
        QTRY_VERIFY(!rowSheet()->property("shown").toBool());
        QVERIFY(!gameSheet()->property("shown").toBool());
        qunsetenv("MICROSET_STEAM_DIR");
    }

    void toastStaysWhileSticky()
    {
        QObject *t = toast();
        t->setProperty("sticky", true);
        QVERIFY(QMetaObject::invokeMethod(t, "show", Q_ARG(QString, QString()), Q_ARG(QString, u"Hold A to format"_s)));
        QTest::qWait(t->property("inDuration").toInt() + t->property("holdDuration").toInt()
                     + t->property("outDuration").toInt() + 300);
        QCOMPARE(t->property("phase").toString(), u"hold"_s);
        t->setProperty("sticky", false);
        QCOMPARE(t->property("phase").toString(), u"out"_s);
        QTRY_COMPARE(t->property("phase").toString(), u"hidden"_s);
    }

    void backHidesOverlayOnRelease()
    {
        QSignalSpy hide(view, SIGNAL(hideRequested()));
        QTest::keyPress(window, Qt::Key_Escape);
        QTest::keyRelease(window, Qt::Key_Escape);
        QCOMPARE(hide.count(), 0);
        page()->setProperty("overlay", true);
        QTest::keyPress(window, Qt::Key_Escape);
        QCOMPARE(hide.count(), 0);
        QTest::keyRelease(window, Qt::Key_Escape);
        QCOMPARE(hide.count(), 1);
        open(u"audio"_s);
        QTest::keyPress(window, Qt::Key_Escape);
        QCOMPARE(page()->property("title").toString(), u"settings"_s);
        QTest::keyRelease(window, Qt::Key_Escape);
        QCOMPARE(hide.count(), 1);
    }

    void sheetLivePreviewAndCancel()
    {
        open(u"audio"_s);
        focus(u"audio.volume_step"_s);
        key(Qt::Key_Return);
        QVERIFY(sheetShown());
        QVERIFY(page()->property("valueOpen").toBool());
        QCOMPARE(sheet()->property("items").toStringList(), (QStringList{u"10%"_s, u"5%"_s, u"2%"_s}));
        QCOMPARE(sheet()->property("title").toString(), u"Volume step"_s);
        QCOMPARE(sheetFocus(), 1);
        QCOMPARE(sheet()->property("mark").toInt(), 1);
        key(Qt::Key_Down);
        QTRY_COMPARE(fake->calls(), QStringList{u"Set audio.volume_step 2"_s});
        QTRY_COMPARE(value(u"audio.volume_step"_s), u"2%"_s);
        QCOMPARE(sheet()->property("mark").toInt(), 1);
        key(Qt::Key_Escape);
        QVERIFY(!sheetShown());
        QCOMPARE(page()->property("group").toString(), u"audio"_s);
        QTRY_COMPARE(fake->calls(), (QStringList{u"Set audio.volume_step 2"_s, u"Set audio.volume_step 5"_s}));
        QTRY_COMPARE(value(u"audio.volume_step"_s), u"5%"_s);
        QTRY_VERIFY(!model->busy(u"audio.volume_step"_s));
        key(Qt::Key_Return);
        key(Qt::Key_Down);
        QTRY_COMPARE(fake->calls().size(), 3);
        key(Qt::Key_Return);
        QVERIFY(!sheetShown());
        QTest::qWait(100);
        QCOMPARE(fake->calls().size(), 3);
        QCOMPARE(fake->stored().value(u"audio.volume_step"_s), QVariant(2u));
    }

    void sheetDraftAppliesOnA()
    {
        open(u"audio"_s);
        focus(u"audio.max_volume"_s);
        key(Qt::Key_Return);
        QCOMPARE(sheetFocus(), 0);
        key(Qt::Key_Down);
        key(Qt::Key_Down);
        QCOMPARE(value(u"audio.max_volume"_s), u"90%"_s);
        QTest::qWait(100);
        QVERIFY(fake->calls().isEmpty());
        key(Qt::Key_Return);
        QTRY_COMPARE(fake->calls(), QStringList{u"Set audio.max_volume 90"_s});
        QTRY_COMPARE(value(u"audio.max_volume"_s), u"90%"_s);
        QTRY_VERIFY(!model->busy(u"audio.max_volume"_s));
        key(Qt::Key_Return);
        key(Qt::Key_Down);
        key(Qt::Key_Up);
        key(Qt::Key_Up);
        key(Qt::Key_Escape);
        QCOMPARE(value(u"audio.max_volume"_s), u"90%"_s);
        QTest::qWait(100);
        QCOMPARE(fake->calls().size(), 1);
        open(u"system"_s);
        focus(u"session.default"_s);
        key(Qt::Key_Return);
        key(Qt::Key_Up);
        QCOMPARE(value(u"session.default"_s), u"microemu"_s);
        key(Qt::Key_Return);
        QTRY_COMPARE(fake->calls().size(), 2);
        QCOMPARE(fake->calls().last(), u"Set session.default microemu"_s);
    }

    void heldMovePreviewsNothing()
    {
        ledsOn();
        open(u"stick leds"_s);
        focus(u"rgb.brightness"_s);
        key(Qt::Key_Return);
        QCOMPARE(sheet()->property("items").toStringList().size(), 21);
        QCOMPARE(sheetFocus(), 10);
        for (int i = 0; i < 30; ++i)
            held(u"down"_s);
        QCOMPARE(sheetFocus(), 20);
        QTest::qWait(100);
        QVERIFY(fake->calls().isEmpty());
        QCOMPARE(value(u"rgb.brightness"_s), u"50 %"_s);
        key(Qt::Key_Escape);
        QTest::qWait(100);
        QVERIFY(fake->calls().isEmpty());
    }

    void wrapsUnlessBounded()
    {
        open(u"audio"_s);
        focus(u"audio.volume_step"_s);
        key(Qt::Key_Return);
        key(Qt::Key_Down);
        key(Qt::Key_Down);
        QCOMPARE(sheetFocus(), 0);
        key(Qt::Key_Escape);
        focus(u"audio.hp_gain"_s);
        key(Qt::Key_Return);
        QCOMPARE(sheetFocus(), 2);
        key(Qt::Key_Down);
        QCOMPARE(sheetFocus(), 2);
        key(Qt::Key_Up);
        key(Qt::Key_Up);
        key(Qt::Key_Up);
        QCOMPARE(sheetFocus(), 0);
        key(Qt::Key_Escape);
    }

    void riseKeepsBAndAStep()
    {
        open(u"audio"_s);
        page()->setProperty("animated", true);
        focus(u"audio.volume_step"_s);
        key(Qt::Key_Return);
        QVERIFY(sheet()->property("animating").toBool());
        key(Qt::Key_Return);
        key(Qt::Key_Down);
        key(Qt::Key_Escape);
        QVERIFY(sheetShown());
        QTRY_VERIFY(!sheetShown());
        QTest::qWait(100);
        QVERIFY(fake->calls().isEmpty());
        QTRY_VERIFY(!sheet()->property("animating").toBool());
        key(Qt::Key_Return);
        QVERIFY(sheet()->property("animating").toBool());
        key(Qt::Key_Down);
        key(Qt::Key_Up);
        QCOMPARE(sheetFocus(), 1);
        QTRY_VERIFY(!sheet()->property("animating").toBool());
        QCOMPARE(sheetFocus(), 2);
        QTRY_COMPARE(fake->calls(), QStringList{u"Set audio.volume_step 2"_s});
        key(Qt::Key_Return);
        QVERIFY(!sheetShown());
        QVERIFY(sheet()->property("animating").toBool());
        QTRY_VERIFY(!sheet()->property("animating").toBool());
        QCOMPARE(fake->calls().size(), 1);
    }

    void busyKeyOpensNothing()
    {
        fake->setHold(true);
        open(u"audio"_s);
        focus(u"audio.volume_step"_s);
        key(Qt::Key_Return);
        key(Qt::Key_Down);
        QTRY_COMPARE(fake->sets().size(), 1);
        key(Qt::Key_Return);
        QVERIFY(!sheetShown());
        key(Qt::Key_Return);
        QVERIFY(!sheetShown());
        QVERIFY(!page()->property("valueOpen").toBool());
        fake->release();
        QTRY_VERIFY(!model->busy(u"audio.volume_step"_s));
        QCOMPARE(value(u"audio.volume_step"_s), u"2%"_s);
        key(Qt::Key_Return);
        QVERIFY(sheetShown());
        QCOMPARE(sheetFocus(), 2);
        key(Qt::Key_Escape);
    }

    void overridePreviewCalls()
    {
        ledsOn();
        open(u"stick leds"_s);
        focus(u"rgb.saturation"_s);
        key(Qt::Key_Return);
        QCOMPARE(sheetFocus(), 8);
        key(Qt::Key_Down);
        QTRY_COMPARE(fake->calls(), QStringList{u"Override rgb.saturation 55"_s});
        QTRY_COMPARE(value(u"rgb.saturation"_s), u"55 %"_s);
        key(Qt::Key_Return);
        QTRY_COMPARE(fake->calls(), (QStringList{u"Override rgb.saturation 55"_s, u"Set rgb.saturation 55"_s,
                                                 u"ClearOverride rgb.saturation"_s}));
        QTRY_COMPARE(value(u"rgb.saturation"_s), u"55 %"_s);
        key(Qt::Key_Return);
        key(Qt::Key_Down);
        QTRY_COMPARE(fake->calls().size(), 4);
        QCOMPARE(fake->calls().last(), u"Override rgb.saturation 50"_s);
        key(Qt::Key_Escape);
        QTRY_COMPARE(fake->calls().size(), 5);
        QCOMPARE(fake->calls().last(), u"ClearOverride rgb.saturation"_s);
        QTRY_COMPARE(value(u"rgb.saturation"_s), u"55 %"_s);
    }

    void outsideChangeClosesKeepingIt()
    {
        open(u"audio"_s);
        focus(u"audio.volume_step"_s);
        key(Qt::Key_Return);
        fake->emitChanged(u"audio.volume_step"_s, 10u);
        QTRY_VERIFY(!sheetShown());
        QTRY_COMPARE(value(u"audio.volume_step"_s), u"10%"_s);
        QTest::qWait(100);
        QVERIFY(fake->calls().isEmpty());
    }

    void lockCancels()
    {
        ledsOn();
        open(u"stick leds"_s);
        focus(u"rgb.brightness"_s);
        key(Qt::Key_Return);
        key(Qt::Key_Down);
        QTRY_COMPARE(fake->calls(), QStringList{u"Set rgb.brightness 45"_s});
        fake->setStored({{u"rgb.enabled"_s, false}, {u"rgb.saturation"_s, 60u}});
        fake->emitChanged(u"rgb.enabled"_s, false);
        QTRY_VERIFY(!sheetShown());
        QVERIFY(!page()->property("valueOpen").toBool());
        QVERIFY(row(u"rgb.brightness"_s).value(u"readOnly"_s).toBool());
        QTRY_COMPARE(fake->calls(), (QStringList{u"Set rgb.brightness 45"_s, u"Set rgb.brightness 50"_s}));
        QTest::qWait(100);
        QCOMPARE(fake->calls().size(), 2);
    }

    void lockBeforeCommitRestores()
    {
        ledsOn();
        open(u"stick leds"_s);
        focus(u"rgb.brightness"_s);
        key(Qt::Key_Return);
        key(Qt::Key_Down);
        QTRY_COMPARE(fake->calls(), QStringList{u"Set rgb.brightness 45"_s});
        held(u"down"_s);
        QCOMPARE(sheetFocus(), 12);
        bool pressed = false;
        const auto pressOnLock = connect(model.get(), &SettingsModel::revisionChanged, this, [&] {
            if (pressed || !row(u"rgb.brightness"_s).value(u"readOnly"_s).toBool())
                return;
            pressed = true;
            QVERIFY(sheetShown());
            key(Qt::Key_Return);
        });
        fake->setStored({{u"rgb.enabled"_s, false}, {u"rgb.saturation"_s, 60u}});
        fake->emitChanged(u"rgb.enabled"_s, false);
        QTRY_VERIFY(pressed);
        disconnect(pressOnLock);
        QTRY_VERIFY(!sheetShown());
        QTRY_COMPARE(fake->calls(), (QStringList{u"Set rgb.brightness 45"_s, u"Set rgb.brightness 50"_s}));
        QTest::qWait(100);
        QCOMPARE(fake->calls().size(), 2);
    }

    void dismissKeepsLiveDropsDraft()
    {
        QObject *ui = engine->singletonInstance<QObject *>("MicroSet", "Ui");
        QVERIFY(ui);
        open(u"system"_s);
        focus(u"session.default"_s);
        key(Qt::Key_Return);
        key(Qt::Key_Up);
        ui->setProperty("turn", 1);
        QVERIFY(!sheetShown());
        QCOMPARE(value(u"session.default"_s), u"steam"_s);
        ui->setProperty("turn", 0);
        open(u"audio"_s);
        focus(u"audio.volume_step"_s);
        key(Qt::Key_Return);
        key(Qt::Key_Down);
        QTRY_COMPARE(fake->calls().size(), 1);
        held(u"down"_s);
        QCOMPARE(sheetFocus(), 2);
        key(Qt::Key_Up);
        QTRY_COMPARE(fake->calls().size(), 2);
        held(u"down"_s);
        QCOMPARE(sheetFocus(), 2);
        ui->setProperty("turn", 1);
        ui->setProperty("turn", 0);
        QVERIFY(!sheetShown());
        QTest::qWait(100);
        QCOMPARE(fake->calls(), (QStringList{u"Set audio.volume_step 2"_s, u"Set audio.volume_step 5"_s}));
        QCOMPARE(value(u"audio.volume_step"_s), u"5%"_s);
        key(Qt::Key_Return);
        key(Qt::Key_Down);
        QTRY_COMPARE(fake->calls().size(), 3);
        QVERIFY(QMetaObject::invokeMethod(page(), "back"));
        QCOMPARE(page()->property("title").toString(), u"system"_s);
        QTest::qWait(100);
        QCOMPARE(fake->calls().size(), 3);
        QCOMPARE(fake->stored().value(u"audio.volume_step"_s), QVariant(2u));
    }

    void refusalToastOverSheet()
    {
        fake->refuse(u"audio.volume_step"_s, u"net.seilent.MicroSet1.Error.InvalidValue"_s);
        open(u"audio"_s);
        focus(u"audio.volume_step"_s);
        key(Qt::Key_Return);
        key(Qt::Key_Down);
        QTRY_VERIFY(toast()->property("shown").toBool());
        QCOMPARE(toast()->property("artist").toString(), u"Volume step"_s);
        QCOMPARE(toast()->property("title").toString(), u"set failed"_s);
        QTRY_COMPARE(value(u"audio.volume_step"_s), u"5%"_s);
        QVERIFY(sheetShown());
        key(Qt::Key_Escape);
    }

    void undervoltDeeperFromSheet()
    {
        fake->refuse(u"power.gpu_undervolt"_s, u"net.seilent.MicroSet1.Error.HoldPending"_s);
        open(u"power"_s);
        focus(u"power.gpu_undervolt"_s);
        QCOMPARE(value(u"power.gpu_undervolt"_s), u"Off"_s);
        key(Qt::Key_Return);
        QCOMPARE(sheet()->property("items").toStringList(), (QStringList{u"Off"_s, u"Low"_s}));
        key(Qt::Key_Down);
        QTest::qWait(100);
        QVERIFY(fake->sets().isEmpty());
        key(Qt::Key_Return);
        QVERIFY(!sheetShown());
        QTRY_COMPARE(fake->sets().size(), 1);
        QCOMPARE(fake->sets().at(0).second, QVariant(u"low"_s));
        QTRY_COMPARE(value(u"power.gpu_undervolt"_s), u"Off"_s);
        QTest::qWait(200);
        QVERIFY(!toast()->property("shown").toBool());
        fake->emitHoldRefused(u"power.gpu_undervolt"_s, u"A not held"_s);
        QTRY_VERIFY(toast()->property("shown").toBool());
        QCOMPARE(toast()->property("artist").toString(), u"GPU undervolt"_s);
        QCOMPARE(toast()->property("title").toString(), u"A not held"_s);
        fake->refuse(u"power.gpu_undervolt"_s, u"org.freedesktop.DBus.Error.LimitsExceeded"_s);
        key(Qt::Key_Return);
        key(Qt::Key_Down);
        key(Qt::Key_Return);
        QTRY_COMPARE(fake->sets().size(), 2);
        QTRY_COMPARE(toast()->property("title").toString(), u"try again shortly"_s);
        QCOMPARE(toast()->property("artist").toString(), u"GPU undervolt"_s);
    }

    void echoHeldDuringSheet()
    {
        fake->setHold(true);
        open(u"audio"_s);
        focus(u"audio.volume_step"_s);
        key(Qt::Key_Return);
        key(Qt::Key_Down);
        key(Qt::Key_Down);
        QTRY_COMPARE(value(u"audio.volume_step"_s), u"10%"_s);
        QTRY_COMPARE(fake->sets().size(), 2);
        fake->emitChanged(u"audio.volume_step"_s, 5u);
        fake->release(1);
        QTest::qWait(100);
        QCOMPARE(value(u"audio.volume_step"_s), u"10%"_s);
        fake->release();
        QTest::qWait(100);
        QCOMPARE(value(u"audio.volume_step"_s), u"10%"_s);
        QVERIFY(sheetShown());
        key(Qt::Key_Return);
    }

    QObject *swatchSheet() const
    {
        return qvariant_cast<QObject *>(page()->property("swatchSheet"));
    }

    void ledHueSheet()
    {
        fake->setStored({{u"rgb.enabled"_s, true}});
        model->load();
        QTRY_COMPARE(model->rows(u"stick leds"_s, 0).size(), 5);
        QTRY_VERIFY(!model->rows(u"stick leds"_s, 0).at(2).toMap().value(u"readOnly"_s).toBool());
        open(u"stick leds"_s);
        focus(u"rgb.hue"_s);
        QCOMPARE(row(u"rgb.hue"_s).value(u"swatch"_s).toString(), u"#ff0000"_s);
        QCOMPARE(row(u"rgb.hue"_s).value(u"pick"_s).toString(), u"swatch"_s);
        key(Qt::Key_Return);
        QVERIFY(swatchSheet()->property("shown").toBool());
        QVERIFY(!sheetShown());
        QVERIFY(!page()->property("editing").toBool());
        const QStringList colors = swatchSheet()->property("colors").toStringList();
        QCOMPARE(colors.size(), 36);
        QCOMPARE(colors.first(), u"#ff0000"_s);
        QCOMPARE(swatchSheet()->property("focusIndex").toInt(), 0);
        key(Qt::Key_Down);
        QTRY_COMPARE(fake->calls(), QStringList{u"Override rgb.hue 10"_s});
        QTRY_COMPARE(row(u"rgb.hue"_s).value(u"swatch"_s).toString(), u"#ff2b00"_s);
        key(Qt::Key_Escape);
        QVERIFY(!swatchSheet()->property("shown").toBool());
        QTRY_COMPARE(fake->calls().size(), 2);
        QCOMPARE(fake->calls().at(1), u"ClearOverride rgb.hue"_s);
        QTRY_COMPARE(row(u"rgb.hue"_s).value(u"swatch"_s).toString(), u"#ff0000"_s);
        key(Qt::Key_Return);
        key(Qt::Key_Down);
        key(Qt::Key_Return);
        QTRY_COMPARE(fake->calls().size(), 5);
        QCOMPARE(fake->calls().mid(2), (QStringList{u"Override rgb.hue 10"_s, u"Set rgb.hue 10"_s,
                                                    u"ClearOverride rgb.hue"_s}));
    }

    void accentSheet()
    {
        QObject *ui = engine->singletonInstance<QObject *>("MicroSet", "Ui");
        QVERIFY(ui);
        fake->setSchema(FakeMicroSetd::describe({FakeMicroSetd::shipped(), FakeMicroSetd::fixture(u"60-microdap.json"_s)}));
        fake->setValues({{u"microdap.accent_hue"_s, 120u}});
        model->load();
        QTRY_COMPARE(model->accentHue(), 120);
        QColor h120;
        QVERIFY(QMetaObject::invokeMethod(ui, "accentForHue", Q_RETURN_ARG(QColor, h120), Q_ARG(int, 120)));
        QTRY_COMPARE(ui->property("accent").value<QColor>(), h120);
        open(u"display"_s);
        focus(u"microdap.accent_hue"_s);
        key(Qt::Key_Return);
        QVERIFY(swatchSheet()->property("shown").toBool());
        QCOMPARE(swatchSheet()->property("colors").toList().size(), 37);
        QCOMPARE(swatchSheet()->property("focusIndex").toInt(), 13);
        key(Qt::Key_Down);
        QCOMPARE(swatchSheet()->property("focusIndex").toInt(), 14);
        QTest::qWait(100);
        QVERIFY(fake->calls().isEmpty());
        QCOMPARE(ui->property("accent").value<QColor>(), h120);
        key(Qt::Key_Return);
        QTRY_COMPARE(fake->calls(), QStringList{u"Set microdap.accent_hue 130"_s});
        QTRY_COMPARE(model->accentHue(), 130);
        key(Qt::Key_Return);
        QCOMPARE(swatchSheet()->property("focusIndex").toInt(), 14);
        for (int i = 0; i < 23; ++i)
            key(Qt::Key_Down);
        QCOMPARE(swatchSheet()->property("focusIndex").toInt(), 0);
        key(Qt::Key_Return);
        QTRY_COMPARE(fake->calls(), (QStringList{u"Set microdap.accent_hue 130"_s, u"Set microdap.accent_off true"_s}));
        model->load();
        QTRY_VERIFY(model->accentOff());
        QTRY_VERIFY(ui->property("accentOff").toBool());
        QCOMPARE(model->accentHue(), 130);
        QTRY_COMPARE(row(u"microdap.accent_hue"_s).value(u"swatch"_s).toString(), u"#ffffff"_s);
        key(Qt::Key_Return);
        QCOMPARE(swatchSheet()->property("focusIndex").toInt(), 0);
        key(Qt::Key_Down);
        key(Qt::Key_Return);
        QTRY_COMPARE(fake->calls().size(), 4);
        QCOMPARE(fake->calls().mid(2), (QStringList{u"Set microdap.accent_off false"_s, u"Set microdap.accent_hue 0"_s}));
        QTRY_VERIFY(!ui->property("accentOff").toBool());
        key(Qt::Key_Return);
        key(Qt::Key_Down);
        key(Qt::Key_Escape);
        QTest::qWait(100);
        QCOMPARE(fake->calls().size(), 4);
    }

    void accentOffRefused()
    {
        QObject *ui = engine->singletonInstance<QObject *>("MicroSet", "Ui");
        QVERIFY(ui);
        fake->setSchema(FakeMicroSetd::describe({FakeMicroSetd::shipped(), FakeMicroSetd::fixture(u"60-microdap.json"_s)}));
        fake->setValues({{u"microdap.accent_hue"_s, 120u}});
        fake->refuse(u"microdap.accent_off"_s, u"net.seilent.MicroSet1.Error.InvalidValue"_s);
        model->load();
        QTRY_COMPARE(model->accentHue(), 120);
        open(u"display"_s);
        focus(u"microdap.accent_hue"_s);
        key(Qt::Key_Return);
        QCOMPARE(swatchSheet()->property("focusIndex").toInt(), 13);
        for (int i = 0; i < 13; ++i)
            key(Qt::Key_Up);
        QCOMPARE(swatchSheet()->property("focusIndex").toInt(), 0);
        QSignalSpy flips(ui, SIGNAL(accentOffChanged()));
        key(Qt::Key_Return);
        QTRY_VERIFY(toast()->property("shown").toBool());
        QCOMPARE(toast()->property("artist").toString(), u"Accent"_s);
        QCOMPARE(toast()->property("title").toString(), u"set failed"_s);
        QTRY_COMPARE(flips.count(), 2);
        QCOMPARE(fake->calls(), QStringList{u"Set microdap.accent_off true"_s});
        QTRY_VERIFY(!model->accentOff());
        QTRY_VERIFY(!ui->property("accentOff").toBool());
        QTRY_COMPARE(row(u"microdap.accent_hue"_s).value(u"swatch"_s).toString(), QString());
        QCOMPARE(model->accentHue(), 120);
    }

    void accentPickWithoutDaemon()
    {
        fake->setSchema(FakeMicroSetd::describe({FakeMicroSetd::shipped(), FakeMicroSetd::fixture(u"60-microdap.json"_s)}));
        fake->setValues({{u"microdap.accent_hue"_s, 120u}});
        model->load();
        QTRY_COMPARE(model->accentHue(), 120);
        fake->stop();
        QTRY_VERIFY(!model->available());
        const qsizetype calls = fake->calls().size();
        QVERIFY(QMetaObject::invokeMethod(model.get(), "pickAccent", Q_ARG(int, -1)));
        QTRY_VERIFY(toast()->property("shown").toBool());
        QCOMPARE(toast()->property("artist").toString(), u"Accent"_s);
        QCOMPARE(toast()->property("title").toString(), u"not available"_s);
        QVERIFY(!model->accentOff());
        QCOMPARE(model->accentHue(), 120);
        QTest::qWait(100);
        QCOMPARE(fake->calls().size(), calls);
    }

    void accentSheetWithoutOffKey()
    {
        fake->setSchema(FakeMicroSetd::describe({FakeMicroSetd::shipped(), FakeMicroSetd::fixture(u"50-microdap.json"_s)}));
        fake->setValues({{u"microdap.accent_hue"_s, 120u}});
        model->load();
        QTRY_COMPARE(model->accentHue(), 120);
        open(u"display"_s);
        focus(u"microdap.accent_hue"_s);
        key(Qt::Key_Return);
        QVERIFY(swatchSheet()->property("shown").toBool());
        QCOMPARE(swatchSheet()->property("colors").toList().size(), 36);
        QCOMPARE(swatchSheet()->property("focusIndex").toInt(), 12);
        key(Qt::Key_Escape);
    }

    void ledLockFollowsSwitch()
    {
        fake->setStored({{u"rgb.enabled"_s, false}});
        fake->setValues({{u"rgb.enabled"_s, false}});
        model->load();
        QTRY_COMPARE(model->rows(u"stick leds"_s, model->revision()).size(), 5);
        open(u"stick leds"_s);
        QVERIFY(row(u"rgb.hue"_s).value(u"readOnly"_s).toBool());
        focus(u"rgb.enabled"_s);
        key(Qt::Key_Return);
        QVERIFY(sheetShown());
        key(Qt::Key_Down);
        QTRY_COMPARE(fake->calls(), QStringList{u"Override rgb.enabled true"_s});
        key(Qt::Key_Return);
        QTRY_COMPARE(fake->calls(), (QStringList{u"Override rgb.enabled true"_s, u"Set rgb.enabled true"_s,
                                                 u"ClearOverride rgb.enabled"_s}));
        QTRY_VERIFY(!row(u"rgb.hue"_s).value(u"readOnly"_s).toBool());
        QVERIFY(!row(u"rgb.saturation"_s).value(u"readOnly"_s).toBool());
        QTRY_VERIFY(!model->busy(u"rgb.enabled"_s));
        key(Qt::Key_Return);
        QVERIFY(sheetShown());
        key(Qt::Key_Up);
        key(Qt::Key_Return);
        QTRY_COMPARE(fake->calls().mid(3), (QStringList{u"Override rgb.enabled false"_s, u"Set rgb.enabled false"_s,
                                                        u"ClearOverride rgb.enabled"_s}));
        QTRY_VERIFY(row(u"rgb.hue"_s).value(u"readOnly"_s).toBool());
        QCOMPARE(value(u"rgb.hue"_s), u"off"_s);
    }

    void ledSwatchFollowsSaturation()
    {
        fake->setStored({{u"rgb.enabled"_s, true}, {u"rgb.hue"_s, 200u}, {u"rgb.saturation"_s, 60u}});
        fake->setValues({{u"rgb.enabled"_s, true}, {u"rgb.hue"_s, 200u}, {u"rgb.saturation"_s, 60u}});
        model->load();
        QTRY_VERIFY(!model->rows(u"stick leds"_s, model->revision()).at(2).toMap().value(u"readOnly"_s).toBool());
        open(u"stick leds"_s);
        QTRY_COMPARE(row(u"rgb.hue"_s).value(u"swatch"_s).toString(), u"#66ccff"_s);
        fake->setStored({{u"rgb.enabled"_s, true}, {u"rgb.hue"_s, 200u}, {u"rgb.saturation"_s, 0u}});
        fake->emitChanged(u"rgb.saturation"_s, 0u);
        QTRY_COMPARE(row(u"rgb.hue"_s).value(u"swatch"_s).toString(), u"#ffffff"_s);
    }

    void balanceAdjusts()
    {
        open(u"audio"_s);
        focus(u"audio.balance.headphones"_s);
        key(Qt::Key_Return);
        QVERIFY(page()->property("editing").toBool());
        QVERIFY(adjust()->property("shown").toBool());
        QVERIFY(!sheetShown());
        key(Qt::Key_Right);
        key(Qt::Key_Right);
        QCOMPARE(value(u"audio.balance.headphones"_s), u"r 1 db"_s);
        QTRY_COMPARE(adjustNumber(0), u"-1.0"_s);
        QCOMPARE(adjustNumber(1), u"0.0"_s);
        QVERIFY(adjustLit(0, "plusBox"));
        QVERIFY(adjustLit(0, "minusBox"));
        QVERIFY(!adjustLit(1, "plusBox"));
        QVERIFY(!adjustLit(1, "minusBox"));
        key(Qt::Key_Up);
        key(Qt::Key_Down);
        QCOMPARE(value(u"audio.balance.headphones"_s), u"r 1 db"_s);
        QTest::qWait(100);
        QVERIFY(fake->calls().isEmpty());
        key(Qt::Key_Return);
        QVERIFY(!page()->property("editing").toBool());
        QVERIFY(!adjust()->property("shown").toBool());
        QTRY_COMPARE(fake->calls(), QStringList{u"Set audio.balance.headphones 2"_s});
        key(Qt::Key_Return);
        key(Qt::Key_Left);
        key(Qt::Key_Escape);
        QVERIFY(!page()->property("editing").toBool());
        QCOMPARE(value(u"audio.balance.headphones"_s), u"r 1 db"_s);
        QTest::qWait(100);
        QCOMPARE(fake->calls().size(), 1);
    }

    void balanceEnds()
    {
        fake->setValues({{u"audio.balance.headphones"_s, -20}});
        model->load();
        open(u"audio"_s);
        QTRY_COMPARE(value(u"audio.balance.headphones"_s), u"l 10 db"_s);
        focus(u"audio.balance.headphones"_s);
        key(Qt::Key_Return);
        QCOMPARE(adjustNumber(0), u"0.0"_s);
        QCOMPARE(adjustNumber(1), u"-10.0"_s);
        QVERIFY(!adjustLit(0, "plusBox"));
        QVERIFY(!adjustLit(0, "minusBox"));
        QVERIFY(adjustLit(1, "plusBox"));
        QVERIFY(!adjustLit(1, "minusBox"));
        key(Qt::Key_Left);
        QCOMPARE(value(u"audio.balance.headphones"_s), u"l 10 db"_s);
        key(Qt::Key_Right);
        QCOMPARE(value(u"audio.balance.headphones"_s), u"l 9.5 db"_s);
        QCOMPARE(adjustNumber(1), u"-9.5"_s);
        QVERIFY(adjustLit(1, "minusBox"));
        key(Qt::Key_Escape);
        fake->setValues({{u"audio.balance.headphones"_s, 20}});
        model->load();
        QTRY_COMPARE(value(u"audio.balance.headphones"_s), u"r 10 db"_s);
        key(Qt::Key_Return);
        QCOMPARE(adjustNumber(0), u"-10.0"_s);
        QVERIFY(adjustLit(0, "plusBox"));
        QVERIFY(!adjustLit(0, "minusBox"));
        QVERIFY(!adjustLit(1, "plusBox"));
        QVERIFY(!adjustLit(1, "minusBox"));
        key(Qt::Key_Escape);
        QTest::qWait(100);
        QVERIFY(fake->calls().isEmpty());
    }

    void adjustNumber()
    {
        fake->setSchema(FakeMicroSetd::describe({FakeMicroSetd::shipped(), FakeMicroSetd::fixture(u"50-microemu.json"_s)}));
        model->load();
        QTRY_VERIFY(!model->rows(u"emulation"_s, model->revision()).isEmpty());
        open(u"emulation"_s);
        focus(u"microemu.retroarch.savestate_max_keep"_s);
        QCOMPARE(row(u"microemu.retroarch.savestate_max_keep"_s).value(u"pick"_s).toString(), u"adjust"_s);
        key(Qt::Key_Return);
        QVERIFY(adjust()->property("shown").toBool());
        QCOMPARE(adjustNumber(0), u"10"_s);
        QCOMPARE(qvariant_cast<QQuickItem *>(adjust()->property("line1"))->property("text").toString(), u"States kept"_s);
        QCOMPARE(qvariant_cast<QQuickItem *>(adjust()->property("line2"))->property("text").toString(), u"10"_s);
        key(Qt::Key_Up);
        QTRY_COMPARE(fake->calls(), QStringList{u"Set microemu.retroarch.savestate_max_keep 11"_s});
        QTRY_COMPARE(adjustNumber(0), u"11"_s);
        key(Qt::Key_Escape);
        QTRY_COMPARE(fake->calls(), (QStringList{u"Set microemu.retroarch.savestate_max_keep 11"_s,
                                                 u"Set microemu.retroarch.savestate_max_keep 10"_s}));
        QVERIFY(!adjust()->property("shown").toBool());
    }

    void boolOpensSheet()
    {
        QObject *ui = engine->singletonInstance<QObject *>("MicroSet", "Ui");
        QVERIFY(ui);
        open(u"display"_s);
        focus(u"display.orientation"_s);
        QVERIFY(row(u"display.orientation"_s).value(u"readOnly"_s).toBool());
        key(Qt::Key_Return);
        QVERIFY(!sheetShown());
        focus(u"display.auto_rotate"_s);
        QCOMPARE(row(u"display.auto_rotate"_s).value(u"pick"_s).toString(), u"sheet"_s);
        key(Qt::Key_Return);
        QVERIFY(sheetShown());
        QCOMPARE(sheet()->property("items").toStringList(), (QStringList{u"off"_s, u"on"_s}));
        QCOMPARE(sheetFocus(), 1);
        QCOMPARE(sheet()->property("mark").toInt(), 1);
        key(Qt::Key_Up);
        QCOMPARE(sheetFocus(), 0);
        QTest::qWait(100);
        QVERIFY(fake->calls().isEmpty());
        key(Qt::Key_Return);
        QVERIFY(!sheetShown());
        QTRY_COMPARE(fake->calls(), QStringList{u"Set display.auto_rotate false"_s});
        QTRY_VERIFY(!row(u"display.orientation"_s).value(u"readOnly"_s).toBool());
        QCOMPARE(value(u"display.auto_rotate"_s), u"off"_s);
        open(u"audio"_s);
        focus(u"audio.eq.headphones.on"_s);
        key(Qt::Key_Return);
        QCOMPARE(sheetFocus(), 0);
        key(Qt::Key_Down);
        QTRY_COMPARE(fake->calls().size(), 2);
        QCOMPARE(fake->calls().last(), u"Set audio.eq.headphones.on true"_s);
        key(Qt::Key_Escape);
        QVERIFY(!sheetShown());
        QTRY_COMPARE(fake->calls().size(), 3);
        QCOMPARE(fake->calls().last(), u"Set audio.eq.headphones.on false"_s);
        QTRY_VERIFY(!model->busy(u"audio.eq.headphones.on"_s));
        key(Qt::Key_Return);
        key(Qt::Key_Down);
        QTRY_COMPARE(fake->calls().size(), 4);
        ui->setProperty("turn", 1);
        QVERIFY(!sheetShown());
        ui->setProperty("turn", 0);
        QTest::qWait(100);
        QCOMPARE(fake->calls().size(), 4);
        QCOMPARE(fake->calls().last(), u"Set audio.eq.headphones.on true"_s);
        QTRY_COMPARE(value(u"audio.eq.headphones.on"_s), u"on"_s);
    }

    void longValueCut()
    {
        fake->setValues({{u"audio.eq.headphones.name"_s,
                          u"Sennheiser HD 600 ParametricEQ oratory1990 Harman over-ear 2018 target"_s}});
        model->load();
        open(u"audio"_s);
        QTRY_COMPARE(value(u"audio.eq.headphones.name"_s).isEmpty(), false);
        int index = -1;
        QVERIFY(QMetaObject::invokeMethod(page(), "indexOfKey", Q_RETURN_ARG(int, index),
                                          Q_ARG(QString, u"audio.eq.headphones.name"_s)));
        QQuickItem *rowItem = nullptr;
        QVERIFY(QMetaObject::invokeMethod(qvariant_cast<QObject *>(page()->property("repeater")), "itemAt",
                                          Q_RETURN_ARG(QQuickItem *, rowItem), Q_ARG(int, index)));
        QVERIFY(rowItem);
        auto *title = qvariant_cast<QQuickItem *>(rowItem->property("titleItem"));
        auto *valueItem = qvariant_cast<QQuickItem *>(rowItem->property("valueItem"));
        QVERIFY(title && valueItem);
        QVERIFY(title->parentItem()->width() - title->x() >= title->implicitWidth());
        const qreal room = valueItem->property("room").toReal();
        QVERIFY(room > 0);
        QVERIFY(valueItem->implicitWidth() > room);
        QCOMPARE(valueItem->width(), room);
        QVERIFY(valueItem->clip());
        QCOMPARE(valueItem->property("elide").toInt(), int(Qt::ElideNone));
        int outputIndex = -1;
        QVERIFY(QMetaObject::invokeMethod(page(), "indexOfKey", Q_RETURN_ARG(int, outputIndex),
                                          Q_ARG(QString, u"audio.output"_s)));
        QQuickItem *outputRow = nullptr;
        QVERIFY(QMetaObject::invokeMethod(qvariant_cast<QObject *>(page()->property("repeater")), "itemAt",
                                          Q_RETURN_ARG(QQuickItem *, outputRow), Q_ARG(int, outputIndex)));
        auto *outputValue = qvariant_cast<QQuickItem *>(outputRow->property("valueItem"));
        QCOMPARE(valueItem->x() + valueItem->width(), outputValue->x() + outputValue->width());
        QVERIFY(title->x() + title->width() < valueItem->x());
    }

    void tuningFallbackHidesTrip()
    {
        fake->setValues({{u"power.gpu_undervolt"_s, u"off"_s}, {u"power.gpu_undervolt_active"_s, u"stock"_s},
                         {u"power.gpu_tuning_fallback"_s, u"start"_s}, {u"power.gpu_tuning_trip"_s, u"start"_s}});
        model->load();
        open(u"power"_s);
        QTRY_COMPARE(value(u"power.gpu_tuning_fallback"_s), u"GPU failed to start"_s);
        QVERIFY(row(u"power.gpu_tuning_trip"_s).isEmpty());
        focus(u"power.gpu_undervolt"_s);
        QVERIFY(QMetaObject::invokeMethod(page(), "keepFocusVisible"));
        key(Qt::Key_Down);
        reachable(u"power.gpu_tuning_fallback"_s);
        key(Qt::Key_Down);
        reachable(u"power.gpu_tuning_fallback"_s);
        fake->emitChanged(u"power.gpu_tuning_fallback"_s, QString());
        QTRY_COMPARE(value(u"power.gpu_tuning_trip"_s), u"GPU failed to start"_s);
        QVERIFY(row(u"power.gpu_tuning_fallback"_s).isEmpty());
        focus(u"power.gpu_undervolt"_s);
        QVERIFY(QMetaObject::invokeMethod(page(), "keepFocusVisible"));
        key(Qt::Key_Down);
        reachable(u"power.gpu_tuning_trip"_s);
    }

    void microsetdGoneAndBack()
    {
        open(u"audio"_s);
        focus(u"audio.volume_step"_s);
        key(Qt::Key_Return);
        QVERIFY(sheetShown());
        fake->stop();
        QTRY_VERIFY(!model->available());
        QVERIFY(!sheetShown());
        QVERIFY(!page()->property("valueOpen").toBool());
        QCOMPARE(value(u"audio.volume_step"_s), u"5%"_s);
        key(Qt::Key_Return);
        QVERIFY(!sheetShown());
        QTRY_VERIFY(toast()->property("shown").toBool());
        QCOMPARE(toast()->property("title").toString(), u"not available"_s);
        fake->setValues({{u"audio.volume_step"_s, 10u}});
        QVERIFY(fake->start());
        QTRY_VERIFY(model->available());
        QTRY_COMPARE(value(u"audio.volume_step"_s), u"10%"_s);
        key(Qt::Key_Return);
        QVERIFY(sheetShown());
        QCOMPARE(sheetFocus(), 0);
        QVERIFY(fake->calls().isEmpty());
    }

    void copiedTokensResolve()
    {
        QObject *ui = engine->singletonInstance<QObject *>("MicroSet", "Ui");
        const QHash<QString, QObject *> singletons{
            {u"Ui"_s, ui},
            {u"ui"_s, ui},
            {u"Motion"_s, engine->singletonInstance<QObject *>("MicroSet", "Motion")},
        };
        static const QRegularExpression token(u"\\b(Ui|Motion|ui)\\.([A-Za-z_]\\w*)"_s);
        int found = 0;
        for (const QString &name : {u"components/Keyboard.qml"_s, u"components/ListRow.qml"_s, u"components/Ring.qml"_s,
                                    u"components/Glyph.qml"_s, u"GlyphPaths.js"_s, u"JumpGrid.qml"_s,
                                    u"components/JumpLetter.qml"_s, u"components/LetterTile.qml"_s,
                                    u"components/Dialog.qml"_s, u"components/Toast.qml"_s, u"Ui.qml"_s}) {
            QFile file(QStringLiteral(MICROSET_SOURCE_DIR "/qml/MicroSet/") + name);
            QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(name));
            for (const QRegularExpressionMatch &m : token.globalMatch(QString::fromUtf8(file.readAll()))) {
                QObject *singleton = singletons.value(m.captured(1));
                QVERIFY(singleton);
                const QMetaObject *meta = singleton->metaObject();
                const QByteArray member = m.captured(2).toUtf8();
                bool known = meta->indexOfProperty(member.constData()) >= 0;
                for (int i = 0; !known && i < meta->methodCount(); ++i)
                    known = meta->method(i).name() == member;
                QVERIFY2(known, qPrintable(u"%1.%2 in %3"_s.arg(m.captured(1), m.captured(2), name)));
                ++found;
            }
        }
        QVERIFY(found > 0);
    }

    void accentOffTokens()
    {
        QObject *ui = engine->singletonInstance<QObject *>("MicroSet", "Ui");
        QVERIFY(ui);
        const auto color = [ui](const char *name) { return ui->property(name).value<QColor>(); };
        QCOMPARE(ui->property("accentOff").toBool(), false);
        QCOMPARE(color("accent"), QColor(u"#fa2a4d"_s));
        ui->setProperty("accentOff", true);
        QCOMPARE(color("accent"), QColor(Qt::white));
        QCOMPARE(color("accentBright"), QColor(Qt::white));
        QCOMPARE(color("accentHueColor"), QColor(u"#fa2a4d"_s));
        const auto mapOf = [](const QVariant &role) {
            return role.metaType() == QMetaType::fromType<QJSValue>() ? role.value<QJSValue>().toVariant().toMap()
                                                                      : role.toMap();
        };
        QVariant value;
        QVERIFY(QMetaObject::invokeMethod(ui, "accentBold", Q_RETURN_ARG(QVariant, value),
                                          Q_ARG(QVariant, ui->property("settingsValue"))));
        QCOMPARE(mapOf(value).value(u"style"_s).toString(), u"Bold"_s);
        QCOMPARE(mapOf(value).value(u"size"_s).toInt(), 11);
        QCOMPARE(ui->property("settingsValue").toMap().value(u"style"_s).toString(), u"Light"_s);
        const QVariant item = ui->property("dialogItem");
        QVERIFY(QMetaObject::invokeMethod(ui, "accentBold", Q_RETURN_ARG(QVariant, value), Q_ARG(QVariant, item)));
        QCOMPARE(mapOf(value), item.toMap());
        ui->setProperty("accentOff", false);
        QCOMPARE(color("accent"), QColor(u"#fa2a4d"_s));
        const QColor scrim = color("scrimDialog");
        QCOMPARE(scrim.red(), 17);
        QCOMPARE(scrim.green(), 17);
        QCOMPARE(scrim.blue(), 17);
        QVERIFY(qAbs(scrim.alphaF() - 0.96) < 1.0 / 255);
    }

    void accentOffBindsUi()
    {
        QObject *ui = engine->singletonInstance<QObject *>("MicroSet", "Ui");
        QVERIFY(ui);
        fake->setSchema(FakeMicroSetd::describe({FakeMicroSetd::shipped(), FakeMicroSetd::fixture(u"60-microdap.json"_s)}));
        fake->setValues({{u"microdap.accent_hue"_s, 120u}, {u"microdap.accent_off"_s, true}});
        model->load();
        QTRY_VERIFY(ui->property("accentOff").toBool());
        QCOMPARE(ui->property("accent").value<QColor>(), QColor(Qt::white));
        fake->emitChanged(u"microdap.accent_off"_s, false);
        QTRY_VERIFY(!ui->property("accentOff").toBool());
        QColor hue;
        QVERIFY(QMetaObject::invokeMethod(ui, "accentForHue", Q_RETURN_ARG(QColor, hue), Q_ARG(int, 120)));
        QCOMPARE(ui->property("accent").value<QColor>(), hue);
    }

    void copiedComponentsLoad()
    {
        QTest::failOnWarning(QRegularExpression(u"Setting initial properties failed|Could not set initial property"_s));
        std::vector<std::unique_ptr<QQuickItem>> items;
        QString error;
        const auto make = [&](QAnyStringView type, QVariantMap props) -> QQuickItem * {
            QQmlComponent component(engine.get());
            component.loadFromModule("MicroSet", type);
            auto *item = qobject_cast<QQuickItem *>(component.createWithInitialProperties(props));
            if (!item) {
                error = component.errorString();
                return nullptr;
            }
            item->setParentItem(window->contentItem());
            items.emplace_back(item);
            return item;
        };
        const QVariantMap full{{u"width"_s, window->width()}, {u"height"_s, window->height()}};

        QQuickItem *row = make("ListRow", {{u"primary"_s, u"game"_s}, {u"secondary"_s, u"query"_s},
                                           {u"number"_s, u"12"_s}, {u"bold"_s, true}, {u"focused"_s, true},
                                           {u"ring"_s, true}, {u"ringGlyph"_s, u"search"_s}});
        QVERIFY2(row, qPrintable(error));
        auto *ring = qvariant_cast<QQuickItem *>(row->property("ringItem"));
        QVERIFY(ring);
        auto *icon = qvariant_cast<QObject *>(ring->property("iconItem"));
        QVERIFY(icon);
        QVERIFY(icon->property("known").toBool());
        QVERIFY(row->property("textX").toReal() > ring->parentItem()->x());

        QQuickItem *glyph = make("Glyph", {{u"name"_s, u"search"_s}});
        QVERIFY2(glyph, qPrintable(error));
        QVERIFY(glyph->property("known").toBool());
        QQuickItem *toggle = make("Ring", {{u"glyph"_s, u"play"_s}, {u"focused"_s, true}, {u"toggle"_s, true},
                                           {u"on"_s, true}});
        QVERIFY2(toggle, qPrintable(error));
        QQuickItem *tile = make("LetterTile", {{u"letter"_s, u"m"_s}, {u"focused"_s, true}});
        QVERIFY2(tile, qPrintable(error));

        QQuickItem *grid = make("JumpGrid", full);
        QVERIFY2(grid, qPrintable(error));
        const QVariant available = QVariantList{u"a"_s, u"m"_s};
        QVERIFY(QMetaObject::invokeMethod(grid, "open", Q_ARG(QVariant, available), Q_ARG(QString, u"m"_s)));
        QVERIFY(QMetaObject::invokeMethod(grid, "complete"));
        QVERIFY(grid->property("shown").toBool());
        QCOMPARE(grid->property("focusLetter").toString(), u"m"_s);

        QQuickItem *letter = make("JumpLetter", full);
        QVERIFY2(letter, qPrintable(error));
        QVERIFY(QMetaObject::invokeMethod(letter, "show", Q_ARG(QString, u"m"_s)));
        QVERIFY(letter->property("shown").toBool());

        QVariantMap dialogProps = full;
        dialogProps.insert(u"title"_s, u"discard text?"_s);
        dialogProps.insert(u"items"_s, QStringList{u"discard"_s, u"keep"_s});
        QQuickItem *dialog = make("Dialog", dialogProps);
        QVERIFY2(dialog, qPrintable(error));
        QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
        QVERIFY(QMetaObject::invokeMethod(dialog, "complete"));
        dialog->setProperty("focusIndex", 1);

        QQuickItem *keyboard = make("Keyboard", full);
        QVERIFY2(keyboard, qPrintable(error));
        const QVariant request = QVariantMap{{u"title"_s, u"search"_s}, {u"text"_s, QString()}};
        QVERIFY(QMetaObject::invokeMethod(keyboard, "open", Q_ARG(QVariant, request)));
        QVERIFY(QMetaObject::invokeMethod(keyboard, "complete"));
        bool handled = false;
        for (const QString &action : {u"right"_s, u"a"_s, u"left"_s, u"a"_s}) {
            QVERIFY(QMetaObject::invokeMethod(keyboard, "handleAction", Q_RETURN_ARG(bool, handled),
                                              Q_ARG(QString, action), Q_ARG(bool, false)));
            QVERIFY(handled);
        }
        auto *entry = qvariant_cast<QObject *>(keyboard->property("entry"));
        QVERIFY(entry);
        QCOMPARE(entry->property("text").toString(), u"b"_s);
        QVERIFY(QMetaObject::invokeMethod(keyboard, "handleAction", Q_RETURN_ARG(bool, handled), Q_ARG(QString, u"b"_s),
                                          Q_ARG(bool, false)));
        auto *discard = qvariant_cast<QObject *>(keyboard->property("discard"));
        QVERIFY(discard);
        QVERIFY(discard->property("shown").toBool());
        QCOMPARE(discard->property("focusIndex").toInt(), 1);
    }

    void touchOpensGroupPage()
    {
        tapRow(u"group-audio"_s);
        QTRY_COMPARE(page()->property("group").toString(), u"audio"_s);
        QCOMPARE(qvariant_cast<QObject *>(view->property("stack"))->property("depth").toInt(), 2);
    }

    void touchDuringPushOpensNothing()
    {
        QObject *stack = qvariant_cast<QObject *>(view->property("stack"));
        stack->setProperty("animate", true);
        const QPoint at = centerOf(rowItem(u"group-display"_s));
        tapRow(u"group-audio"_s);
        QTRY_COMPARE(stack->property("depth").toInt(), 2);
        QVERIFY(stack->property("inputLocked").toBool());
        tap(at);
        QTRY_VERIFY(!stack->property("inputLocked").toBool());
        QTest::qWait(100);
        QCOMPARE(stack->property("depth").toInt(), 2);
        QCOMPARE(page()->property("group").toString(), u"audio"_s);
        QVERIFY(!sheetShown());
        QVERIFY(!page()->property("valueOpen").toBool());
    }

    void touchRowOpensItsSheet()
    {
        open(u"audio"_s);
        show(u"audio.volume_step"_s, u"audio.output"_s);
        tapRow(u"audio.volume_step"_s);
        QTRY_VERIFY(sheetShown());
        QCOMPARE(focusedKey(), u"audio.volume_step"_s);
        QCOMPARE(sheet()->property("title").toString(), u"Volume step"_s);
        QCOMPARE(sheetFocus(), 1);
    }

    void touchReadOnlyRowFocuses()
    {
        open(u"display"_s);
        QVERIFY(row(u"display.orientation"_s).value(u"readOnly"_s).toBool());
        show(u"display.orientation"_s, u"display.brightness"_s);
        tapRow(u"display.orientation"_s);
        QTRY_COMPARE(focusedKey(), u"display.orientation"_s);
        QTest::qWait(100);
        QVERIFY(!sheetShown());
        QVERIFY(!page()->property("valueOpen").toBool());
    }

    void touchItemApplies()
    {
        open(u"audio"_s);
        show(u"audio.volume_step"_s, u"audio.output"_s);
        tapRow(u"audio.volume_step"_s);
        QTRY_VERIFY(sheetShown());
        laidOut(sheet());
        tap(settledPoint(sheet(), 2));
        QTRY_VERIFY(!sheetShown());
        QTRY_COMPARE(fake->stored().value(u"audio.volume_step"_s), QVariant(2u));
        QTRY_VERIFY(!model->busy(u"audio.volume_step"_s));
        show(u"audio.max_volume"_s, u"audio.output"_s);
        tapRow(u"audio.max_volume"_s);
        QTRY_VERIFY(sheetShown());
        QCOMPARE(sheetFocus(), 0);
        laidOut(sheet());
        tap(settledPoint(sheet(), 2));
        QTRY_VERIFY(!sheetShown());
        QTRY_COMPARE(fake->stored().value(u"audio.max_volume"_s), QVariant(90u));
        QCOMPARE(fake->calls().last(), u"Set audio.max_volume 90"_s);
    }

    void touchDragScrollsSheet()
    {
        ledsOn();
        open(u"stick leds"_s);
        show(u"rgb.brightness"_s, u"rgb.brightness"_s);
        key(Qt::Key_Return);
        QVERIFY(sheetShown());
        auto *list = qvariant_cast<QQuickItem *>(sheet()->property("window"));
        QVERIFY(list->property("touchScrollEnabled").toBool());
        const qreal before = list->property("contentY").toReal();
        const int pitch = int(sheet()->property("pitch").toReal());
        laidOut(sheet());
        drag(settledPoint(sheet(), sheetFocus()), QPoint(0, -2 * pitch), 10);
        QTRY_VERIFY(list->property("contentY").toReal() > before);
        QTest::qWait(100);
        QVERIFY(sheetShown());
        QVERIFY(fake->calls().isEmpty());
        QVERIFY(fake->stored().value(u"rgb.brightness"_s).isNull());
    }

    void touchScrimRestores()
    {
        open(u"audio"_s);
        show(u"audio.volume_step"_s, u"audio.volume_step"_s);
        key(Qt::Key_Return);
        key(Qt::Key_Down);
        QTRY_COMPARE(fake->calls(), QStringList{u"Set audio.volume_step 2"_s});
        auto *root = qobject_cast<QQuickItem *>(sheet());
        tap(root->mapToScene(QPointF(root->width() / 2, sheet()->property("statusClear").toReal() / 2)).toPoint());
        QTRY_VERIFY(!sheetShown());
        QTRY_COMPARE(fake->calls(), (QStringList{u"Set audio.volume_step 2"_s, u"Set audio.volume_step 5"_s}));
        QTRY_COMPARE(value(u"audio.volume_step"_s), u"5%"_s);
    }

    void touchSheetTitleTakesNothing()
    {
        open(u"audio"_s);
        show(u"audio.volume_step"_s, u"audio.volume_step"_s);
        key(Qt::Key_Return);
        QVERIFY(sheetShown());
        auto *panel = qvariant_cast<QQuickItem *>(sheet()->property("panel"));
        tap(panel->mapToScene(QPointF(panel->width() / 2, sheet()->property("edge").toReal() / 2)).toPoint());
        QTest::qWait(100);
        QVERIFY(sheetShown());
        QVERIFY(page()->property("valueOpen").toBool());
        QCOMPARE(focusedKey(), u"audio.volume_step"_s);
        QVERIFY(fake->calls().isEmpty());
    }

    void touchDuringRiseApplies()
    {
        open(u"audio"_s);
        page()->setProperty("animated", true);
        show(u"audio.volume_step"_s, u"audio.volume_step"_s);
        key(Qt::Key_Return);
        rising(sheet());
        laidOut(sheet());
        tap(settledPoint(sheet(), 0));
        QTRY_VERIFY(!sheetShown());
        QTRY_COMPARE(fake->stored().value(u"audio.volume_step"_s), QVariant(10u));
    }

    void touchConfirmDialog()
    {
        open(u"display"_s);
        const QVariantMap ask{{u"kind"_s, u"row"_s}, {u"value"_s, QString()}, {u"readOnly"_s, true},
                              {u"widget"_s, QString()}, {u"swatch"_s, QString()}, {u"hue"_s, -1},
                              {u"confirm"_s, false}, {u"pick"_s, QString()}, {u"bounded"_s, false},
                              {u"key"_s, u"test-ask"_s}, {u"title"_s, u"Ask first"_s}, {u"action"_s, u"test-ask"_s},
                              {u"ask"_s, u"ask first"_s}, {u"accept"_s, u"go"_s}};
        page()->setProperty("modelRows", page()->property("modelRows").toList() << ask);
        auto *dialog = qvariant_cast<QQuickItem *>(page()->property("confirm"));
        QSignalSpy acted(page(), SIGNAL(acted(QString)));
        show(u"test-ask"_s, u"display.brightness"_s);
        tapRow(u"test-ask"_s);
        QTRY_VERIFY(dialog->property("shown").toBool());
        QTRY_VERIFY(!dialog->property("animating").toBool());
        tap(dialog->mapToScene(QPointF(dialog->width() / 2, dialog->height() - 2)).toPoint());
        QTRY_VERIFY(!dialog->property("shown").toBool());
        QTest::qWait(100);
        QCOMPARE(acted.count(), 0);
        QTRY_VERIFY(!dialog->property("animating").toBool());
        tapRow(u"test-ask"_s);
        QTRY_VERIFY(dialog->property("shown").toBool());
        QTRY_VERIFY(!dialog->property("animating").toBool());
        tap(centerOf(sheetItem(dialog, 0)));
        QTRY_COMPARE(acted.count(), 1);
        QCOMPARE(acted.takeFirst().value(0).toString(), u"test-ask"_s);
        QVERIFY(!dialog->property("shown").toBool());
    }

    void touchGamesSheet()
    {
        QTemporaryDir steam;
        qputenv("MICROSET_STEAM_DIR", steam.path().toUtf8());
        FakeMicroSetd::writeSteam(steam.path(), {{u"292120"_s, u"FINAL FANTASY XIII"_s}, {u"2238900"_s, u"SO2R"_s}});
        fake->setProton({FakeMicroSetd::shipped()}, FakeMicroSetd::Profiles);
        fake->setValues({{u"app.292120.lsfg.state"_s, u"off"_s}});
        fake->setStored({{u"app.292120.lsfg.state"_s, u"off"_s}});
        model->load();
        QTRY_COMPARE(model->value(u"app.292120.lsfg.state"_s, model->revision()).toString(), u"off"_s);
        writeLsfg(R"({"version": 1, "owned": "yes", "install": "installed"})");
        openLsfg();
        QTRY_COMPARE(value(u"lsfg-games"_s), u"0 on"_s);
        show(u"lsfg-games"_s, u"lsfg-status"_s);
        tapRow(u"lsfg-games"_s);
        QTRY_VERIFY(rowSheet()->property("shown").toBool());
        QTRY_COMPARE(entryKeys(), (QStringList{u"lsfg-game-292120"_s, u"lsfg-add"_s}));
        laidOut(rowSheet());
        tap(settledPoint(rowSheet(), 0));
        QTRY_VERIFY(gameSheet()->property("shown").toBool());
        QCOMPARE(gameSheet()->property("title").toString(), u"FINAL FANTASY XIII"_s);
        laidOut(gameSheet());
        tap(settledPoint(gameSheet(), 1));
        QTRY_COMPARE(fake->calls(), QStringList{u"Set app.292120.lsfg.state on"_s});
        QVERIFY(!gameSheet()->property("shown").toBool());
        QVERIFY(rowSheet()->property("shown").toBool());
        auto *root = qobject_cast<QQuickItem *>(rowSheet());
        tap(root->mapToScene(QPointF(root->width() / 2, rowSheet()->property("statusClear").toReal() / 2)).toPoint());
        QTRY_VERIFY(!rowSheet()->property("shown").toBool());
        QCOMPARE(page()->property("group").toString(), Lsfg);
        QCOMPARE(fake->calls().size(), 1);
        qunsetenv("MICROSET_STEAM_DIR");
    }

    void touchHeaderGoesBack()
    {
        QSignalSpy hide(view, SIGNAL(hideRequested()));
        QObject *stack = qvariant_cast<QObject *>(view->property("stack"));
        open(u"audio"_s);
        QCOMPARE(stack->property("depth").toInt(), 2);
        tap(headerPoint());
        QTRY_COMPARE(stack->property("depth").toInt(), 1);
        QCOMPARE(page()->property("title").toString(), u"settings"_s);
        tap(headerPoint());
        QTest::qWait(100);
        QCOMPARE(stack->property("depth").toInt(), 1);
        QCOMPARE(hide.count(), 0);
    }

    void touchEventOpensSheet()
    {
        static QPointingDevice *device = QTest::createTouchDevice();
        open(u"audio"_s);
        show(u"audio.volume_step"_s, u"audio.output"_s);
        const QPoint at = centerOf(rowItem(u"audio.volume_step"_s));
        QTest::touchEvent(window, device).press(0, at, window);
        QTest::touchEvent(window, device).release(0, at, window);
        QTRY_VERIFY(sheetShown());
        QCOMPARE(sheet()->property("title").toString(), u"Volume step"_s);
    }

    void touchCountsAsAFocusMove()
    {
        engine.reset();
        model = std::make_unique<SettingsModel>(h->client());
        model->lsfg()->setFixture({}, true, u"android"_s);
        model->load();
        startPage();
        QVERIFY(window && view);
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(window));
        QTRY_VERIFY(model->available());
        QTRY_VERIFY(!row(u"perf.android_overlay"_s).isEmpty());
        atTop(u"perf.android_overlay"_s);
        tapRow(u"group-audio"_s);
        QTRY_COMPARE(page()->property("group").toString(), u"audio"_s);
        key(Qt::Key_Escape);
        QTRY_COMPARE(page()->property("title").toString(), u"settings"_s);
        QCOMPARE(focusedKey(), u"group-audio"_s);
        model->setAndroidApp(u"com.example.game"_s);
        QTRY_VERIFY(!row(u"android-app"_s).isEmpty());
        QCOMPARE(focusedKey(), u"group-audio"_s);
    }

    void touchFlingKeepsFocusInView()
    {
        open(u"audio"_s);
        auto *flick = qvariant_cast<QQuickItem *>(page()->property("flick"));
        QObject *scroller = qvariant_cast<QObject *>(flick->property("touch"));
        const QPoint from = flick->mapToScene(QPointF(flick->width() / 2, flick->height() * 3 / 4)).toPoint();
        drag(from, QPoint(0, -int(flick->height() / 2)), 8);
        const qreal released = flick->property("contentY").toReal();
        QVERIFY(released > 0);
        QTRY_VERIFY(flick->property("contentY").toReal() > released);
        QTRY_VERIFY(!scroller->property("flinging").toBool());
        const int index = page()->property("focusIndex").toInt();
        qreal top = 0;
        qreal height = 0;
        QVERIFY(QMetaObject::invokeMethod(page(), "rowTopOf", Q_RETURN_ARG(qreal, top), Q_ARG(int, index)));
        QVERIFY(QMetaObject::invokeMethod(page(), "rowHeightOf", Q_RETURN_ARG(qreal, height),
                                          Q_ARG(QVariant, page()->property("focusedRow"))));
        const qreal y = flick->property("contentY").toReal();
        QVERIFY(top >= y - 0.5 && top + height <= y + flick->height() + 0.5);
    }

    void touchAccentSwatch()
    {
        fake->setSchema(FakeMicroSetd::describe({FakeMicroSetd::shipped(), FakeMicroSetd::fixture(u"60-microdap.json"_s)}));
        fake->setValues({{u"microdap.accent_hue"_s, 120u}});
        model->load();
        QTRY_COMPARE(model->accentHue(), 120);
        open(u"display"_s);
        show(u"microdap.accent_hue"_s, u"microdap.accent_hue"_s);
        key(Qt::Key_Return);
        QVERIFY(swatchSheet()->property("shown").toBool());
        QCOMPARE(swatchSheet()->property("focusIndex").toInt(), 13);
        laidOut(swatchSheet());
        const QPoint from = settledPoint(swatchSheet(), 12);
        const QPoint by(2 * int(swatchSheet()->property("pitch").toReal()), 0);
        drag(from, by, 4);
        QTest::qWait(100);
        QCOMPARE(swatchSheet()->property("focusIndex").toInt(), 13);
        QVERIFY(swatchSheet()->property("shown").toBool());
        QVERIFY(fake->calls().isEmpty());
        QVERIFY(swatchSheet()->property("firstRow").toInt() <= 11);
        tap(settledPoint(swatchSheet(), 11));
        QTRY_VERIFY(!swatchSheet()->property("shown").toBool());
        QTRY_COMPARE(fake->calls(), QStringList{u"Set microdap.accent_hue 100"_s});
    }

    void touchLedHueSwatch()
    {
        fake->setStored({{u"rgb.enabled"_s, true}});
        model->load();
        QTRY_VERIFY(!model->rows(u"stick leds"_s, 0).at(2).toMap().value(u"readOnly"_s).toBool());
        open(u"stick leds"_s);
        show(u"rgb.hue"_s, u"rgb.hue"_s);
        key(Qt::Key_Return);
        QVERIFY(swatchSheet()->property("shown").toBool());
        QCOMPARE(swatchSheet()->property("focusIndex").toInt(), 0);
        laidOut(swatchSheet());
        tap(settledPoint(swatchSheet(), 2));
        QTRY_VERIFY(!swatchSheet()->property("shown").toBool());
        QTRY_COMPARE(fake->stored().value(u"rgb.hue"_s), QVariant(20u));
    }

    void touchAdjustNumber()
    {
        fake->setSchema(FakeMicroSetd::describe({FakeMicroSetd::shipped(), FakeMicroSetd::fixture(u"50-microemu.json"_s)}));
        model->load();
        QTRY_VERIFY(!model->rows(u"emulation"_s, model->revision()).isEmpty());
        open(u"emulation"_s);
        show(u"microemu.retroarch.savestate_max_keep"_s, u"microemu.retroarch.savestate_max_keep"_s);
        key(Qt::Key_Return);
        QVERIFY(adjust()->property("shown").toBool());
        QCOMPARE(adjustNumber(0), u"10"_s);
        tap(centerOf(qvariant_cast<QQuickItem *>(adjustColumn(0)->property("plusBox"))));
        QTRY_COMPARE(adjustNumber(0), u"11"_s);
        QTRY_COMPARE(fake->calls(), QStringList{u"Set microemu.retroarch.savestate_max_keep 11"_s});
        tap(centerOf(qvariant_cast<QQuickItem *>(adjustColumn(0)->property("minusBox"))));
        QTRY_COMPARE(adjustNumber(0), u"10"_s);
        QTRY_COMPARE(fake->calls().size(), 2);
        auto *root = qobject_cast<QQuickItem *>(adjust());
        const QPoint plus = centerOf(qvariant_cast<QQuickItem *>(adjustColumn(0)->property("plusBox")));
        tap(root->mapToScene(QPointF(root->width() * 3 / 4, root->height() / 2)).toPoint());
        QTest::qWait(100);
        QCOMPARE(adjustNumber(0), u"10"_s);
        QVERIFY(page()->property("editing").toBool());
        QCOMPARE(fake->calls().size(), 2);
        tap(plus);
        QTRY_COMPARE(adjustNumber(0), u"11"_s);
        QTRY_COMPARE(fake->calls().size(), 3);
        tap(centerOf(qvariant_cast<QQuickItem *>(adjust()->property("exitBox"))));
        QTRY_VERIFY(!page()->property("editing").toBool());
        QTest::qWait(100);
        QCOMPARE(fake->calls().size(), 3);
        QCOMPARE(fake->calls().last(), u"Set microemu.retroarch.savestate_max_keep 11"_s);
        QTRY_COMPARE(value(u"microemu.retroarch.savestate_max_keep"_s), u"11"_s);
    }

    void touchAdjustBalance()
    {
        open(u"audio"_s);
        show(u"audio.balance.headphones"_s, u"audio.balance.headphones"_s);
        key(Qt::Key_Return);
        QVERIFY(adjust()->property("shown").toBool());
        QCOMPARE(adjustNumber(0), u"0.0"_s);
        QCOMPARE(adjustNumber(1), u"0.0"_s);
        tap(centerOf(qvariant_cast<QQuickItem *>(adjustColumn(0)->property("minusBox"))));
        QTRY_COMPARE(adjustNumber(0), u"-0.5"_s);
        QCOMPARE(adjustNumber(1), u"0.0"_s);
        tap(centerOf(qvariant_cast<QQuickItem *>(adjustColumn(1)->property("plusBox"))));
        QTest::qWait(100);
        QCOMPARE(adjustNumber(0), u"-0.5"_s);
        QCOMPARE(adjustNumber(1), u"0.0"_s);
        tap(centerOf(qvariant_cast<QQuickItem *>(adjustColumn(0)->property("plusBox"))));
        QTRY_COMPARE(adjustNumber(0), u"0.0"_s);
        QCOMPARE(adjustNumber(1), u"0.0"_s);
        tap(centerOf(qvariant_cast<QQuickItem *>(adjustColumn(1)->property("minusBox"))));
        QTRY_COMPARE(adjustNumber(1), u"-0.5"_s);
        QCOMPARE(adjustNumber(0), u"0.0"_s);
        QVERIFY(page()->property("editing").toBool());
        QVERIFY(fake->calls().isEmpty());
        auto *exitBox = qvariant_cast<QQuickItem *>(adjust()->property("exitBox"));
        auto *leftPlus = qvariant_cast<QQuickItem *>(adjustColumn(0)->property("plusBox"));
        const QRectF corner = exitBox->mapRectToScene(QRectF(0, 0, exitBox->width(), exitBox->height()))
                                  .intersected(leftPlus->mapRectToScene(QRectF(0, 0, leftPlus->width(), leftPlus->height())));
        QVERIFY(!corner.isEmpty());
        tap(corner.center().toPoint());
        QTRY_VERIFY(!page()->property("editing").toBool());
        QTRY_COMPARE(fake->calls(), QStringList{u"Set audio.balance.headphones -1"_s});
        QTRY_COMPARE(value(u"audio.balance.headphones"_s), u"l 0.5 db"_s);
    }

    void touchAdjustFadeBlocks()
    {
        fake->setSchema(FakeMicroSetd::describe({FakeMicroSetd::shipped(), FakeMicroSetd::fixture(u"50-microemu.json"_s)}));
        model->load();
        QTRY_VERIFY(!model->rows(u"emulation"_s, model->revision()).isEmpty());
        open(u"emulation"_s);
        page()->setProperty("animated", true);
        show(u"microemu.retroarch.savestate_max_keep"_s, u"microemu.retroarch.savestate_max_keep"_s);
        key(Qt::Key_Return);
        QVERIFY(adjust()->property("shown").toBool());
        QTRY_COMPARE(qobject_cast<QQuickItem *>(adjust())->opacity(), 1.0);
        const QPoint plus = centerOf(qvariant_cast<QQuickItem *>(adjustColumn(0)->property("plusBox")));
        tap(plus);
        QTRY_COMPARE(fake->calls(), QStringList{u"Set microemu.retroarch.savestate_max_keep 11"_s});
        tap(centerOf(qvariant_cast<QQuickItem *>(adjust()->property("exitBox"))));
        QTRY_VERIFY(!page()->property("editing").toBool());
        QVERIFY(qobject_cast<QQuickItem *>(adjust())->opacity() > 0);
        tap(plus);
        QTest::qWait(100);
        QVERIFY(!page()->property("editing").toBool());
        QVERIFY(!sheetShown());
        QVERIFY(!page()->property("valueOpen").toBool());
        QVERIFY(!rowSheet()->property("shown").toBool());
        QCOMPARE(fake->calls(), QStringList{u"Set microemu.retroarch.savestate_max_keep 11"_s});
        QTRY_COMPARE(value(u"microemu.retroarch.savestate_max_keep"_s), u"11"_s);
    }

    void touchPressCutsTitle()
    {
        open(u"audio"_s);
        page()->setProperty("animated", true);
        show(u"audio.volume_step"_s, u"audio.output"_s);
        QTRY_VERIFY(near(titleAlpha(u"audio.volume_step"_s), 0.6));
        QTRY_COMPARE(titleAlpha(u"audio.output"_s), 1.0);
        const QPoint at = centerOf(rowItem(u"audio.volume_step"_s));
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, at);
        QCOMPARE(focusedKey(), u"audio.volume_step"_s);
        QCOMPARE(titleAlpha(u"audio.volume_step"_s), 1.0);
        QVERIFY(titleAlpha(u"audio.output"_s) > 0.6);
        QTRY_VERIFY(titleAlpha(u"audio.output"_s) < 1.0);
        QCOMPARE(titleAlpha(u"audio.volume_step"_s), 1.0);
        const QPoint by(2 * int(sheet()->property("pitch").toReal()), 0);
        QTest::mouseMove(window, at + by / 2, 8);
        QTest::mouseMove(window, at + by, 8);
        QCOMPARE(titleAlpha(u"audio.volume_step"_s), 1.0);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, at + by);
        QTest::qWait(300);
        QCOMPARE(titleAlpha(u"audio.volume_step"_s), 1.0);
        QTRY_VERIFY(near(titleAlpha(u"audio.output"_s), 0.6));
        QVERIFY(!sheetShown());
    }

    void touchPressCutsRowSheetEntry()
    {
        QTemporaryDir steam;
        qputenv("MICROSET_STEAM_DIR", steam.path().toUtf8());
        FakeMicroSetd::writeSteam(steam.path(), {{u"292120"_s, u"FINAL FANTASY XIII"_s}, {u"2238900"_s, u"SO2R"_s}});
        fake->setProton({FakeMicroSetd::shipped()}, FakeMicroSetd::Profiles);
        fake->setValues({{u"app.292120.lsfg.state"_s, u"off"_s}});
        fake->setStored({{u"app.292120.lsfg.state"_s, u"off"_s}});
        model->load();
        QTRY_COMPARE(model->value(u"app.292120.lsfg.state"_s, model->revision()).toString(), u"off"_s);
        writeLsfg(R"({"version": 1, "owned": "yes", "install": "installed"})");
        openLsfg();
        QTRY_COMPARE(value(u"lsfg-games"_s), u"0 on"_s);
        page()->setProperty("animated", true);
        show(u"lsfg-games"_s, u"lsfg-games"_s);
        key(Qt::Key_Return);
        QVERIFY(rowSheet()->property("shown").toBool());
        QTRY_COMPARE(entryKeys(), (QStringList{u"lsfg-game-292120"_s, u"lsfg-add"_s}));
        QTRY_VERIFY(!rowSheet()->property("animating").toBool());
        key(Qt::Key_Down);
        QCOMPARE(rowSheet()->property("focusIndex").toInt(), 1);
        QTRY_VERIFY(near(alphaOf(cellTitle(0)), 0.6));
        const QPoint at = settledPoint(rowSheet(), 0);
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, at);
        QCOMPARE(rowSheet()->property("focusIndex").toInt(), 0);
        QCOMPARE(alphaOf(cellTitle(0)), 1.0);
        QVERIFY(alphaOf(cellTitle(1)) > 0.6);
        const QPoint by(2 * int(rowSheet()->property("pitch").toReal()), 0);
        QTest::mouseMove(window, at + by / 2, 8);
        QTest::mouseMove(window, at + by, 8);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, at + by);
        QTRY_VERIFY(near(alphaOf(cellTitle(1)), 0.6));
        QCOMPARE(alphaOf(cellTitle(0)), 1.0);
        QVERIFY(!gameSheet()->property("shown").toBool());
        auto *lead = qvariant_cast<QQuickItem *>(rowSheet()->property("leadLine"));
        QVERIFY(lead->isVisible());
        const QList<qreal> before{alphaOf(cellTitle(0)), alphaOf(cellTitle(1))};
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, centerOf(lead));
        QTest::qWait(50);
        QCOMPARE((QList<qreal>{alphaOf(cellTitle(0)), alphaOf(cellTitle(1))}), before);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, centerOf(lead));
        QTest::qWait(100);
        QCOMPARE(rowSheet()->property("focusIndex").toInt(), 0);
        QVERIFY(!gameSheet()->property("shown").toBool());
        qunsetenv("MICROSET_STEAM_DIR");
    }

    void pageFlingAtListDecay()
    {
        open(u"audio"_s);
        auto *flick = qvariant_cast<QQuickItem *>(page()->property("flick"));
        QObject *scroller = qvariant_cast<QObject *>(flick->property("touch"));
        QCOMPARE(flick->property("contentY").toReal(), 0.0);
        const QPoint from = flick->mapToScene(QPointF(flick->width() / 2, flick->height() * 3 / 4)).toPoint();
        drag(from, QPoint(0, -int(flick->height() / 3)), 6);
        for (int i = 0; i < 2000 && !scroller->property("flinging").toBool(); ++i)
            QTest::qWait(1);
        QVERIFY(scroller->property("flinging").toBool());
        const double velocity = scroller->property("velocity").toReal();
        const double end = flick->property("contentY").toReal() + velocity / (62.5 * -std::log(0.83));
        qreal limit = 0;
        QVERIFY(QMetaObject::invokeMethod(page(), "maxContentY", Q_RETURN_ARG(qreal, limit)));
        QVERIFY(velocity > 0);
        QVERIFY(end < limit);
        QTRY_VERIFY(!scroller->property("flinging").toBool());
        const double moved = flick->property("contentY").toReal();
        QVERIFY2(std::abs(moved - end) <= 0.5, qPrintable(u"%1 against %2 from %3 px/s"_s.arg(moved).arg(end).arg(velocity)));
    }
};

QTEST_MAIN(PageTest)
#include "tst_page.moc"
