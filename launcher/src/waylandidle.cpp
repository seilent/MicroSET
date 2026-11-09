#include "waylandidle.h"

#include "idle-inhibit-unstable-v1-client-protocol.h"

#include <QDebug>
#include <QGuiApplication>
#include <QTimer>

#include <cstring>

#include <wayland-client.h>

namespace {

void onGlobal(void *data, wl_registry *registry, uint32_t name, const char *interface, uint32_t version)
{
    Q_UNUSED(version)
    if (std::strcmp(interface, zwp_idle_inhibit_manager_v1_interface.name) != 0)
        return;
    static_cast<WaylandIdlePulse *>(data)->takeManager(registry, name);
}

void onGlobalRemove(void *, wl_registry *, uint32_t)
{
}

const wl_registry_listener registryListener = {onGlobal, onGlobalRemove};

}

WaylandIdlePulse::WaylandIdlePulse(QObject *parent)
    : IdlePulse(parent)
{
    auto *wayland = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    if (wayland == nullptr || wayland->display() == nullptr) {
        qWarning("launcher: no Wayland display, the idle pulse is off");
        return;
    }
    m_display = wayland->display();
    wl_registry *registry = wl_display_get_registry(m_display);
    if (registry == nullptr) {
        qWarning("launcher: cannot read the Wayland registry, the idle pulse is off");
        return;
    }
    wl_registry_add_listener(registry, &registryListener, this);
    wl_display_roundtrip(m_display);
    wl_registry_destroy(registry);
    if (m_manager == nullptr)
        qWarning("launcher: the compositor has no zwp_idle_inhibit_manager_v1, the idle pulse is off");
}

WaylandIdlePulse::~WaylandIdlePulse()
{
    if (m_inhibitor != nullptr)
        zwp_idle_inhibitor_v1_destroy(m_inhibitor);
    if (m_manager != nullptr)
        zwp_idle_inhibit_manager_v1_destroy(m_manager);
}

void WaylandIdlePulse::takeManager(wl_registry *registry, uint32_t name)
{
    m_manager = static_cast<zwp_idle_inhibit_manager_v1 *>(
        wl_registry_bind(registry, name, &zwp_idle_inhibit_manager_v1_interface, 1));
}

bool WaylandIdlePulse::ready() const
{
    return m_display != nullptr && m_manager != nullptr && m_surface != nullptr;
}

void WaylandIdlePulse::attach(wl_surface *surface)
{
    m_surface = surface;
}

void WaylandIdlePulse::pulse()
{
    if (m_manager == nullptr || m_surface == nullptr)
        return;
    if (m_inhibitor != nullptr) {
        zwp_idle_inhibitor_v1_destroy(m_inhibitor);
        m_inhibitor = nullptr;
    }
    m_inhibitor = zwp_idle_inhibit_manager_v1_create_inhibitor(m_manager, m_surface);
    wl_display_flush(m_display);
    counted();
    QTimer::singleShot(0, this, [this] {
        if (m_inhibitor != nullptr) {
            zwp_idle_inhibitor_v1_destroy(m_inhibitor);
            m_inhibitor = nullptr;
        }
    });
}
