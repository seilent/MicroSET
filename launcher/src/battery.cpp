#include "battery.h"

#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDebug>
#include <QVariantMap>

using namespace Qt::StringLiterals;

Battery::Battery(const QDBusConnection &bus, QObject *parent)
    : QObject(parent)
    , m_bus(bus)
{
    if (!m_bus.isConnected())
        return;
    subscribe();
    read();
}

QString Battery::service()
{
    return u"org.freedesktop.UPower"_s;
}

QString Battery::path()
{
    return u"/org/freedesktop/UPower/devices/DisplayDevice"_s;
}

QString Battery::deviceInterface()
{
    return u"org.freedesktop.UPower.Device"_s;
}

QString Battery::propertiesInterface()
{
    return u"org.freedesktop.DBus.Properties"_s;
}

bool Battery::present() const
{
    return m_present;
}

int Battery::percent() const
{
    return m_percent;
}

bool Battery::low() const
{
    return m_percent < 10;
}

void Battery::subscribe()
{
    if (!m_bus.connect(service(), path(), propertiesInterface(), u"PropertiesChanged"_s, this,
                       SLOT(onProperties(QString,QVariantMap,QStringList))))
        qWarning("launcher: cannot watch the battery: %s", qPrintable(m_bus.lastError().message()));
}

void Battery::read()
{
    QDBusMessage call = QDBusMessage::createMethodCall(service(), path(), propertiesInterface(), u"GetAll"_s);
    call << deviceInterface();
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        const QDBusPendingReply<QVariantMap> reply = *w;
        if (reply.isError()) {
            setPresent(false);
            return;
        }
        apply(reply.value());
    });
}

void Battery::onProperties(const QString &interface, const QVariantMap &changed, const QStringList &invalidated)
{
    if (interface != deviceInterface())
        return;
    apply(changed);
    if (!invalidated.isEmpty())
        read();
}

void Battery::apply(const QVariantMap &values)
{
    if (values.contains(u"IsPresent"_s) && !values.value(u"IsPresent"_s).toBool()) {
        setPresent(false);
        return;
    }
    if (values.contains(u"Percentage"_s))
        setPercent(values.value(u"Percentage"_s).toInt());
    if (values.contains(u"State"_s) && values.value(u"State"_s).toInt() == 0) {
        setPresent(false);
        return;
    }
    if (values.contains(u"IsPresent"_s) || values.contains(u"Percentage"_s))
        setPresent(true);
}

void Battery::setPresent(bool present)
{
    if (present == m_present)
        return;
    m_present = present;
    emit presentChanged();
}

void Battery::setPercent(int percent)
{
    const int clamped = qBound(0, percent, 100);
    if (clamped == m_percent)
        return;
    m_percent = clamped;
    emit percentChanged();
}
