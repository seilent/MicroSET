#include "dbuspad.h"
#include "fakemicrosetd.h"
#include "harness.h"
#include "overlay.h"
#include "settingsmodel.h"
#include "uirelease.h"

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QRegularExpression>
#include <QTest>
#include <QtQml/qqmlextensionplugin.h>

#include <memory>
#include <xcb/xcb.h>

Q_IMPORT_QML_PLUGIN(MicroSetPlugin)
Q_IMPORT_QML_PLUGIN(MicroSetOverlayPlugin)

using namespace Qt::StringLiterals;

class FakeOverlay : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool shown READ shown NOTIFY shownChanged)
    Q_PROPERTY(bool mapped READ mapped CONSTANT)
    Q_PROPERTY(qreal opacity READ opacity CONSTANT)
    Q_PROPERTY(bool hintShown READ hintShown CONSTANT)
    Q_PROPERTY(bool hint READ hint NOTIFY hintChanged)
    Q_PROPERTY(QString hintDevice READ hintDevice CONSTANT)
    Q_PROPERTY(QString hintKind READ hintKind NOTIFY hintChanged)
    Q_PROPERTY(bool menuOpen READ menuOpen NOTIFY menuStateChanged)
    Q_PROPERTY(bool menuLeaving READ menuLeaving NOTIFY menuStateChanged)
    Q_PROPERTY(bool menuOnly READ menuOnly NOTIFY menuStateChanged)
    Q_PROPERTY(bool menuBusy READ menuBusy NOTIFY menuCallChanged)
    Q_PROPERTY(QStringList menuItems READ menuItems NOTIFY menuChanged)
    Q_PROPERTY(int menuCurrent READ menuCurrent NOTIFY menuChanged)
    Q_PROPERTY(QStringList menuNotes READ menuNotes NOTIFY menuChanged)

public:
    bool shown() const { return m_shown; }
    void setShown(bool shown)
    {
        m_shown = shown;
        emit shownChanged();
    }
    bool mapped() const { return true; }
    qreal opacity() const { return 1.0; }
    bool hintShown() const { return false; }
    bool hint() const { return m_hint; }
    QString hintDevice() const { return u"/dev/mmcblk0"_s; }
    QString hintKind() const { return m_kind; }
    bool menuOpen() const { return m_menuOpen; }
    bool menuLeaving() const { return m_menuLeaving; }
    bool menuOnly() const { return m_menuOnly; }
    bool menuBusy() const { return m_menuBusy; }
    QStringList menuItems() const { return m_items; }
    int menuCurrent() const { return m_current; }
    QStringList menuNotes() const { return m_notes; }
    void setHint(bool hint, const QString &kind = u"format"_s)
    {
        m_hint = hint;
        m_kind = kind;
        emit hintChanged();
    }
    void openMenu(const QStringList &items, int current, const QStringList &notes, bool only = true)
    {
        m_items = items;
        m_notes = notes;
        m_current = current;
        m_menuOnly = only;
        m_menuLeaving = false;
        emit menuChanged();
        m_menuOpen = true;
        emit menuStateChanged();
    }
    void leaveMenu()
    {
        m_menuOpen = false;
        m_menuLeaving = true;
        emit menuStateChanged();
    }
    void resetMenu()
    {
        m_menuOpen = false;
        m_menuLeaving = false;
        m_menuOnly = false;
        emit menuStateChanged();
    }
    void setMenuBusy(bool busy)
    {
        m_menuBusy = busy;
        emit menuCallChanged();
    }
    Q_INVOKABLE void menuAccept(int index)
    {
        ++accepts;
        accepted = index;
    }
    Q_INVOKABLE void menuClose()
    {
        ++closes;
        m_menuOpen = false;
        emit menuStateChanged();
    }
    Q_INVOKABLE void hide() { ++hides; }
    Q_INVOKABLE void hintDone() {}

    int hides = 0;
    int accepts = 0;
    int accepted = -1;
    int closes = 0;

