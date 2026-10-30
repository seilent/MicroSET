#pragma once

#include <QAbstractNativeEventFilter>
#include <QObject>

#include <xcb/xcb.h>

namespace microset {

class FrameLimitWatch : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT

public:
    using QObject::QObject;

    bool start();
    int limit() const { return m_limit; }
    int refresh() const { return m_refresh; }

    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

signals:
    void changed();

private:
    int read(xcb_atom_t property) const;

    xcb_connection_t *m_connection = nullptr;
    xcb_window_t m_root = XCB_WINDOW_NONE;
    xcb_atom_t m_limitAtom = XCB_ATOM_NONE;
    xcb_atom_t m_refreshAtom = XCB_ATOM_NONE;
    int m_limit = -1;
    int m_refresh = -1;
};

}
