#include "wifi.h"

#include <QDBusArgument>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDebug>
#include <QVariantMap>

using namespace Qt::StringLiterals;

Wifi::Wifi(const QDBusConnection &bus, QObject *parent)
    : QObject(parent)
    , m_bus(bus)
{
    if (!m_bus.isConnected())
        return;
    qDBusRegisterMetaType<QList<QDBusObjectPath>>();
    subscribeManager();
    readState();
}

QString Wifi::service()
{
    return u"org.freedesktop.NetworkManager"_s;
}

QString Wifi::path()
{
    return u"/org/freedesktop/NetworkManager"_s;
}

QString Wifi::managerInterface()
{
    return u"org.freedesktop.NetworkManager"_s;
}

QString Wifi::connectionInterface()
{
    return u"org.freedesktop.NetworkManager.Connection.Active"_s;
}

QString Wifi::deviceInterface()
{
    return u"org.freedesktop.NetworkManager.Device.Wireless"_s;
}

QString Wifi::accessPointInterface()
{
    return u"org.freedesktop.NetworkManager.AccessPoint"_s;
}

QString Wifi::wirelessType()
{
    return u"802-11-wireless"_s;
}

QString Wifi::propertiesInterface()
{
    return u"org.freedesktop.DBus.Properties"_s;
}

bool Wifi::connected() const
{
    return m_connected;
}

QString Wifi::ssid() const
{
    return m_ssid;
}

void Wifi::subscribeManager()
{
    if (!m_bus.connect(service(), path(), propertiesInterface(), u"PropertiesChanged"_s, this,
                       SLOT(onManagerProperties(QString,QVariantMap,QStringList))))
        qWarning("launcher: cannot watch NetworkManager: %s", qPrintable(m_bus.lastError().message()));
}

void Wifi::readState()
{
    const int generation = ++m_generation;
    QDBusMessage call = QDBusMessage::createMethodCall(service(), path(), propertiesInterface(), u"Get"_s);
    call << managerInterface() << u"PrimaryConnection"_s;
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, generation](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        if (generation != m_generation)
            return;
        const QDBusPendingReply<QDBusVariant> reply = *w;
        if (reply.isError()) {
            dropPrimary();
            return;
        }
        readPrimary(qvariant_cast<QDBusObjectPath>(reply.value().variant()));
    });
}

void Wifi::readPrimary(QDBusObjectPath connection, bool reload)
{
    if (connection.path().isEmpty() || connection.path() == u"/"_s) {
        dropPrimary();
        return;
    }
    if (!reload && connection.path() == m_primary.path()) {
        if (m_primarySubscribed && !m_device.path().isEmpty())
            readDevice(m_device);
        return;
    }
    dropPrimary();
    m_primary = connection;
    m_primarySubscribed = m_bus.connect(service(), connection.path(), propertiesInterface(), u"PropertiesChanged"_s,
                                        this, SLOT(onPrimaryProperties(QString,QVariantMap,QStringList)));
    QDBusMessage call = QDBusMessage::createMethodCall(service(), connection.path(), propertiesInterface(), u"GetAll"_s);
    call << connectionInterface();
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, path = connection.path()](
                                                                  QDBusPendingCallWatcher *w) {
        w->deleteLater();
        if (path != m_primary.path())
            return;
        const QDBusPendingReply<QVariantMap> reply = *w;
        if (reply.isError()) {
            dropPrimary();
            return;
        }
        const QVariantMap values = reply.value();
        if (values.value(u"Type"_s).toString() != wirelessType()) {
            dropPrimary();
            return;
        }
        const QList<QDBusObjectPath> devices =
            qdbus_cast<QList<QDBusObjectPath>>(values.value(u"Devices"_s).value<QDBusArgument>());
        if (devices.isEmpty()) {
            dropPrimary();
            return;
        }
        readDevice(devices.constFirst());
    });
}

void Wifi::dropPrimary()
{
    if (m_primarySubscribed)
        m_bus.disconnect(service(), m_primary.path(), propertiesInterface(), u"PropertiesChanged"_s, this,
                         SLOT(onPrimaryProperties(QString,QVariantMap,QStringList)));
    m_primarySubscribed = false;
    m_primary = {};
    dropDevice();
    setConnected(false, {});
}

