#include "androidstatus.h"

#include <QDBusArgument>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>

using namespace Qt::StringLiterals;

namespace {

const QString Properties = u"org.freedesktop.DBus.Properties"_s;
const QString SystemdService = u"org.freedesktop.systemd1"_s;
const QString SystemdPath = u"/org/freedesktop/systemd1"_s;
const QString SystemdManager = u"org.freedesktop.systemd1.Manager"_s;

}

AndroidStatus::AndroidStatus(const QDBusConnection &bus, QObject *parent)
    : QObject(parent)
    , m_bus(bus)
    , m_watcher(Service, bus, QDBusServiceWatcher::WatchForOwnerChange)
{
    connect(&m_watcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
            [this](const QString &, const QString &, const QString &owner) {
                if (!owner.isEmpty()) {
                    fetch();
                    return;
                }
                ++m_ask;
                m_since.clear();
                setStatus({});
            });
    if (!m_bus.connect(Service, Path, Properties, u"PropertiesChanged"_s, this,
                       SLOT(onPropertiesChanged(QString,QVariantMap,QStringList))))
        qInfo("microset overlay: cannot watch PropertiesChanged of %s: %s", qPrintable(Service),
              qPrintable(m_bus.lastError().message()));
    fetch();
}

QVariantMap AndroidStatus::status() const
{
    return m_status;
}

void AndroidStatus::installGooglePlay()
{
    if (m_starting)
        return;
    m_starting = true;
    QDBusMessage call = QDBusMessage::createMethodCall(SystemdService, SystemdPath, SystemdManager, u"StartUnit"_s);
    call << GooglePlayUnit << u"replace"_s;
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        watcher->deleteLater();
        m_starting = false;
        const QDBusMessage reply = watcher->reply();
        if (reply.type() == QDBusMessage::ReplyMessage)
            return;
        qInfo("microset overlay: start of %s failed: %s %s", qPrintable(GooglePlayUnit), qPrintable(reply.errorName()),
              qPrintable(reply.errorMessage()));
        emit failed();
    });
}

void AndroidStatus::onPropertiesChanged(const QString &interface, const QVariantMap &changed, const QStringList &invalidated)
{
    if (interface != Interface)
        return;
    QVariantMap status = m_status;
    status.insert(changed);
    m_since.insert(changed);
    for (const QString &name : invalidated) {
        status.remove(name);
        m_since.remove(name);
    }
    setStatus(status);
}

void AndroidStatus::fetch()
{
    const quint64 ask = ++m_ask;
    m_since.clear();
    QDBusMessage call = QDBusMessage::createMethodCall(Service, Path, Properties, u"GetAll"_s);
    call << Interface;
    call.setAutoStartService(false);
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call, CallMs), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, ask] {
        watcher->deleteLater();
        if (ask != m_ask)
            return;
        const QDBusMessage reply = watcher->reply();
        if (reply.type() == QDBusMessage::ReplyMessage) {
            QVariantMap status = qdbus_cast<QVariantMap>(reply.arguments().value(0));
            status.insert(m_since);
            setStatus(status);
            return;
        }
        if (reply.errorName() == "org.freedesktop.DBus.Error.ServiceUnknown"_L1
            || reply.errorName() == "org.freedesktop.DBus.Error.NameHasNoOwner"_L1) {
            setStatus({});
            return;
        }
        qInfo("microset overlay: GetAll of %s failed: %s %s", qPrintable(Service), qPrintable(reply.errorName()),
              qPrintable(reply.errorMessage()));
    });
}

void AndroidStatus::setStatus(const QVariantMap &status)
{
    if (status == m_status)
        return;
    m_status = status;
    emit changed();
}
