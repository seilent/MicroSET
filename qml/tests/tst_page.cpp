#include "fakemicrosetd.h"
#include "harness.h"
#include "settingsmodel.h"

#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QtQml/qqmlextensionplugin.h>

#include <memory>

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

    void held(const QString &name)
    {
        bool handled = false;
        QVERIFY(QMetaObject::invokeMethod(page(), "handleAction", Q_RETURN_ARG(bool, handled), Q_ARG(QString, name),
                                          Q_ARG(bool, true)));
    }

    void ledsOn()
    {
        fake->setStored({{u"rgb.enabled"_s, true}, {u"rgb.saturation"_s, 60u}});
        fake->setValues({{u"rgb.enabled"_s, true}, {u"rgb.saturation"_s, 60u}, {u"rgb.brightness"_s, 50u}});
        model->load();
        QTRY_VERIFY(!model->rows(u"stick leds"_s, model->revision()).at(2).toMap().value(u"readOnly"_s).toBool());
    }

private slots:
    void init()
    {
        QTest::failOnWarning(QRegularExpression(u"TypeError|ReferenceError|Unable to assign|Binding loop"_s));
        h = std::make_unique<Harness>();
        QVERIFY(h->startBus());
        fake = std::make_unique<FakeMicroSetd>(h->address());
        fake->setSchema(FakeMicroSetd::describe({FakeMicroSetd::shipped()}));
        QVERIFY(fake->start());
        model = std::make_unique<SettingsModel>(h->client());
        model->load();
        QTRY_VERIFY(model->available());
        engine = std::make_unique<QQmlApplicationEngine>();
        engine->setInitialProperties({
            {u"model"_s, QVariant::fromValue(static_cast<QObject *>(model.get()))},
            {u"screenshot"_s, true},
            {u"shotState"_s, u"none"_s},
        });
        engine->loadFromModule("MicroSetPageTest", "PageRoot");
        window = qobject_cast<QQuickWindow *>(engine->rootObjects().value(0));
        QVERIFY(window);
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(window));
        view = window->findChild<QObject *>(u"settingsView"_s);
        QVERIFY(view);
    }

    void cleanup()
    {
        engine.reset();
        model.reset();
        fake.reset();
        h.reset();
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
        fake->setValues({{u"app.77.fex.tso"_s, u"on"_s}});
        model->load();
        QTRY_VERIFY(model->available());
        focus(u"group-games"_s);
        key(Qt::Key_Return);
        QCOMPARE(page()->property("group").toString(), u"games"_s);
        QCOMPARE(page()->property("title").toString(), u"games"_s);
        QTRY_VERIFY(!row(u"game-292120"_s).isEmpty());
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
        key(Qt::Key_Return);
        QCOMPARE(page()->property("group").toString(), u"games"_s);
        QVERIFY(!sheetShown());
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
        QCOMPARE(value(u"rgb.saturation"_s), u"55 %"_s);
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
        QCOMPARE(value(u"audio.volume_step"_s), u"10%"_s);
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
        QCOMPARE(value(u"audio.volume_step"_s), u"10%"_s);
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

    void hueEditsThroughOverride()
    {
        fake->setStored({{u"rgb.enabled"_s, true}});
        model->load();
        QTRY_COMPARE(model->rows(u"stick leds"_s, 0).size(), 5);
        QTRY_VERIFY(!model->rows(u"stick leds"_s, 0).at(2).toMap().value(u"readOnly"_s).toBool());
        open(u"stick leds"_s);
        focus(u"rgb.hue"_s);
        QCOMPARE(row(u"rgb.hue"_s).value(u"swatch"_s).toString(), u"#ff0000"_s);
        key(Qt::Key_Return);
        QVERIFY(page()->property("editing").toBool());
        QVERIFY(!sheetShown());
        key(Qt::Key_Right);
        QTRY_COMPARE(fake->calls(), QStringList{u"Override rgb.hue 10"_s});
        QTRY_COMPARE(row(u"rgb.hue"_s).value(u"swatch"_s).toString(), u"#ff2b00"_s);
        key(Qt::Key_Right);
        QTRY_COMPARE(fake->calls().size(), 2);
        QCOMPARE(fake->calls().at(1), u"Override rgb.hue 20"_s);
        key(Qt::Key_Escape);
        QTRY_COMPARE(fake->calls().size(), 3);
        QCOMPARE(fake->calls().at(2), u"ClearOverride rgb.hue"_s);
        QTRY_COMPARE(row(u"rgb.hue"_s).value(u"swatch"_s).toString(), u"#ff0000"_s);
        key(Qt::Key_Return);
        key(Qt::Key_Right);
        key(Qt::Key_Return);
        QTRY_COMPARE(fake->calls().size(), 6);
        QCOMPARE(fake->calls().mid(3), (QStringList{u"Override rgb.hue 10"_s, u"Set rgb.hue 10"_s,
                                                    u"ClearOverride rgb.hue"_s}));
    }

    void balanceEditsWithDraft()
    {
        open(u"audio"_s);
        focus(u"audio.balance.headphones"_s);
        key(Qt::Key_Return);
        QVERIFY(page()->property("editing").toBool());
        QVERIFY(!sheetShown());
        key(Qt::Key_Right);
        key(Qt::Key_Right);
        QCOMPARE(value(u"audio.balance.headphones"_s), u"r 1 db"_s);
        QTest::qWait(100);
        QVERIFY(fake->calls().isEmpty());
        key(Qt::Key_Return);
        QVERIFY(!page()->property("editing").toBool());
        QTRY_COMPARE(fake->calls(), QStringList{u"Set audio.balance.headphones 2"_s});
        key(Qt::Key_Return);
        key(Qt::Key_Left);
        key(Qt::Key_Escape);
        QCOMPARE(value(u"audio.balance.headphones"_s), u"r 1 db"_s);
        QTest::qWait(100);
        QCOMPARE(fake->calls().size(), 1);
    }

    void flipsOnOneA()
    {
        open(u"display"_s);
        focus(u"display.orientation"_s);
        QVERIFY(row(u"display.orientation"_s).value(u"readOnly"_s).toBool());
        key(Qt::Key_Return);
        QVERIFY(!sheetShown());
        QVERIFY(!page()->property("editing").toBool());
        focus(u"display.auto_rotate"_s);
        QCOMPARE(row(u"display.auto_rotate"_s).value(u"pick"_s).toString(), u"flip"_s);
        key(Qt::Key_Return);
        QVERIFY(!sheetShown());
        QVERIFY(!page()->property("editing").toBool());
        QTRY_COMPARE(fake->calls(), QStringList{u"Set display.auto_rotate false"_s});
        QTRY_VERIFY(!row(u"display.orientation"_s).value(u"readOnly"_s).toBool());
        QCOMPARE(value(u"display.auto_rotate"_s), u"off"_s);
        open(u"audio"_s);
        focus(u"audio.eq.headphones.on"_s);
        key(Qt::Key_Return);
        QTRY_COMPARE(fake->calls().size(), 2);
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
        QCOMPARE(title->width(), title->implicitWidth());
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
};

QTEST_MAIN(PageTest)
#include "tst_page.moc"
