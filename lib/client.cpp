#include "microset/client.h"

#include <QDBusConnectionInterface>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusServiceWatcher>

using namespace Qt::StringLiterals;

namespace MicroSet {

namespace {

const QString Service = u"net.seilent.MicroSet"_s;
const QString Path = u"/net/seilent/MicroSet"_s;
const QString Interface = u"net.seilent.MicroSet1"_s;

Error errorOf(const QDBusMessage &reply)
{
    if (reply.type() == QDBusMessage::ReplyMessage)
        return {};
    return {reply.errorName().isEmpty() ? u"org.freedesktop.DBus.Error.Failed"_s : reply.errorName(),
            reply.errorMessage()};
}

bool refused(const Error &error)
{
    return error.name.startsWith("net.seilent.MicroSet1.Error."_L1)
        || error.name == "org.freedesktop.DBus.Error.AccessDenied"_L1
        || error.name == "org.freedesktop.DBus.Error.InvalidArgs"_L1;
}

}

Client::Client(QObject *parent)
    : Client(QDBusConnection::systemBus(), parent)
{
}

Client::Client(const QDBusConnection &bus, QObject *parent)
    : QObject(parent)
    , m_bus(bus)
{
    qDBusRegisterMetaType<QMap<QString, QString>>();
    m_bus.connect(Service, Path, Interface, u"Changed"_s, this, SLOT(onChanged(QString, QDBusVariant)));
    m_bus.connect(Service, Path, Interface, u"HoldRefused"_s, this, SIGNAL(holdRefused(QString, QString)));
    auto *watcher = new QDBusServiceWatcher(Service, m_bus, QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, this, &Client::onOwnerChanged);
}

bool Client::available() const
{
    return m_bus.isConnected() && m_bus.interface()->isServiceRegistered(Service).value();
}

QDBusMessage Client::method(const QString &name, const QVariantList &arguments) const
{
    QDBusMessage message = QDBusMessage::createMethodCall(Service, Path, Interface, name);
    message.setArguments(arguments);
    return message;
}

QDBusMessage Client::call(const QString &name, const QVariantList &arguments, Error *error) const
{
    const QDBusMessage reply = m_bus.call(method(name, arguments));
    if (error)
        *error = errorOf(reply);
    return reply;
}

QVariant Client::get(const QString &key, Error *error) const
{
    const QDBusMessage reply = call(u"Get"_s, {key}, error);
    return reply.arguments().value(0).value<QDBusVariant>().variant();
}

void Client::set(const QString &key, const QVariant &value, std::function<void(const Error &)> done)
{
    auto *watcher = new QDBusPendingCallWatcher(
        m_bus.asyncCall(method(u"Set"_s, {key, QVariant::fromValue(QDBusVariant(value))})), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [watcher, done = std::move(done)] {
        watcher->deleteLater();
        if (done)
            done(errorOf(watcher->reply()));
    });
}

QVariantMap Client::list(const QString &prefix, Error *error) const
{
    const QDBusMessage reply = call(u"List"_s, {prefix}, error);
    return qdbus_cast<QVariantMap>(reply.arguments().value(0));
}

QVariantMap Client::stored(const QString &prefix, Error *error) const
{
    const QDBusMessage reply = call(u"Stored"_s, {prefix}, error);
    return qdbus_cast<QVariantMap>(reply.arguments().value(0));
}

void Client::import(const QVariantMap &values, Imported done)
{
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(method(u"Import"_s, {values})), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [watcher, done = std::move(done)] {
        watcher->deleteLater();
        if (!done)
            return;
        const QDBusMessage reply = watcher->reply();
        done(reply.arguments().value(0).toStringList(), qdbus_cast<QMap<QString, QString>>(reply.arguments().value(1)),
             errorOf(reply));
    });
}

void Client::override(const QString &key, const QVariant &value, std::function<void(const Error &)> done)
{
    m_overrides.insert(key, value);
    auto *watcher = new QDBusPendingCallWatcher(
        m_bus.asyncCall(method(u"Override"_s, {key, QVariant::fromValue(QDBusVariant(value))})), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, key, value, done = std::move(done)] {
        watcher->deleteLater();
        const Error error = errorOf(watcher->reply());
        if (refused(error) && m_overrides.value(key) == value)
            m_overrides.remove(key);
        if (done)
            done(error);
    });
}

void Client::clearOverride(const QString &key, std::function<void(const Error &)> done)
{
    m_overrides.remove(key);
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(method(u"ClearOverride"_s, {key})), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [watcher, done = std::move(done)] {
        watcher->deleteLater();
        if (done)
            done(errorOf(watcher->reply()));
    });
}

QString Client::schema(const QString &prefix, Error *error) const
{
    return call(u"Schema"_s, {prefix}, error).arguments().value(0).toString();
}

void Client::watch(const QString &key)
{
    m_watched.insert(key);
}

void Client::subscribe(const QString &key)
{
    if (m_subscribed.contains(key))
        return;
    m_subscribed.insert(key);
    m_bus.asyncCall(method(u"Subscribe"_s, {key}));
}

void Client::unsubscribe(const QString &key)
{
    if (m_subscribed.remove(key))
        m_bus.asyncCall(method(u"Unsubscribe"_s, {key}));
}

void Client::onChanged(const QString &key, const QDBusVariant &value)
{
    emit changed(key, value.variant());
}

void Client::refresh(const QString &key)
{
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(method(u"Get"_s, {key})), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, key] {
        watcher->deleteLater();
        const QDBusMessage reply = watcher->reply();
        if (reply.type() == QDBusMessage::ReplyMessage)
            emit changed(key, reply.arguments().value(0).value<QDBusVariant>().variant());
    });
}

void Client::onOwnerChanged(const QString &, const QString &, const QString &newOwner)
{
    if (newOwner.isEmpty())
        return;
    for (const QString &key : std::as_const(m_subscribed))
        m_bus.asyncCall(method(u"Subscribe"_s, {key}));
    for (auto it = m_overrides.cbegin(); it != m_overrides.cend(); ++it)
        m_bus.asyncCall(method(u"Override"_s, {it.key(), QVariant::fromValue(QDBusVariant(it.value()))}));
    for (const QString &key : m_watched | m_subscribed)
        refresh(key);
}

}
