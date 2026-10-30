#include "framelimit.h"

#include <QAbstractNativeEventFilter>
#include <QGuiApplication>
#include <QSignalSpy>
#include <QTest>

#include <cstdlib>
#include <cstring>

class NotifyCount : public QAbstractNativeEventFilter
{
public:
    xcb_window_t root = XCB_WINDOW_NONE;
    xcb_atom_t atom = XCB_ATOM_NONE;
    int seen = 0;

    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *) override
    {
        const auto *event = static_cast<const xcb_generic_event_t *>(message);
        if (eventType != "xcb_generic_event_t" || (event->response_type & ~0x80) != XCB_PROPERTY_NOTIFY)
            return false;
        const auto *notify = reinterpret_cast<const xcb_property_notify_event_t *>(event);
        if (notify->window == root && notify->atom == atom)
            ++seen;
        return false;
    }
};

class FrameLimitTest : public QObject
{
    Q_OBJECT

    static constexpr char Limit[] = "GAMESCOPE_FPS_LIMIT_FEEDBACK";
    static constexpr char Refresh[] = "GAMESCOPE_DISPLAY_REFRESH_RATE_FEEDBACK";

    xcb_connection_t *m_connection = nullptr;
    xcb_window_t m_root = XCB_WINDOW_NONE;
    NotifyCount m_barrier;
    uint32_t m_tick = 0;

    xcb_atom_t atom(const char *name)
    {
        const xcb_intern_atom_cookie_t cookie
            = xcb_intern_atom(m_connection, 0, static_cast<uint16_t>(std::strlen(name)), name);
        xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(m_connection, cookie, nullptr);
        const xcb_atom_t result = reply ? reply->atom : XCB_ATOM_NONE;
        std::free(reply);
        return result;
    }

    void put(const char *name, xcb_atom_t type, uint8_t format, uint32_t count, const void *data)
    {
        xcb_change_property(m_connection, XCB_PROP_MODE_REPLACE, m_root, atom(name), type, format, count, data);
        xcb_flush(m_connection);
    }

    void cardinal(const char *name, uint32_t value) { put(name, XCB_ATOM_CARDINAL, 32, 1, &value); }

    void remove(const char *name)
    {
        xcb_delete_property(m_connection, m_root, atom(name));
        xcb_flush(m_connection);
    }

    uint32_t mask()
    {
        xcb_get_window_attributes_reply_t *reply
            = xcb_get_window_attributes_reply(m_connection, xcb_get_window_attributes(m_connection, m_root), nullptr);
        const uint32_t result = reply ? reply->your_event_mask : 0;
        std::free(reply);
        return result;
    }

    bool settled()
    {
        const int before = m_barrier.seen;
        cardinal("MICROSET_TEST_BARRIER", ++m_tick);
        return QTest::qWaitFor([&] { return m_barrier.seen > before; });
    }

private slots:
    void initTestCase()
    {
        auto *x11 = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
        QVERIFY(x11 && x11->connection());
        m_connection = x11->connection();
        m_root = xcb_setup_roots_iterator(xcb_get_setup(m_connection)).data->root;
    }

    void watch()
    {
        const uint32_t rest = mask() & ~uint32_t(XCB_EVENT_MASK_PROPERTY_CHANGE);
        QVERIFY(rest != 0);
        xcb_change_window_attributes(m_connection, m_root, XCB_CW_EVENT_MASK, &rest);
        QCOMPARE(mask(), rest);

        NotifyCount behind;
        behind.root = m_root;
        behind.atom = atom(Limit);
        qGuiApp->installNativeEventFilter(&behind);
        microset::FrameLimitWatch watch;
        QSignalSpy changed(&watch, &microset::FrameLimitWatch::changed);
        QVERIFY(watch.start());
        QCOMPARE(mask(), rest | XCB_EVENT_MASK_PROPERTY_CHANGE);
        QCOMPARE(watch.limit(), -1);
        QCOMPARE(watch.refresh(), -1);
        m_barrier.root = m_root;
        m_barrier.atom = atom("MICROSET_TEST_BARRIER");
        qGuiApp->installNativeEventFilter(&m_barrier);

        cardinal(Limit, 30);
        cardinal(Refresh, 60);
        QTRY_COMPARE(watch.refresh(), 60);
        QCOMPARE(watch.limit(), 30);
        QCOMPARE(changed.count(), 2);
        QCOMPARE(behind.seen, 1);
        {
            microset::FrameLimitWatch late;
            QVERIFY(late.start());
            QCOMPARE(late.limit(), 30);
            QCOMPARE(late.refresh(), 60);
        }

        cardinal(Limit, 60);
        QTRY_COMPARE(watch.limit(), 60);
        QCOMPARE(changed.count(), 3);
        cardinal(Limit, 60);
        QVERIFY(settled());
        QCOMPARE(changed.count(), 3);

        cardinal("GAMESCOPE_FPS_LIMIT", 30);
        QVERIFY(settled());
        QCOMPARE(watch.limit(), 60);
        QCOMPARE(changed.count(), 3);
        cardinal("MICROSET_TEST_OTHER", 30);
        QVERIFY(settled());
        QCOMPARE(changed.count(), 3);

        const uint8_t small = 30;
        put(Limit, XCB_ATOM_CARDINAL, 8, 1, &small);
        QVERIFY(settled());
        QCOMPARE(watch.limit(), -1);
        QCOMPARE(changed.count(), 4);
        cardinal(Limit, 60);
        QTRY_COMPARE(watch.limit(), 60);
        const uint16_t pair[] = {30, 0};
        put(Limit, XCB_ATOM_CARDINAL, 16, 2, pair);
        QVERIFY(settled());
        QCOMPARE(watch.limit(), -1);
        QCOMPARE(changed.count(), 6);
        cardinal(Limit, 60);
        QTRY_COMPARE(watch.limit(), 60);
        const uint32_t two[] = {30, 40};
        put(Limit, XCB_ATOM_CARDINAL, 32, 2, two);
        QVERIFY(settled());
        QCOMPARE(watch.limit(), -1);
        QCOMPARE(changed.count(), 8);
        cardinal(Limit, 60);
        QTRY_COMPARE(watch.limit(), 60);
        const uint32_t thirty = 30;
        put(Limit, XCB_ATOM_INTEGER, 32, 1, &thirty);
        QVERIFY(settled());
        QCOMPARE(watch.limit(), -1);
        QCOMPARE(changed.count(), 10);

        cardinal(Limit, 45);
        QTRY_COMPARE(watch.limit(), 45);
        QCOMPARE(changed.count(), 11);
        remove(Limit);
        QTRY_COMPARE(watch.limit(), -1);
        QCOMPARE(changed.count(), 12);
        remove(Refresh);
        QTRY_COMPARE(watch.refresh(), -1);
        QCOMPARE(changed.count(), 13);
        QCOMPARE(mask(), rest | XCB_EVENT_MASK_PROPERTY_CHANGE);
    }
};

QTEST_MAIN(FrameLimitTest)
#include "tst_framelimit.moc"
