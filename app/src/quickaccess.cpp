#include "quickaccess.h"

#include <QDBusMessage>
#include <QDBusPendingCallWatcher>

using namespace Qt::StringLiterals;

QuickAccess::QuickAccess(const QDBusConnection &bus, const QString &session, QObject *parent)
    : QObject(parent)
    , m_bus(bus)
    , m_watcher(Service, bus, QDBusServiceWatcher::WatchForOwnerChange)
    , m_steam(session == "steam"_L1)
{
    connect(&m_watcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
            [this](const QString &, const QString &, const QString &owner) {
                m_known = true;
                setOwned(!owner.isEmpty());
            });
    QDBusMessage ask = QDBusMessage::createMethodCall(u"org.freedesktop.DBus"_s, u"/org/freedesktop/DBus"_s,
                                                      u"org.freedesktop.DBus"_s, u"NameHasOwner"_s);
    ask << Service;
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(ask, CallMs), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        watcher->deleteLater();
        const QDBusMessage reply = watcher->reply();
        if (!m_known && reply.type() == QDBusMessage::ReplyMessage)
            setOwned(reply.arguments().value(0).toBool());
    });
}

bool QuickAccess::available() const
{
    return m_steam && m_owned;
}

void QuickAccess::open()
{
    if (!available() || m_calling)
        return;
    m_calling = true;
    QDBusMessage call = QDBusMessage::createMethodCall(Service, Path, Interface, u"OpenQuickAccess"_s);
    call.setAutoStartService(false);
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call, CallMs), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        watcher->deleteLater();
        m_calling = false;
        const QDBusMessage reply = watcher->reply();
        if (reply.type() == QDBusMessage::ReplyMessage) {
            emit opened();
            return;
        }
        qInfo("microset overlay: OpenQuickAccess failed: %s %s", qPrintable(reply.errorName()),
              qPrintable(reply.errorMessage()));
        emit failed();
    });
}

void QuickAccess::setOwned(bool owned)
{
    if (owned == m_owned)
        return;
    const bool was = available();
    m_owned = owned;
    if (available() != was)
        emit availableChanged();
}
