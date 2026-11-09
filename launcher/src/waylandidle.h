#pragma once

#include "idlepulse.h"

struct wl_display;
struct wl_registry;
struct wl_surface;
struct zwp_idle_inhibit_manager_v1;
struct zwp_idle_inhibitor_v1;

class WaylandIdlePulse : public IdlePulse
{
    Q_OBJECT

public:
    explicit WaylandIdlePulse(QObject *parent = nullptr);
    ~WaylandIdlePulse() override;

    void pulse() override;
    bool ready() const override;

    void attach(wl_surface *surface);
    void takeManager(wl_registry *registry, unsigned int name);

private:
    wl_display *m_display = nullptr;
    wl_surface *m_surface = nullptr;
    zwp_idle_inhibit_manager_v1 *m_manager = nullptr;
    zwp_idle_inhibitor_v1 *m_inhibitor = nullptr;
};
