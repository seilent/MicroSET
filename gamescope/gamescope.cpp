#include "gamescope.h"

#include <QGuiApplication>
#include <QTextStream>
#include <QWindow>

#include <xcb/xcb.h>

#include <cstdlib>
#include <cstring>

namespace microset {

namespace {

xcb_atom_t atom(xcb_connection_t *connection, const char *name)
{
    const xcb_intern_atom_cookie_t cookie
        = xcb_intern_atom(connection, 0, static_cast<uint16_t>(std::strlen(name)), name);
    xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(connection, cookie, nullptr);
    if (!reply)
        return XCB_ATOM_NONE;
    const xcb_atom_t result = reply->atom;
    std::free(reply);
    return result;
}

void setCardinal(xcb_connection_t *connection, xcb_window_t window, xcb_atom_t property, uint32_t value)
{
    if (property == XCB_ATOM_NONE)
        return;
    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window, property, XCB_ATOM_CARDINAL, 32, 1,
                        &value);
}

}

bool markOverlay(QWindow *window)
{
    auto *x11 = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
    if (!x11 || !x11->connection() || !window || !window->handle())
        return false;
    xcb_connection_t *connection = x11->connection();
    setCardinal(connection, static_cast<xcb_window_t>(window->winId()), atom(connection, "STEAM_OVERLAY"), 1);
    xcb_flush(connection);
    return true;
}

void claimGamescopeFocus(QWindow *window)
{
    bool ok = false;
    const uint32_t appId = qEnvironmentVariable("MICROSET_GAMESCOPE_APPID").toUInt(&ok, 0);
    if (!ok || appId == 0 || !window)
        return;
    auto *x11 = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
    if (!x11) {
        QTextStream(stderr) << "microset-recovery: MICROSET_GAMESCOPE_APPID set but the platform is not xcb\n";
        return;
    }
    xcb_connection_t *connection = x11->connection();
    const xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(connection)).data;
    setCardinal(connection, static_cast<xcb_window_t>(window->winId()), atom(connection, "STEAM_GAME"), appId);
    if (screen)
        setCardinal(connection, screen->root, atom(connection, "GAMESCOPECTRL_BASELAYER_APPID"), appId);
    xcb_flush(connection);
}

}
