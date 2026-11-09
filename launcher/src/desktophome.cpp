#include "desktophome.h"

#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QDebug>
#include <QVariantMap>

using namespace Qt::StringLiterals;

namespace {

QString propertiesInterface()
{
    return u"org.freedesktop.DBus.Properties"_s;
}

struct WindowRow
{
    QString address;
    QString cls;
    QString title;
};

QDBusArgument &operator<<(QDBusArgument &argument, const WindowRow &row)
{
    argument.beginStructure();
    argument << row.address << row.cls << row.title;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, WindowRow &row)
{
    argument.beginStructure();
    argument >> row.address >> row.cls >> row.title;
    argument.endStructure();
    return argument;
}

QVariantList toList(const QVariant &value)
{
    QVariantList out;
    if (!value.isValid())
        return out;
    if (value.metaType() != QMetaType::fromType<QDBusArgument>()) {
        const QVariantList rows = value.toList();
        for (const QVariant &row : rows)
            out << row;
        return out;
    }
    const QDBusArgument argument = value.value<QDBusArgument>();
    argument.beginArray();
    while (!argument.atEnd()) {
        WindowRow row;
        argument >> row;
        out << QVariantMap{{u"address"_s, row.address}, {u"class"_s, row.cls}, {u"title"_s, row.title}};
    }
    argument.endArray();
    return out;
}

}

DesktopHome::DesktopHome(QObject *parent)
    : QObject(parent)
{
}

QString DesktopHome::service()
{
    return u"net.seilent.MicroOs.DesktopHome"_s;
}

QString DesktopHome::path()
{
    return u"/net/seilent/MicroOs/DesktopHome"_s;
}

QString DesktopHome::interface()
{
    return u"net.seilent.MicroOs.DesktopHome1"_s;
}

bool DesktopHome::available() const
{
    return m_available;
}

bool DesktopHome::shown() const
{
    return m_shown;
}

QVariantList DesktopHome::windows() const
{
    return m_windows;
}

uint DesktopHome::transform() const
{
    return m_transform;
}

void DesktopHome::setAvailable(bool available)
{
    if (available == m_available)
        return;
    m_available = available;
    emit availableChanged();
}

void DesktopHome::setShown(bool shown)
{
    if (shown == m_shown)
        return;
    m_shown = shown;
    emit shownChanged();
}

void DesktopHome::setWindows(const QVariantList &windows)
{
    if (windows == m_windows)
        return;
    m_windows = windows;
    emit windowsChanged();
}

void DesktopHome::setTransform(uint transform)
{
    if (transform == m_transform)
        return;
    m_transform = transform;
    emit transformChanged();
}

HomeClient::HomeClient(const QDBusConnection &bus, QObject *parent)
    : DesktopHome(parent)
    , m_bus(bus)
    , m_watcher(service(), bus, QDBusServiceWatcher::WatchForOwnerChange)
{
    connect(&m_watcher, &QDBusServiceWatcher::serviceOwnerChanged, this, &HomeClient::onOwner);
    if (!m_bus.isConnected()) {
        qWarning("launcher: session bus not connected, the home service is down");
        return;
    }
    m_subscribed = m_bus.connect(service(), path(), propertiesInterface(), u"PropertiesChanged"_s, this,
                                 SLOT(onProperties(QString,QVariantMap,QStringList)));
    const QString owner = m_bus.interface()->serviceOwner(service()).value();
    if (owner.isEmpty()) {
        qInfo("launcher: no owner of %s, the home shows and the pad stays with the apps", qPrintable(service()));
        return;
    }
    setAvailable(true);
    subscribe();
    read();
}

void HomeClient::onOwner(const QString &service, const QString &oldOwner, const QString &newOwner)
{
    Q_UNUSED(oldOwner)
    if (newOwner.isEmpty()) {
        qInfo("launcher: %s left the bus, the home shows and the pad stays with the apps", qPrintable(service));
        setAvailable(false);
        setShown(false);
        setWindows({});
        return;
    }
    qInfo("launcher: %s is up, reading the home state", qPrintable(service));
    setAvailable(true);
    subscribe();
    read();
}

void HomeClient::subscribe()
{
    if (m_subscribed)
        return;
    m_subscribed = m_bus.connect(service(), path(), propertiesInterface(), u"PropertiesChanged"_s, this,
                                 SLOT(onProperties(QString,QVariantMap,QStringList)));
    if (!m_subscribed)
        qWarning("launcher: cannot watch the home properties: %s", qPrintable(m_bus.lastError().message()));
}

void HomeClient::read()
{
    QDBusMessage call = QDBusMessage::createMethodCall(service(), path(), propertiesInterface(), u"GetAll"_s);
    call << interface();
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        const QDBusPendingReply<QVariantMap> reply = *w;
        if (reply.isError()) {
            qWarning("launcher: cannot read the home state: %s", qPrintable(reply.error().message()));
            return;
        }
        const QVariantMap values = reply.value();
        if (values.contains(u"Shown"_s))
            setShown(values.value(u"Shown"_s).toBool());
        if (values.contains(u"Windows"_s))
            setWindows(toList(values.value(u"Windows"_s)));
        if (values.contains(u"Transform"_s))
            setTransform(values.value(u"Transform"_s).toUInt());
    });
}

void HomeClient::onProperties(const QString &interface, const QVariantMap &changed, const QStringList &invalidated)
{
    if (interface != HomeClient::interface())
        return;
    if (changed.contains(u"Shown"_s))
        setShown(changed.value(u"Shown"_s).toBool());
    if (changed.contains(u"Windows"_s))
        setWindows(toList(changed.value(u"Windows"_s)));
    if (changed.contains(u"Transform"_s))
        setTransform(changed.value(u"Transform"_s).toUInt());
    if (!invalidated.isEmpty())
        read();
}

void HomeClient::enter()
{
    call(u"Enter"_s, {});
}

void HomeClient::toggle()
{
    call(u"Toggle"_s, {});
}

void HomeClient::leave()
{
    call(u"Leave"_s, {});
}

void HomeClient::focusWindow(const QString &address)
{
    call(u"FocusWindow"_s, {address});
}

void HomeClient::call(const QString &method, const QVariantList &arguments)
{
    if (!available()) {
        qWarning("launcher: %s dropped, no owner of %s", qPrintable(method), qPrintable(service()));
        emit failed(method, u"no owner"_s);
        return;
    }
    QDBusMessage message = QDBusMessage::createMethodCall(service(), path(), interface(), method);
    message.setArguments(arguments);
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, method](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        const QDBusPendingReply<> reply = *w;
        if (!reply.isError())
            return;
        qWarning("launcher: %s failed: %s", qPrintable(method), qPrintable(reply.error().message()));
        emit failed(method, reply.error().message());
    });
}
