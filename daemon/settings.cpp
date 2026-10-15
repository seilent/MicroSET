#include "settings.h"

Settings::Settings(const Schema &schema, Store &store, QObject *parent)
    : QObject(parent)
    , m_schema(schema)
    , m_store(store)
{
}

const Schema &Settings::schema() const
{
    return m_schema;
}

QVariant Settings::value(const QString &name) const
{
    const Key *key = m_schema.key(name);
    if (!key)
        return {};
    if (m_live.contains(name))
        return m_live.value(name);
    const QVariant stored = Schema::fromJson(key->type, m_store.value(name));
    return stored.isValid() && m_schema.accepts(*key, stored) ? stored : key->defaultValue;
}

bool Settings::isStored(const QString &key) const
{
    return m_store.contains(key);
}

Settings::Result Settings::store(const QString &key, const QVariant &value)
{
    if (this->value(key) == value)
        return Unchanged;
    return store(QVariantMap{{key, value}});
}

Settings::Result Settings::store(const QVariantMap &values)
{
    QVariantMap before;
    QJsonObject changes;
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        before.insert(it.key(), value(it.key()));
        changes.insert(it.key(), Schema::toJson(it.value()));
    }
    if (!m_store.set(changes))
        return Failed;
    Result result = Unchanged;
    for (auto it = before.cbegin(); it != before.cend(); ++it) {
        const QVariant after = value(it.key());
        if (after != it.value()) {
            emit changed(it.key(), after);
            result = Changed;
        }
    }
    return result;
}

void Settings::setLive(const QString &key, const QVariant &value)
{
    const QVariant before = this->value(key);
    m_live.insert(key, value);
    if (before != value)
        emit changed(key, value);
}
