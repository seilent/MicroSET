#include "service.h"

#include "errors.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QJsonDocument>

using namespace Qt::StringLiterals;

Service::Service(Settings &settings, const Auth &auth, PowerBridge &bridge, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_auth(auth)
    , m_bridge(bridge)
{
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
    return QString::fromUtf8(QJsonDocument(m_settings.schema().describe(prefix)).toJson(QJsonDocument::Compact));
}