signals:
    void shownChanged();
    void pageHint();
    void hintShownChanged();
    void hintChanged();
    void menuStateChanged();
    void menuCallChanged();
    void menuChanged();
    void menuToast(const QString &);

private:
    bool m_shown = true;
    bool m_hint = false;
    bool m_menuOpen = false;
    bool m_menuLeaving = false;
    bool m_menuOnly = false;
    bool m_menuBusy = false;
    QString m_kind;
    QStringList m_items;
    QStringList m_notes;
    int m_current = -1;
};

class FakePad : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool holding READ holding NOTIFY holdingChanged)

public:
    enum Action { Up, Down, Left, Right, A, B };

    bool holding() const { return m_holding; }
    void setHolding(bool holding)
    {
        m_holding = holding;
        emit holdingChanged();
    }
    Q_INVOKABLE QString actionName(int action) const
    {
        static const QStringList names{u"up"_s, u"down"_s, u"left"_s, u"right"_s, u"a"_s, u"b"_s};
        return names.value(action);
    }
    void press(int a)
    {
        emit pressed(a);
        emit action(a, false);
    }
    void tap(int a)
    {
        press(a);
        emit released(a);
    }

signals:
    void action(int action, bool repeat);
    void pressed(int action);
    void released(int action);
    void holdingChanged();

private:
    bool m_holding = true;
};

class OverlayQmlTest : public QObject
{
    Q_OBJECT

    std::unique_ptr<SettingsModel> m_model;
    std::unique_ptr<FakeOverlay> m_overlay;
    std::unique_ptr<FakePad> m_pad;
    std::unique_ptr<QQmlApplicationEngine> m_engine;
    QQuickWindow *m_window = nullptr;
    QObject *m_view = nullptr;

    QObject *page() const
    {
        return qvariant_cast<QObject *>(qvariant_cast<QObject *>(m_view->property("stack"))->property("currentItem"));
    }

    QObject *toast() const { return qvariant_cast<QObject *>(m_view->property("toast")); }

    QString title() const { return page()->property("title").toString(); }

    static constexpr uint32_t minMaxSize = (1 << 4) | (1 << 5);

