#include "service.h"

#include "errors.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>

#include <algorithm>

using namespace Qt::StringLiterals;

Q_LOGGING_CATEGORY(lcCoalesced, "microset.coalesced", QtInfoMsg)

Service::Service(const QDBusConnection &bus, Settings &settings, const Auth &auth, PowerBridge &bridge,
                 QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_auth(auth)
    , m_bridge(bridge)
    , m_watcher(QString(), bus, QDBusServiceWatcher::WatchForUnregistration)
{
    qDBusRegisterMetaType<QMap<QString, QString>>();
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
    const QByteArray shown = ::Schema::text(requested).left(64).toUtf8();
    if (key->coalesce)
        qCDebug(lcCoalesced, "set %s=%s by uid %lld", qPrintable(name), shown.constData(), uid);
    else
        qInfo("set %s=%s by uid %lld", qPrintable(name), shown.constData(), uid);
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

QVariantMap Service::Stored(const QString &prefix)
{
    QVariantMap out;
    for (const QString &key : m_settings.schema().keys(prefix)) {
        const QVariant value = m_settings.stored(key);
        if (value.isValid())
            out.insert(key, value);
    }
    return out;
}

QStringList Service::Import(const QVariantMap &values, QMap<QString, QString> &refused)
{
    qint64 uid = -1;
    const QString refusal = m_auth.refusal(message().service(), &uid);
    if (!refusal.isEmpty()) {
        qInfo("import by uid %lld refused: %s", uid, qPrintable(refusal));
        sendErrorReply(Errors::AccessDenied, refusal);
        return {};
    }
    const ::Schema &schema = m_settings.schema();
    if (values.size() > schema.keys({}).size()) {
        sendErrorReply(Errors::InvalidArgs, u"too many keys"_s);
        return {};
    }
    QVariantMap batch;
    int unknown = 0;
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        const Key *key = schema.key(it.key());
        QString why;
        if (!key)
            why = u"no key"_s;
        else if (m_settings.stored(it.key()).isValid())
            continue;
        else if (!key->writable)
            why = u"read only"_s;
        else if (key->applier == "power"_L1)
            why = u"set through powerd"_s;
        else if (!schema.accepts(*key, it.value()))
            why = u"not a valid value"_s;
        if (why.isEmpty())
            batch.insert(it.key(), it.value());
        else
            refused.insert(it.key(), why);
    }
    for (auto it = refused.cbegin(); it != refused.cend(); ++it) {
        if (schema.key(it.key()))
            qInfo("import %s by uid %lld refused: %s", qPrintable(it.key()), uid, qPrintable(it.value()));
        else
            ++unknown;
    }
    if (unknown > 0)
        qInfo("import by uid %lld refused %d unknown keys", uid, unknown);
    if (batch.isEmpty())
        return {};
    if (m_settings.store(batch) == Settings::Failed) {
        refused.clear();
        sendErrorReply(Errors::Failed, u"cannot write the state file"_s);
        return {};
    }
    qInfo("import by uid %lld: %s", uid, qPrintable(batch.keys().join(u' ')));
    return batch.keys();
}

const Key *Service::overridable(const QString &name, const char *verb, qint64 *uid)
{
    const Key *key = m_settings.schema().key(name);
    if (!key) {
        sendErrorReply(Errors::UnknownKey, u"no key "_s + name);
        return nullptr;
    }
    const QString refusal = m_auth.refusal(message().service(), uid);
    if (!refusal.isEmpty()) {
        qInfo("%s %s by uid %lld refused: %s", verb, qPrintable(name), *uid, qPrintable(refusal));
        sendErrorReply(Errors::AccessDenied, refusal);
        return nullptr;
    }
    if (!key->overridable) {
        sendErrorReply(Errors::InvalidArgs, name + u" takes no override"_s);
        return nullptr;
    }
    return key;
}

void Service::Override(const QString &name, const QDBusVariant &value)
{
    qint64 uid = -1;
    const Key *key = overridable(name, "override", &uid);
    if (!key)
        return;
    const QVariant requested = value.variant();
    if (!m_settings.schema().accepts(*key, requested)) {
        sendErrorReply(Errors::InvalidValue, u"not a valid "_s + name);
        return;
    }
    const QString owner = message().service();
    if (!watch(owner))
        return;
    qInfo("override %s by %s uid %lld", qPrintable(name), qPrintable(owner), uid);
    m_settings.setOverride(name, owner, requested);
}

void Service::ClearOverride(const QString &name)
{
    qint64 uid = -1;
    if (!overridable(name, "clear override", &uid))
        return;
    const QString owner = message().service();
    if (!m_settings.clearOverride(name, owner))
        return;
    qInfo("clear override %s by %s", qPrintable(name), qPrintable(owner));
    unwatch(owner);
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
    if (m_subscribers.value(key).contains(name) || !watch(name))
        return;
    QSet<QString> &names = m_subscribers[key];
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
    unwatch(name);
    if (it->isEmpty())
        emit subscribersChanged(key, false);
}

bool Service::watch(const QString &name)
{
    m_watcher.addWatchedService(name);
    if (connection().interface()->isServiceRegistered(name).value())
        return true;
    gone(name);
    return false;
}

void Service::unwatch(const QString &name)
{
    if (m_settings.holdsOverride(name)
        || std::any_of(m_subscribers.cbegin(), m_subscribers.cend(),
                       [&](const QSet<QString> &names) { return names.contains(name); }))
        return;
    m_watcher.removeWatchedService(name);
}

void Service::gone(const QString &name)
{
    for (const QString &key : m_subscribers.keys())
        drop(key, name);
    for (const QString &key : m_settings.dropOverrides(name))
        qInfo("override %s dropped: %s left the bus", qPrintable(key), qPrintable(name));
    m_watcher.removeWatchedService(name);
}
