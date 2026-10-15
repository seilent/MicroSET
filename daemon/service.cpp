#include "service.h"

#include "errors.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

using namespace Qt::StringLiterals;

Service::Service(const QDBusConnection &bus, Settings &settings, const Auth &auth, PowerBridge &bridge,
                 QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_auth(auth)
    , m_bridge(bridge)
    , m_watcher(QString(), bus, QDBusServiceWatcher::WatchForUnregistration)
{
    connect(&m_watcher, &QDBusServiceWatcher::serviceUnregistered, this, &Service::gone);
    connect(&m_settings, &Settings::changed, this, [this](const QString &key, const QVariant &value) {
        emit Changed(key, QDBusVariant(value));
    });
}

QDBusVariant Service::Get(const QString &key)
{
    const QVariant value = m_settings.value(key);
    if (!value.isValid()) {
        sendErrorReply(Errors::UnknownKey, u"no key "_s + key);
        return {};
    }
    return QDBusVariant(value);
}

void Service::Set(const QString &name, const QDBusVariant &value)
{
    const Key *key = m_settings.schema().key(name);
    if (!key) {
        sendErrorReply(Errors::UnknownKey, u"no key "_s + name);
        return;
    }
    qint64 uid = -1;
    const QString refusal = m_auth.refusal(message().service(), &uid);
    if (!refusal.isEmpty()) {
        qInfo("set %s by uid %lld refused: %s", qPrintable(name), uid, qPrintable(refusal));
        sendErrorReply(Errors::AccessDenied, refusal);
        return;
    }
    if (!key->writable) {
        sendErrorReply(Errors::ReadOnly, name + u" is read only"_s);
        return;
    }
    const QVariant requested = value.variant();
    if (!m_settings.schema().accepts(*key, requested)) {
        sendErrorReply(Errors::InvalidValue, u"not a valid "_s + name);
        return;
    }
    qInfo("set %s=%s by uid %lld", qPrintable(name), qPrintable(::Schema::text(requested).left(64)), uid);
    if (key->applier == "power"_L1) {
        setDelayedReply(true);
        const QDBusMessage request = message();
        QDBusConnection bus = connection();
        m_bridge.set(name, requested, [request, bus](const QString &error, const QString &text) mutable {
            bus.send(error.isEmpty() ? request.createReply() : request.createErrorReply(error, text));
        });
        return;
    }
    if (m_settings.store(name, requested) == Settings::Failed)
        sendErrorReply(Errors::Failed, u"cannot write the state file"_s);
}

QVariantMap Service::List(const QString &prefix)
{
    QVariantMap out;
    for (const QString &key : m_settings.schema().keys(prefix))
        out.insert(key, m_settings.value(key));
    return out;
}

QString Service::Schema(const QString &prefix)
{
    QJsonObject out = m_settings.schema().describe(prefix);
    for (const QString &key : std::as_const(m_live)) {
        if (!out.contains(key))
            continue;
        QJsonObject entry = out.value(key).toObject();
        entry.insert(u"live"_s, true);
        out.insert(key, entry);
    }
    return QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact));
}

void Service::addLive(const QString &key)
{
    m_live.insert(key);
}

bool Service::held(const QString &key) const
{
    return !m_subscribers.value(key).isEmpty();
}

bool Service::live(const QString &key)
{
    if (!m_settings.schema().key(key)) {
        sendErrorReply(Errors::UnknownKey, u"no key "_s + key);
        return false;
    }
    if (!m_live.contains(key)) {
        sendErrorReply(Errors::InvalidArgs, key + u" is not a live key"_s);
        return false;
    }
    return true;
}

void Service::Subscribe(const QString &key)
{
    if (!live(key))
        return;
    const QString name = message().service();
    qint64 uid = -1;
    const QString refusal = m_auth.refusal(name, &uid);
    if (!refusal.isEmpty()) {
        qInfo("subscribe %s by uid %lld refused: %s", qPrintable(key), uid, qPrintable(refusal));
        sendErrorReply(Errors::AccessDenied, refusal);
        return;
    }
    QSet<QString> &names = m_subscribers[key];
    if (names.contains(name))
        return;
    m_watcher.addWatchedService(name);
    if (!connection().interface()->isServiceRegistered(name).value()) {
        gone(name);
        return;
    }
    names.insert(name);
    qInfo("subscribe %s by %s uid %lld", qPrintable(key), qPrintable(name), uid);
    if (names.size() == 1)
        emit subscribersChanged(key, true);
}

void Service::Unsubscribe(const QString &key)
{
    if (live(key))
        drop(key, message().service());
}

void Service::drop(const QString &key, const QString &name)
{
    auto it = m_subscribers.find(key);
    if (it == m_subscribers.end() || !it->remove(name))
        return;
    qInfo("unsubscribe %s by %s", qPrintable(key), qPrintable(name));
    if (std::none_of(m_subscribers.cbegin(), m_subscribers.cend(),
                     [&](const QSet<QString> &names) { return names.contains(name); }))
        m_watcher.removeWatchedService(name);
    if (it->isEmpty())
        emit subscribersChanged(key, false);
}

void Service::gone(const QString &name)
{
    for (const QString &key : m_subscribers.keys())
        drop(key, name);
    m_watcher.removeWatchedService(name);
}