    static QList<int> sizeHints(QWindow *window)
    {
        auto *x11 = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
        xcb_connection_t *connection = x11->connection();
        const auto id = static_cast<xcb_window_t>(window->winId());
        const xcb_get_property_cookie_t cookie
            = xcb_get_property(connection, 0, id, XCB_ATOM_WM_NORMAL_HINTS, XCB_ATOM_WM_SIZE_HINTS, 0, 18);
        xcb_get_property_reply_t *reply = xcb_get_property_reply(connection, cookie, nullptr);
        const auto freeReply = qScopeGuard([reply] { free(reply); });
        if (!reply || reply->format != 32 || xcb_get_property_value_length(reply) < 9 * 4)
            return {};
        const auto *v = static_cast<const uint32_t *>(xcb_get_property_value(reply));
        return {int(v[0] & minMaxSize), int(v[5]), int(v[6]), int(v[7]), int(v[8])};
    }

private slots:
    void init()
    {
        QTest::failOnWarning(QRegularExpression(u"TypeError|ReferenceError|Unable to assign|Binding loop"_s));
        m_model = std::make_unique<SettingsModel>();
        m_model->setFixture(FakeMicroSetd::describe({FakeMicroSetd::shipped()}), {}, {});
        m_overlay = std::make_unique<FakeOverlay>();
        m_pad = std::make_unique<FakePad>();
        m_engine = std::make_unique<QQmlApplicationEngine>();
        m_engine->setInitialProperties({
            {u"overlay"_s, QVariant::fromValue(static_cast<QObject *>(m_overlay.get()))},
            {u"model"_s, QVariant::fromValue(static_cast<QObject *>(m_model.get()))},
            {u"pad"_s, QVariant::fromValue(static_cast<QObject *>(m_pad.get()))},
            {u"screenshot"_s, true},
            {u"shotState"_s, u"none"_s},
            {u"shotWidth"_s, 960},
            {u"shotHeight"_s, 640},
        });
        m_engine->loadFromModule("MicroSetOverlay", "Overlay");
        m_window = qobject_cast<QQuickWindow *>(m_engine->rootObjects().value(0));
        QVERIFY(m_window);
        m_window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(m_window));
        m_view = m_window->findChild<QObject *>(u"settingsView"_s);
        QVERIFY(m_view);
        QCOMPARE(title(), u"settings"_s);
    }

    void cleanup()
    {
        m_engine.reset();
        m_pad.reset();
        m_overlay.reset();
        m_model.reset();
    }

    void holdTextComesBackAfterAnotherToast()
    {
        const QString hold = u"Hold A to format /dev/mmcblk0"_s;
        m_overlay->setHint(true);
        emit m_overlay->pageHint();
        QTRY_COMPARE(toast()->property("phase").toString(), u"hold"_s);
        QCOMPARE(toast()->property("title").toString(), hold);
        QVERIFY(toast()->property("sticky").toBool());
        QVERIFY(QMetaObject::invokeMethod(toast(), "show", Q_ARG(QString, u"Output"_s), Q_ARG(QString, u"set failed"_s)));
        QTRY_COMPARE(toast()->property("title").toString(), u"set failed"_s);
        QVERIFY(toast()->property("sticky").toBool());
        const int holdMs = toast()->property("holdDuration").toInt();
        QTRY_COMPARE_WITH_TIMEOUT(toast()->property("title").toString(), hold, holdMs + 2000);
        QVERIFY(toast()->property("sticky").toBool());
        QTest::qWait(holdMs + 500);
        QCOMPARE(toast()->property("phase").toString(), u"hold"_s);
        QCOMPARE(toast()->property("title").toString(), hold);
        m_overlay->setHint(false);
        QVERIFY(!toast()->property("sticky").toBool());
        QTRY_COMPARE(toast()->property("phase").toString(), u"hidden"_s);
    }

    void undervoltHoldText()
    {
        m_overlay->setHint(true, u"undervolt"_s);
        emit m_overlay->pageHint();
        QTRY_COMPARE(toast()->property("phase").toString(), u"hold"_s);
        QCOMPARE(toast()->property("title").toString(), u"Hold A to apply undervolt"_s);
        QVERIFY(toast()->property("sticky").toBool());
        m_overlay->setHint(false);
        QTRY_COMPARE(toast()->property("phase").toString(), u"hidden"_s);
    }

    void acceptIgnoredWhileHint()
    {
        m_overlay->setHint(true);
        QTest::keyClick(m_window, Qt::Key_Return);
        m_pad->tap(FakePad::A);
        QTest::qWait(100);
        QCOMPARE(title(), u"settings"_s);
        QVERIFY(!page()->property("pressed").toBool());
        m_overlay->setHint(false);
        m_pad->tap(FakePad::A);
        QTRY_COMPARE(title(), u"audio"_s);
        QTest::keyClick(m_window, Qt::Key_Escape);
        QTRY_COMPARE(title(), u"settings"_s);
        QTest::keyClick(m_window, Qt::Key_Return);
        QTRY_COMPARE(title(), u"audio"_s);
    }

    void stripStaysForTheWholeHint()
    {
        cleanup();
        m_model = std::make_unique<SettingsModel>();
        m_model->setFixture(FakeMicroSetd::describe({FakeMicroSetd::shipped()}), {}, {});
        DbusPad pad(QDBusConnection(u"none"_s), QDBusConnection(u"none"_s), nullptr, false);
        Overlay overlay(&pad, nullptr);
        const auto engineFirst = qScopeGuard([this] { m_engine.reset(); });
        QQuickWindow *root = nullptr;
        overlay.setUi([&] {
            if (m_engine)
                return;
            m_engine = std::make_unique<QQmlApplicationEngine>();
            m_engine->setInitialProperties({
                {u"overlay"_s, QVariant::fromValue(static_cast<QObject *>(&overlay))},
                {u"model"_s, QVariant::fromValue(static_cast<QObject *>(m_model.get()))},
            });
            m_engine->loadFromModule("MicroSetOverlay", "Overlay");
            root = qobject_cast<QQuickWindow *>(m_engine->rootObjects().value(0));
            QVERIFY(root);
            overlay.setWindow(root);
            overlay.setToastWindow(qvariant_cast<QWindow *>(root->property("toastWindow")));
        });
        overlay.setHint(true, u"/dev/mmcblk0"_s);
        QVERIFY(root);
        QWindow *strip = overlay.toastWindow();
        QVERIFY(strip);
        QVERIFY(strip->handle());
        QCOMPARE(strip->width(), root->width());
        QCOMPARE(strip->height(), int(root->property("stripDepth").toReal()));
        auto *toast = qvariant_cast<QObject *>(root->property("hint"));
        QVERIFY(toast);
        const int whole = toast->property("inDuration").toInt() + 3 * toast->property("holdDuration").toInt();
        QElapsedTimer clock;
        clock.start();
        while (clock.elapsed() < whole) {
            QTest::qWait(20);
            QVERIFY2(overlay.hintShown() && strip->handle() && strip->isVisible(),
                     qPrintable(u"strip gone after %1 ms"_s.arg(clock.elapsed())));
        }
        QCOMPARE(toast->property("phase").toString(), u"hold"_s);
        overlay.setHint(false);
        QTRY_VERIFY(!overlay.hintShown());
        QVERIFY(!strip->handle());
    }

    void stripSizeHintsFollowTheTurn()
    {
        auto *strip = qvariant_cast<QWindow *>(m_window->property("toastWindow"));
        QVERIFY(strip);
        const int depth = int(m_window->property("stripDepth").toReal());
        const bool x11 = QGuiApplication::platformName() == "xcb"_L1;
        const struct {
            int width;
            int height;
            QString rotation;
            QSize strip;
        } turns[] = {
            {960, 640, u"landscape"_s, {960, depth}},
            {640, 960, u"landscape"_s, {depth, 960}},
            {640, 960, u"portrait"_s, {640, depth}},
            {640, 960, u"landscape-flipped"_s, {depth, 960}},
            {640, 960, u"portrait-flipped"_s, {640, depth}},
            {960, 640, u"landscape-flipped"_s, {960, depth}},
        };
        for (const auto &turn : turns) {
            const QString row = u"%1x%2 %3"_s.arg(turn.width).arg(turn.height).arg(turn.rotation);
            m_model->setFixture(FakeMicroSetd::describe({FakeMicroSetd::shipped()}),
                                {{u"display.rotation"_s, turn.rotation}}, {});
            m_window->setProperty("shotWidth", turn.width);
            m_window->setProperty("shotHeight", turn.height);
            QTRY_COMPARE(strip->size(), turn.strip);
            QVERIFY2(strip->minimumSize() == turn.strip && strip->maximumSize() == turn.strip, qPrintable(row));
            const QList<int> hints{int(minMaxSize), turn.strip.width(), turn.strip.height(), turn.strip.width(),
                                   turn.strip.height()};
            if (x11 && strip->handle())
                QCOMPARE(sizeHints(strip), hints);
            strip->show();
            QVERIFY(QTest::qWaitForWindowExposed(strip));
            QCOMPARE(strip->size(), turn.strip);
            if (x11)
                QCOMPARE(sizeHints(strip), hints);
            strip->hide();
        }
    }

    void hideSettlesPreview_data()
    {
        QTest::addColumn<bool>("hint");
        QTest::newRow("hide") << false;
        QTest::newRow("hide with the hint up") << true;
    }

    void hideSettlesPreview()
    {
        QFETCH(bool, hint);
        cleanup();
        Harness h;
        QVERIFY(h.startBus());
        FakeMicroSetd fake(h.address());
        fake.setSchema(FakeMicroSetd::describe({FakeMicroSetd::shipped()}));
        fake.setStored({{u"rgb.enabled"_s, true}, {u"rgb.saturation"_s, 60u}});
        fake.setValues({{u"rgb.enabled"_s, true}, {u"rgb.saturation"_s, 60u}});
        QVERIFY(fake.start());
        const auto modelFirst = qScopeGuard([this] { cleanup(); });
        m_model = std::make_unique<SettingsModel>(h.client());
        m_model->load();
        QTRY_VERIFY(m_model->available());
        m_overlay = std::make_unique<FakeOverlay>();
        m_pad = std::make_unique<FakePad>();
        m_engine = std::make_unique<QQmlApplicationEngine>();
        m_engine->setInitialProperties({
            {u"overlay"_s, QVariant::fromValue(static_cast<QObject *>(m_overlay.get()))},
            {u"model"_s, QVariant::fromValue(static_cast<QObject *>(m_model.get()))},
            {u"pad"_s, QVariant::fromValue(static_cast<QObject *>(m_pad.get()))},
            {u"screenshot"_s, true},
            {u"shotState"_s, u"none"_s},
        });
        m_engine->loadFromModule("MicroSetOverlay", "Overlay");
        m_window = qobject_cast<QQuickWindow *>(m_engine->rootObjects().value(0));
        QVERIFY(m_window);
        m_view = m_window->findChild<QObject *>(u"settingsView"_s);
        QVERIFY(QMetaObject::invokeMethod(m_view, "open", Q_ARG(QString, u"stick leds"_s)));
        int index = -1;
        QVERIFY(QMetaObject::invokeMethod(page(), "indexOfKey", Q_RETURN_ARG(int, index), Q_ARG(QString, u"rgb.saturation"_s)));
        page()->setProperty("focusIndex", index);
        QVERIFY(QMetaObject::invokeMethod(page(), "activate"));
        auto *sheet = qvariant_cast<QObject *>(page()->property("sheet"));
        QVERIFY(sheet->property("shown").toBool());
        for (int i = 0; i < 2; ++i)
            m_pad->press(FakePad::Down);
        QTRY_COMPARE(fake.calls(), (QStringList{u"Override rgb.saturation 55"_s, u"Override rgb.saturation 50"_s}));
        if (hint)
            m_overlay->setHint(true, u"undervolt"_s);
        m_overlay->setShown(false);
        QVERIFY(!sheet->property("shown").toBool());
        QTRY_COMPARE(fake.calls().size(), 4);
        QCOMPARE(fake.calls().mid(2), (QStringList{u"Set rgb.saturation 50"_s, u"ClearOverride rgb.saturation"_s}));
        m_model->endPreviews();
        m_engine.reset();
        m_model.reset();
        QTest::qWait(100);
        QCOMPARE(fake.calls().size(), 4);
        QCOMPARE(fake.stored().value(u"rgb.saturation"_s), QVariant(50u));
    }

    void unusedSettlesBeforeReset()
    {
        cleanup();
        Harness h;
        QVERIFY(h.startBus());
        FakeMicroSetd fake(h.address());
        fake.setSchema(FakeMicroSetd::describe({FakeMicroSetd::shipped()}));
        fake.setStored({{u"rgb.enabled"_s, true}, {u"rgb.saturation"_s, 60u}});
        fake.setValues({{u"rgb.enabled"_s, true}, {u"rgb.saturation"_s, 60u}});
        QVERIFY(fake.start());
        const auto modelFirst = qScopeGuard([this] { cleanup(); });
        m_model = std::make_unique<SettingsModel>(h.client());
        m_model->load();
        QTRY_VERIFY(m_model->available());
        DbusPad pad(QDBusConnection(u"none"_s), QDBusConnection(u"none"_s), nullptr, false);
        Overlay overlay(&pad, nullptr);
        m_engine = std::make_unique<QQmlApplicationEngine>();
        m_engine->setInitialProperties({
            {u"overlay"_s, QVariant::fromValue(static_cast<QObject *>(&overlay))},
            {u"model"_s, QVariant::fromValue(static_cast<QObject *>(m_model.get()))},
        });
        m_engine->loadFromModule("MicroSetOverlay", "Overlay");
        auto *root = qobject_cast<QQuickWindow *>(m_engine->rootObjects().value(0));
        QVERIFY(root);
        overlay.setWindow(root);
        QVERIFY(m_model->beginEdit(u"rgb.saturation"_s));
        QVERIFY(m_model->preview(45u));
        QTRY_COMPARE(fake.calls(), QStringList{u"Override rgb.saturation 45"_s});
        QStringList order;
        connect(m_model.get(), &SettingsModel::previewEnded, this, [&order] { order << u"settled"_s; });
        connect(root, &QObject::destroyed, this, [&order] { order << u"reset"_s; });
        releaseUi(overlay, m_model.get(), m_engine);
        QCOMPARE(order, (QStringList{u"settled"_s, u"reset"_s}));
        QVERIFY(!m_engine);
        QTRY_COMPARE(fake.calls().size(), 3);
        QCOMPARE(fake.calls().mid(1), (QStringList{u"Set rgb.saturation 45"_s, u"ClearOverride rgb.saturation"_s}));
        m_model.reset();
        QTest::qWait(100);
        QCOMPARE(fake.calls().size(), 3);
    }

    void backReleaseAfterHolderLossDoesNotHide()
    {
        m_pad->tap(FakePad::B);
        QCOMPARE(m_overlay->hides, 1);
        m_pad->press(FakePad::B);
        m_pad->setHolding(false);
        emit m_pad->released(FakePad::B);
        QCOMPARE(m_overlay->hides, 1);
        m_pad->setHolding(true);
        m_pad->tap(FakePad::B);
        QCOMPARE(m_overlay->hides, 2);
    }

    void menuItemsMarkFocusAndNotes()
    {
        const QStringList items{u"Steam OS"_s, u"Micro DAP"_s, u"Micro EMU"_s, u"Desktop"_s, u"Android"_s};
        const QStringList notes{QString(), u"not reviewed"_s, u"not installed, a to install"_s, u"closes the game"_s,
                                u"not installed"_s};
        m_overlay->openMenu(items, 0, notes);
        QObject *sheet = m_window->findChild<QObject *>(u"menuSheet"_s);
        QVERIFY(sheet);
        QTRY_VERIFY(sheet->property("shown").toBool());
        QCOMPARE(sheet->property("title").toString(), u"switch to"_s);
        QCOMPARE(sheet->property("items").toStringList(), items);
        QCOMPARE(sheet->property("mark").toInt(), 0);
        QCOMPARE(sheet->property("focusIndex").toInt(), 0);
        QVERIFY(sheet->property("wrap").toBool());
        QCOMPARE(sheet->property("lead").toString(), QString());
        QVERIFY(sheet->property("keepLead").toBool());
        m_pad->tap(FakePad::Down);
        QTRY_COMPARE(sheet->property("focusIndex").toInt(), 1);
        QCOMPARE(sheet->property("lead").toString(), u"not reviewed"_s);
        for (int i = 0; i < 3; ++i)
            m_pad->tap(FakePad::Down);
        QTRY_COMPARE(sheet->property("focusIndex").toInt(), 4);
        QCOMPARE(sheet->property("lead").toString(), u"not installed"_s);
        m_pad->tap(FakePad::Down);
        QTRY_COMPARE(sheet->property("focusIndex").toInt(), 0);
        QCOMPARE(sheet->property("lead").toString(), QString());
        m_pad->tap(FakePad::Up);
        QTRY_COMPARE(sheet->property("focusIndex").toInt(), 4);
        QCOMPARE(sheet->property("lead").toString(), u"not installed"_s);
    }

    void menuMarkOnCurrent()
    {
        const QStringList items{u"Steam OS"_s, u"Micro EMU"_s, u"Desktop"_s};
        const QStringList notes{QString(), u"not installed, a to install"_s, u"closes the game"_s};
        m_overlay->openMenu(items, 1, notes);
        QObject *sheet = m_window->findChild<QObject *>(u"menuSheet"_s);
        QVERIFY(sheet);
        QTRY_VERIFY(sheet->property("shown").toBool());
        QCOMPARE(sheet->property("mark").toInt(), 1);
        QCOMPARE(sheet->property("focusIndex").toInt(), 1);
        QCOMPARE(sheet->property("lead").toString(), u"not installed, a to install"_s);
    }

    void menuAcceptThroughRouter()
    {
        m_overlay->openMenu({u"Steam OS"_s, u"Micro DAP"_s}, 0, {QString(), QString()});
        m_pad->tap(FakePad::A);
        QTRY_COMPARE(m_overlay->accepts, 1);
        QCOMPARE(m_overlay->accepted, 0);
        QCOMPARE(m_overlay->closes, 0);
    }

    void menuBackClosesSheet()
    {
        m_overlay->openMenu({u"Steam OS"_s, u"Micro DAP"_s}, 0, {QString(), QString()});
        QObject *sheet = m_window->findChild<QObject *>(u"menuSheet"_s);
        QVERIFY(sheet);
        QTRY_VERIFY(sheet->property("shown").toBool());
        m_pad->tap(FakePad::B);
        QTRY_COMPARE(m_overlay->closes, 1);
        QTRY_VERIFY(!sheet->property("shown").toBool());
        QCOMPARE(m_overlay->accepts, 0);
    }

    void menuAcceptIgnoredWhileHint()
    {
        m_overlay->setHint(true);
        m_overlay->openMenu({u"Steam OS"_s, u"Micro DAP"_s}, 0, {QString(), QString()});
        QTest::qWait(50);
        m_pad->tap(FakePad::A);
        QTest::qWait(100);
        QCOMPARE(m_overlay->accepts, 0);
        m_overlay->setHint(false);
        m_pad->tap(FakePad::A);
        QTRY_COMPARE(m_overlay->accepts, 1);
    }

    void menuInputIgnoredWhileBusy()
    {
        m_overlay->openMenu({u"Steam OS"_s, u"Micro DAP"_s}, 0, {QString(), QString()});
        m_overlay->setMenuBusy(true);
        QTest::qWait(50);
        m_pad->tap(FakePad::A);
        m_pad->tap(FakePad::B);
        QTest::qWait(100);
        QCOMPARE(m_overlay->accepts, 0);
        QCOMPARE(m_overlay->closes, 0);
        m_overlay->setMenuBusy(false);
        m_pad->tap(FakePad::A);
        QTRY_COMPARE(m_overlay->accepts, 1);
    }

    void menuCancelsValueSheet()
    {
        QVERIFY(QMetaObject::invokeMethod(m_view, "open", Q_ARG(QString, u"audio"_s)));
        int index = -1;
        QVERIFY(QMetaObject::invokeMethod(page(), "indexOfKey", Q_RETURN_ARG(int, index),
                                          Q_ARG(QString, u"audio.volume_step"_s)));
        page()->setProperty("focusIndex", index);
        QVERIFY(QMetaObject::invokeMethod(page(), "activate"));
        QVERIFY(page()->property("valueOpen").toBool());
        m_overlay->openMenu({u"Steam OS"_s}, 0, {QString()});
        QTRY_VERIFY(!page()->property("valueOpen").toBool());
        QCOMPARE(m_overlay->hides, 0);
    }

    void menuOnlyTogglesStackVisibility()
    {
        QObject *stack = qvariant_cast<QObject *>(m_view->property("stack"));
        QVERIFY(stack);
        QVERIFY(stack->property("visible").toBool());
        m_overlay->openMenu({u"Steam OS"_s}, 0, {QString()});
        QTRY_VERIFY(!stack->property("visible").toBool());
        m_overlay->leaveMenu();
        QTRY_VERIFY(!stack->property("visible").toBool());
        m_overlay->resetMenu();
        QTRY_VERIFY(stack->property("visible").toBool());
        m_overlay->openMenu({u"Steam OS"_s}, 0, {QString()}, false);
        QTRY_VERIFY(stack->property("visible").toBool());
    }

    void menuToastHoldMatchesMotion()
    {
        QObject *motion = m_engine->singletonInstance<QObject *>(u"MicroSet"_s, u"Motion"_s);
        QVERIFY(motion);
        QCOMPARE(motion->property("toastHold").toInt(), Overlay::MenuToastMs);
    }

    void menuArmHidesThroughRouter()
    {
        cleanup();
        m_model = std::make_unique<SettingsModel>();
        m_model->setFixture(FakeMicroSetd::describe({FakeMicroSetd::shipped()}), {}, {});
        DbusPad pad(QDBusConnection(u"none"_s), QDBusConnection(u"none"_s), nullptr, false);
        Overlay overlay(&pad, nullptr);
        const auto engineFirst = qScopeGuard([this] { m_engine.reset(); });
        QQuickWindow *root = nullptr;
        overlay.setUi([&] {
            if (m_engine)
                return;
            m_engine = std::make_unique<QQmlApplicationEngine>();
            m_engine->setInitialProperties({
                {u"overlay"_s, QVariant::fromValue(static_cast<QObject *>(&overlay))},
                {u"model"_s, QVariant::fromValue(static_cast<QObject *>(m_model.get()))},
                {u"pad"_s, QVariant::fromValue(static_cast<QObject *>(&pad))},
            });
            m_engine->loadFromModule("MicroSetOverlay", "Overlay");
            root = qobject_cast<QQuickWindow *>(m_engine->rootObjects().value(0));
            QVERIFY(root);
            overlay.setWindow(root);
            overlay.setToastWindow(qvariant_cast<QWindow *>(root->property("toastWindow")));
        });
        const QList<SessionMenuEntry> entries{{u"steam"_s, u"Steam OS"_s, u"available"_s, false},
                                               {u"desktop"_s, u"Desktop"_s, u"available"_s, false}};
        overlay.showMenu(entries, u"steam"_s, false, false);
        QVERIFY(root);
        QVERIFY(overlay.shown());
        QVERIFY(overlay.menuOnly());
        QObject *sheet = root->findChild<QObject *>(u"menuSheet"_s);
        QVERIFY(sheet);
        QTRY_VERIFY(sheet->property("shown").toBool());
        emit pad.pressed(DbusPad::B);
        emit pad.action(DbusPad::B, false);
        QTRY_VERIFY(!overlay.menuOpen());
        QVERIFY(overlay.shown());
        emit pad.released(DbusPad::B);
        QTRY_VERIFY(!overlay.shown());
        QTRY_VERIFY(!root->isVisible());
    }

    void menuToastShowsAboveSheet()
    {
        m_overlay->openMenu({u"Steam OS"_s, u"Micro DAP"_s}, 0, {QString(), QString()});
        QObject *sheet = m_window->findChild<QObject *>(u"menuSheet"_s);
        QObject *toast = m_window->findChild<QObject *>(u"menuToast"_s);
        QVERIFY(sheet);
        QVERIFY(toast);
        auto *sheetItem = qobject_cast<QQuickItem *>(sheet);
        auto *toastItem = qobject_cast<QQuickItem *>(toast);
        QVERIFY(sheetItem);
        QVERIFY(toastItem);
        QCOMPARE(sheetItem->parentItem(), toastItem->parentItem());
        const QList<QQuickItem *> siblings = toastItem->parentItem()->childItems();
        QVERIFY(siblings.indexOf(toastItem) > siblings.indexOf(sheetItem));
        emit m_overlay->menuToast(u"switch failed"_s);
        QTRY_COMPARE(toast->property("title").toString(), u"switch failed"_s);
        QCOMPARE(toast->property("artist").toString(), QString());
    }
};

QTEST_MAIN(OverlayQmlTest)
#include "tst_overlayqml.moc"
