#include "framelimit.h"

#include <QGuiApplication>

#include <cstdlib>

namespace microset {

namespace {

constexpr char LimitName[] = "GAMESCOPE_FPS_LIMIT_FEEDBACK";
constexpr char RefreshName[] = "GAMESCOPE_DISPLAY_REFRESH_RATE_FEEDBACK";

xcb_atom_t interned(xcb_connection_t *connection, xcb_intern_atom_cookie_t cookie)
{
    xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(connection, cookie, nullptr);
    const xcb_atom_t result = reply ? reply->atom : XCB_ATOM_NONE;
    std::free(reply);
    return result;
}

}

bool FrameLimitWatch::start()
{
    auto *x11 = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
    if (m_connection || !x11 || !x11->connection())
        return false;
    xcb_connection_t *connection = x11->connection();
    const xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(connection)).data;
    if (!screen)
        return false;
    const xcb_intern_atom_cookie_t limit = xcb_intern_atom(connection, 0, sizeof(LimitName) - 1, LimitName);
    const xcb_intern_atom_cookie_t refresh = xcb_intern_atom(connection, 0, sizeof(RefreshName) - 1, RefreshName);
    const xcb_get_window_attributes_cookie_t asked = xcb_get_window_attributes(connection, screen->root);
    m_limitAtom = interned(connection, limit);
    m_refreshAtom = interned(connection, refresh);
    xcb_get_window_attributes_reply_t *attributes = xcb_get_window_attributes_reply(connection, asked, nullptr);
    if (!attributes)
        return false;
    const uint32_t mask = attributes->your_event_mask | XCB_EVENT_MASK_PROPERTY_CHANGE;
    std::free(attributes);
    xcb_change_window_attributes(connection, screen->root, XCB_CW_EVENT_MASK, &mask);
    m_connection = connection;
    m_root = screen->root;
    m_limit = read(m_limitAtom);
    m_refresh = read(m_refreshAtom);
    qGuiApp->installNativeEventFilter(this);
    return true;
}

bool FrameLimitWatch::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *)
{
    if (eventType != "xcb_generic_event_t")
        return false;
    const auto *event = static_cast<const xcb_generic_event_t *>(message);
    if ((event->response_type & ~0x80) != XCB_PROPERTY_NOTIFY)
        return false;
    const auto *notify = reinterpret_cast<const xcb_property_notify_event_t *>(event);
    if (notify->window != m_root)
        return false;
    int *value = nullptr;
    if (notify->atom == m_limitAtom)
        value = &m_limit;
    else if (notify->atom == m_refreshAtom)
        value = &m_refresh;
    else
        return false;
    const int now = read(notify->atom);
    if (now != *value) {
        *value = now;
        emit changed();
    }
    return false;
}

int FrameLimitWatch::read(xcb_atom_t property) const
{
    if (property == XCB_ATOM_NONE)
        return -1;
    const xcb_get_property_cookie_t asked
        = xcb_get_property(m_connection, 0, m_root, property, XCB_ATOM_CARDINAL, 0, 1);
    xcb_get_property_reply_t *reply = xcb_get_property_reply(m_connection, asked, nullptr);
    int value = -1;
    if (reply && reply->format == 32 && reply->bytes_after == 0 && xcb_get_property_value_length(reply) == 4)
        value = int(*static_cast<const uint32_t *>(xcb_get_property_value(reply)));
    std::free(reply);
    return value;
}

}
