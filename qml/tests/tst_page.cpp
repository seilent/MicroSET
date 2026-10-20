#include "fakemicrosetd.h"
#include "harness.h"
#include "settingsmodel.h"

#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QSignalSpy>
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

    void editStepConfirm()
    {
        open(u"audio"_s);
        focus(u"audio.output"_s);
        key(Qt::Key_Return);
        QVERIFY(page()->property("editing").toBool());
        QCOMPARE(page()->property("editKey").toString(), u"audio.output"_s);
        key(Qt::Key_Right);
        QCOMPARE(value(u"audio.output"_s), u"speaker"_s);
        QTRY_COMPARE(fake->sets().size(), 1);
        QCOMPARE(fake->sets().at(0), qMakePair(u"audio.output"_s, QVariant(u"speaker"_s)));
        key(Qt::Key_Return);
        QVERIFY(!page()->property("editing").toBool());
        QTest::qWait(50);
        QCOMPARE(fake->sets().size(), 1);
        QCOMPARE(value(u"audio.output"_s), u"speaker"_s);
    }

    void cancelWritesBack()
    {
        open(u"audio"_s);
        focus(u"audio.volume_step"_s);
        key(Qt::Key_Return);
        key(Qt::Key_Right);
        QCOMPARE(value(u"audio.volume_step"_s), u"10%"_s);
        key(Qt::Key_Escape);
        QVERIFY(!page()->property("editing").toBool());
        QCOMPARE(page()->property("group").toString(), u"audio"_s);
        QTRY_COMPARE(fake->sets().size(), 2);
        QCOMPARE(fake->sets().at(1), qMakePair(u"audio.volume_step"_s, QVariant(5u)));
        QTRY_COMPARE(value(u"audio.volume_step"_s), u"5%"_s);
        QCOMPARE(fake->stored().value(u"audio.volume_step"_s), QVariant(5u));
    }

    void refusalToast()
    {
        fake->refuse(u"audio.output"_s, u"net.seilent.MicroSet1.Error.InvalidValue"_s);
        open(u"audio"_s);
        focus(u"audio.output"_s);
        key(Qt::Key_Return);
        key(Qt::Key_Right);
        QTRY_VERIFY(toast()->property("shown").toBool());
        QCOMPARE(toast()->property("artist").toString(), u"Output"_s);
        QCOMPARE(toast()->property("title").toString(), u"set failed"_s);
        QTRY_COMPARE(value(u"audio.output"_s), u"auto"_s);
    }

    void undervoltHoldPending()
    {
        fake->refuse(u"power.gpu_undervolt"_s, u"net.seilent.MicroSet1.Error.HoldPending"_s);
        open(u"power"_s);
        focus(u"power.gpu_undervolt"_s);
        QCOMPARE(value(u"power.gpu_undervolt"_s), u"Off"_s);
        key(Qt::Key_Return);
        key(Qt::Key_Right);
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
        if (page()->property("editing").toBool())
            key(Qt::Key_Return);
        QTRY_VERIFY(!page()->property("editing").toBool());
        key(Qt::Key_Return);
        key(Qt::Key_Right);
        QTRY_COMPARE(fake->sets().size(), 2);
        QTRY_COMPARE(toast()->property("title").toString(), u"try again shortly"_s);
        QCOMPARE(toast()->property("artist").toString(), u"GPU undervolt"_s);
    }

    void echoHeldDuringEdit()
    {
        fake->setHold(true);
        open(u"audio"_s);
        focus(u"audio.output"_s);
        key(Qt::Key_Return);
        key(Qt::Key_Right);
        key(Qt::Key_Right);
        QCOMPARE(value(u"audio.output"_s), u"headphones"_s);
        QTRY_COMPARE(fake->sets().size(), 2);
        fake->emitChanged(u"audio.output"_s, u"bt"_s);
        fake->release(1);
        QTest::qWait(100);
        QCOMPARE(value(u"audio.output"_s), u"headphones"_s);
        fake->release();
        QTest::qWait(100);
        QCOMPARE(value(u"audio.output"_s), u"headphones"_s);
        QVERIFY(page()->property("editing").toBool());
    }

    void hueSteps()
    {
        fake->setStored({{u"rgb.enabled"_s, true}});
        model->load();
        QTRY_COMPARE(model->rows(u"stick leds"_s, 0).size(), 5);
        QTRY_VERIFY(!model->rows(u"stick leds"_s, 0).at(2).toMap().value(u"readOnly"_s).toBool());
        open(u"stick leds"_s);
        focus(u"rgb.hue"_s);
        QCOMPARE(row(u"rgb.hue"_s).value(u"swatch"_s).toString(), u"#ff0000"_s);
        key(Qt::Key_Return);
        key(Qt::Key_Right);
        QTRY_COMPARE(fake->sets().size(), 1);
        QCOMPARE(fake->sets().at(0), qMakePair(u"rgb.hue"_s, QVariant(10u)));
        QTRY_COMPARE(row(u"rgb.hue"_s).value(u"swatch"_s).toString(), u"#ff2b00"_s);
        key(Qt::Key_Right);
        QTRY_COMPARE(fake->sets().size(), 2);
        QCOMPARE(fake->sets().at(1), qMakePair(u"rgb.hue"_s, QVariant(20u)));
        key(Qt::Key_Escape);
        QTRY_COMPARE(fake->sets().size(), 3);
        QCOMPARE(fake->sets().at(2), qMakePair(u"rgb.hue"_s, QVariant(0u)));
    }

    void draftAppliesOnA()
    {
        open(u"system"_s);
        focus(u"session.default"_s);
        key(Qt::Key_Return);
        QVERIFY(page()->property("editing").toBool());
        key(Qt::Key_Left);
        QCOMPARE(value(u"session.default"_s), u"microemu"_s);
        QTest::qWait(50);
        QVERIFY(fake->sets().isEmpty());
        key(Qt::Key_Return);
        QVERIFY(!page()->property("editing").toBool());
        QTRY_COMPARE(fake->sets().size(), 1);
        QCOMPARE(fake->sets().at(0), qMakePair(u"session.default"_s, QVariant(u"microemu"_s)));
        QTRY_COMPARE(value(u"session.default"_s), u"microemu"_s);
    }

    void draftDroppedOnB()
    {
        open(u"system"_s);
        focus(u"session.default"_s);
        key(Qt::Key_Return);
        key(Qt::Key_Left);
        QCOMPARE(value(u"session.default"_s), u"microemu"_s);
        key(Qt::Key_Escape);
        QVERIFY(!page()->property("editing").toBool());
        QCOMPARE(value(u"session.default"_s), u"steam"_s);
        QTest::qWait(50);
        QVERIFY(fake->sets().isEmpty());
    }

    void draftDroppedOnRotation()
    {
        QObject *ui = engine->singletonInstance<QObject *>("MicroSet", "Ui");
        QVERIFY(ui);
        open(u"system"_s);
        focus(u"session.default"_s);
        key(Qt::Key_Return);
        key(Qt::Key_Left);
        ui->setProperty("turn", 1);
        QVERIFY(!page()->property("editing").toBool());
        QCOMPARE(value(u"session.default"_s), u"steam"_s);
        ui->setProperty("turn", 0);
        open(u"audio"_s);
        focus(u"audio.output"_s);
        key(Qt::Key_Return);
        key(Qt::Key_Right);
        ui->setProperty("turn", 1);
        ui->setProperty("turn", 0);
        QVERIFY(!page()->property("editing").toBool());
        QTRY_COMPARE(fake->sets().size(), 1);
        QCOMPARE(value(u"audio.output"_s), u"speaker"_s);
    }

    void lockedRowsDoNotEdit()
    {
        open(u"display"_s);
        focus(u"display.orientation"_s);
        QVERIFY(row(u"display.orientation"_s).value(u"readOnly"_s).toBool());
        key(Qt::Key_Return);
        QVERIFY(!page()->property("editing").toBool());
        focus(u"display.auto_rotate"_s);
        key(Qt::Key_Return);
        key(Qt::Key_Right);
        QCOMPARE(value(u"display.auto_rotate"_s), u"off"_s);
        QVERIFY(row(u"display.orientation"_s).value(u"readOnly"_s).toBool());
        key(Qt::Key_Return);
        QTRY_COMPARE(fake->sets().size(), 1);
        QTRY_VERIFY(!row(u"display.orientation"_s).value(u"readOnly"_s).toBool());
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
        focus(u"audio.output"_s);
        key(Qt::Key_Return);
        QVERIFY(page()->property("editing").toBool());
        fake->stop();
        QTRY_VERIFY(!model->available());
        QVERIFY(!page()->property("editing").toBool());
        QCOMPARE(value(u"audio.output"_s), u"auto"_s);
        key(Qt::Key_Return);
        QVERIFY(!page()->property("editing").toBool());
        QTRY_VERIFY(toast()->property("shown").toBool());
        QCOMPARE(toast()->property("title").toString(), u"not available"_s);
        fake->setValues({{u"audio.output"_s, u"usb"_s}});
        QVERIFY(fake->start());
        QTRY_VERIFY(model->available());
        QTRY_COMPARE(value(u"audio.output"_s), u"usb"_s);
        key(Qt::Key_Return);
        QVERIFY(page()->property("editing").toBool());
    }
};

QTEST_MAIN(PageTest)
#include "tst_page.moc"
