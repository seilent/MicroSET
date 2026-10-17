#include "overlayservice.h"

#include "auth.h"
#include "overlay.h"

#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QPointer>
#include <QVariantMap>

#include <utility>

using namespace Qt::StringLiterals;

OverlayService::OverlayService(Overlay *overlay, const Auth &auth, QObject *parent)
    : QObject(parent)
    , m_overlay(overlay)
    , m_auth(auth)
    , m_bus(QString())
{
    connect(m_overlay, &Overlay::shownChanged, this, &OverlayService::announce);
}

QString OverlayService::service()
{
    return u"net.seilent.MicroSet.Overlay"_s;
}

QString OverlayService::path()
{
    return u"/net/seilent/MicroSet/Overlay"_s;
}

QString OverlayService::interface()
{
    return u"net.seilent.MicroSet.Overlay1"_s;
}

bool OverlayService::publish(QDBusConnection bus)
{
    m_bus = bus;
    if (!bus.registerObject(path(), this,
                            QDBusConnection::ExportScriptableSlots | QDBusConnection::ExportScriptableProperties))
        return false;
    return bus.interface()->registerService(service(), QDBusConnectionInterface::DontQueueService,
                                            QDBusConnectionInterface::DontAllowReplacement)
        == QDBusConnectionInterface::ServiceRegistered;
}

bool OverlayService::shown() const
{
    return m_overlay->shown();
}

void OverlayService::Toggle()
{
    run("Toggle", &Overlay::toggle);
}

void OverlayService::Show()
{
    run("Show", &Overlay::show);
}

void OverlayService::Hide()
{
    run("Hide", &Overlay::hide);
}

void OverlayService::run(const char *method, void (Overlay::*action)())
{
    setDelayedReply(true);
    const QDBusMessage request = message();
    QDBusConnection bus = connection();
    QPointer<Overlay> overlay(m_overlay);
    auto done = [request, bus, overlay, method, action](const QString &refusal, qint64 uid) mutable {
        if (!refusal.isEmpty()) {
            qInfo("microset overlay: %s by uid %lld refused: %s", method, uid, qPrintable(refusal));
            bus.send(request.createErrorReply(QDBusError::AccessDenied, refusal));
            return;
        }
        if (overlay)
            (overlay.data()->*action)();
        bus.send(request.createReply());
    };
    m_auth.check(request.service(), std::move(done));
}

void OverlayService::announce()
{
    QDBusMessage changed = QDBusMessage::createSignal(path(), u"org.freedesktop.DBus.Properties"_s,
                                                      u"PropertiesChanged"_s);
    changed << interface() << QVariantMap{{u"Shown"_s, m_overlay->shown()}} << QStringList();
    m_bus.send(changed);
}