void Wifi::readDevice(QDBusObjectPath device)
{
    if (device.path().isEmpty() || device.path() == u"/"_s) {
        dropDevice();
        setConnected(false, {});
        return;
    }
    if (device.path() != m_device.path() || !m_deviceSubscribed) {
        dropDevice();
        m_device = device;
        m_deviceSubscribed = m_bus.connect(service(), device.path(), propertiesInterface(), u"PropertiesChanged"_s,
                                           this, SLOT(onDeviceProperties(QString,QVariantMap,QStringList)));
    }
    QDBusMessage call =
        QDBusMessage::createMethodCall(service(), device.path(), propertiesInterface(), u"Get"_s);
    call << deviceInterface() << u"ActiveAccessPoint"_s;
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, path = device.path()](
                                                                  QDBusPendingCallWatcher *w) {
        w->deleteLater();
        if (path != m_device.path())
            return;
        const QDBusPendingReply<QDBusVariant> reply = *w;
        if (reply.isError()) {
            dropAccessPoint();
            setConnected(false, {});
            return;
        }
        readSsid(qvariant_cast<QDBusObjectPath>(reply.value().variant()));
    });
}

void Wifi::dropDevice()
{
    if (m_deviceSubscribed)
        m_bus.disconnect(service(), m_device.path(), propertiesInterface(), u"PropertiesChanged"_s, this,
                         SLOT(onDeviceProperties(QString,QVariantMap,QStringList)));
    m_deviceSubscribed = false;
    m_device = {};
    dropAccessPoint();
}

void Wifi::readSsid(QDBusObjectPath accessPoint)
{
    if (accessPoint.path().isEmpty() || accessPoint.path() == u"/"_s) {
        dropAccessPoint();
        setConnected(false, {});
        return;
    }
    if (accessPoint.path() != m_accessPoint.path() || !m_apSubscribed) {
        dropAccessPoint();
        m_accessPoint = accessPoint;
        m_apSubscribed = m_bus.connect(service(), accessPoint.path(), propertiesInterface(), u"PropertiesChanged"_s,
                                       this, SLOT(onAccessPointProperties(QString,QVariantMap,QStringList)));
    }
    QDBusMessage call = QDBusMessage::createMethodCall(service(), accessPoint.path(), propertiesInterface(), u"Get"_s);
    call << accessPointInterface() << u"Ssid"_s;
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, path = accessPoint.path()](
                                                                  QDBusPendingCallWatcher *w) {
        w->deleteLater();
        if (path != m_accessPoint.path())
            return;
        const QDBusPendingReply<QDBusVariant> reply = *w;
        if (reply.isError()) {
            setConnected(false, {});
            return;
        }
        const QByteArray raw = reply.value().variant().toByteArray();
        setConnected(!raw.isEmpty(), QString::fromUtf8(raw));
    });
}

void Wifi::dropAccessPoint()
{
    if (m_apSubscribed)
        m_bus.disconnect(service(), m_accessPoint.path(), propertiesInterface(), u"PropertiesChanged"_s, this,
                         SLOT(onAccessPointProperties(QString,QVariantMap,QStringList)));
    m_apSubscribed = false;
    m_accessPoint = {};
}

void Wifi::onManagerProperties(const QString &interface, const QVariantMap &changed, const QStringList &invalidated)
{
    if (interface != managerInterface())
        return;
    if (changed.contains(u"PrimaryConnection"_s))
        readPrimary(qvariant_cast<QDBusObjectPath>(changed.value(u"PrimaryConnection"_s)));
    else if (invalidated.contains(u"PrimaryConnection"_s) || invalidated.contains(u"State"_s))
        readState();
}

void Wifi::onPrimaryProperties(const QString &interface, const QVariantMap &changed, const QStringList &invalidated)
{
    if (interface != connectionInterface())
        return;
    if (changed.contains(u"Devices"_s) || invalidated.contains(u"Devices"_s))
        readPrimary(m_primary, true);
}

void Wifi::onDeviceProperties(const QString &interface, const QVariantMap &changed, const QStringList &invalidated)
{
    if (interface != deviceInterface())
        return;
    if (changed.contains(u"ActiveAccessPoint"_s))
        readSsid(qvariant_cast<QDBusObjectPath>(changed.value(u"ActiveAccessPoint"_s)));
    else if (invalidated.contains(u"ActiveAccessPoint"_s))
        readDevice(m_device);
}

void Wifi::onAccessPointProperties(const QString &interface, const QVariantMap &changed,
                                   const QStringList &invalidated)
{
    if (interface != accessPointInterface())
        return;
    if (changed.contains(u"Ssid"_s)) {
        const QByteArray raw = changed.value(u"Ssid"_s).toByteArray();
        setConnected(!raw.isEmpty(), QString::fromUtf8(raw));
    } else if (invalidated.contains(u"Ssid"_s)) {
        readSsid(m_accessPoint);
    }
}

void Wifi::setConnected(bool connected, const QString &ssid)
{
    if (ssid != m_ssid) {
        m_ssid = ssid;
        emit ssidChanged();
    }
    if (connected == m_connected)
        return;
    m_connected = connected;
    emit connectedChanged();
}
