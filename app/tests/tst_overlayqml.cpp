#include "dbuspad.h"
#include "fakemicrosetd.h"
#include "overlay.h"
#include "settingsmodel.h"

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QScopeGuard>
#include <QQuickWindow>
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
    Q_PROPERTY(bool shown READ shown CONSTANT)
    Q_PROPERTY(bool mapped READ mapped CONSTANT)
    Q_PROPERTY(qreal opacity READ opacity CONSTANT)
    Q_PROPERTY(bool hintShown READ hintShown CONSTANT)
    Q_PROPERTY(bool hint READ hint NOTIFY hintChanged)
    Q_PROPERTY(QString hintDevice READ hintDevice CONSTANT)

public:
    bool shown() const { return true; }
    bool mapped() const { return true; }
    qreal opacity() const { return 1.0; }
    bool hintShown() const { return false; }
    bool hint() const { return m_hint; }
    QString hintDevice() const { return u"/dev/mmcblk0"_s; }
    void setHint(bool hint)
    {
        m_hint = hint;
        emit hintChanged();
    }
    Q_INVOKABLE void hide() { ++hides; }
    Q_INVOKABLE void hintDone() {}

    int hides = 0;

signals:
    void pageHint();
    void hintShownChanged();
    void hintChanged();

private:
    bool m_hint = false;
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
};

QTEST_MAIN(OverlayQmlTest)
#include "tst_overlayqml.moc"
