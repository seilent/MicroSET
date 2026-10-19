#include "settings.h"

#include <algorithm>

Settings::Settings(const Schema &schema, Store &store, QObject *parent)
    : QObject(parent)
    , m_schema(schema)
    , m_store(store)
{
    m_flush.setSingleShot(true);
    connect(&m_flush, &QTimer::timeout, this, &Settings::flush);
}

const Schema &Settings::schema() const
{
    return m_schema;
}

QVariant Settings::value(const QString &name) const
{
    if (!m_schema.key(name))
        return {};
    if (m_live.contains(name))
        return m_live.value(name);
    const auto it = m_overrides.constFind(name);
    if (it != m_overrides.cend() && !it->isEmpty())
        return it->last().value;
    return base(name);
}

QVariant Settings::stored(const QString &name) const
{
    const Key *key = m_schema.key(name);
    if (key && m_pending.contains(name))
        return m_pending.value(name);
    if (!key || !m_store.contains(name))
        return {};
    const QVariant stored = Schema::fromJson(key->type, m_store.value(name));
    return stored.isValid() && m_schema.accepts(*key, stored) ? stored : QVariant();
}

QVariant Settings::base(const QString &name) const
{
    const Key *key = m_schema.key(name);
    if (!key)
        return {};
    const QVariant value = stored(name);
    return value.isValid() ? value : key->defaultValue;
}

bool Settings::isStored(const QString &key) const
{
    return m_pending.contains(key) || m_store.contains(key);
}

Settings::Result Settings::store(const QString &key, const QVariant &value)
{
    if (base(key) == value)
        return Unchanged;
    const Key *entry = m_schema.key(key);
    if (!entry || !entry->coalesce)
        return store(QVariantMap{{key, value}});
    const QVariant before = this->value(key);
    m_pending.insert(key, value);
    m_flush.start(m_delay);
    emitIfMoved(key, before);
    return Changed;
}

bool Settings::flush()
{
    m_flush.stop();
    if (m_pending.isEmpty())
        return true;
    QJsonObject changes;
    for (auto it = m_pending.cbegin(); it != m_pending.cend(); ++it)
        changes.insert(it.key(), Schema::toJson(it.value()));
    if (!m_store.set(changes)) {
        if (!m_flushWarned)
            qWarning("cannot write %lld coalesced keys, kept in memory and retried", qlonglong(changes.size()));
        m_flushWarned = true;
        m_flush.start(m_delay * 10);
        return false;
    }
    m_flushWarned = false;
    m_pending.clear();
    return true;
}

void Settings::setCoalesceDelay(int ms)
{
    m_delay = ms;
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
    for (auto it = values.cbegin(); it != values.cend(); ++it)
        m_pending.remove(it.key());
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

void Settings::emitIfMoved(const QString &key, const QVariant &before)
{
    const QVariant after = value(key);
    if (after != before)
        emit changed(key, after);
}

bool Settings::removeOverride(const QString &key, const QString &owner)
{
    auto it = m_overrides.find(key);
    if (it == m_overrides.end())
        return false;
    const bool removed = it->removeIf([&](const Override &entry) { return entry.owner == owner; }) > 0;
    if (it->isEmpty())
        m_overrides.erase(it);
    return removed;
}

void Settings::setOverride(const QString &key, const QString &owner, const QVariant &value)
{
    const QVariant before = this->value(key);
    removeOverride(key, owner);
    m_overrides[key].append({owner, value});
    emitIfMoved(key, before);
}

bool Settings::clearOverride(const QString &key, const QString &owner)
{
    const QVariant before = value(key);
    if (!removeOverride(key, owner))
        return false;
    emitIfMoved(key, before);
    return true;
}

QStringList Settings::dropOverrides(const QString &owner)
{
    QStringList keys;
    for (auto it = m_overrides.cbegin(); it != m_overrides.cend(); ++it)
        if (std::any_of(it->cbegin(), it->cend(), [&](const Override &entry) { return entry.owner == owner; }))
            keys << it.key();
    for (const QString &key : std::as_const(keys))
        clearOverride(key, owner);
    return keys;
}

bool Settings::holdsOverride(const QString &owner) const
{
    const auto mine = [&](const Override &entry) { return entry.owner == owner; };
    return std::any_of(m_overrides.cbegin(), m_overrides.cend(), [&](const QList<Override> &entries) {
        return std::any_of(entries.cbegin(), entries.cend(), mine);
    });
}

bool Settings::overridden(const QString &key) const
{
    const auto it = m_overrides.constFind(key);
    return it != m_overrides.cend() && !it->isEmpty();
}
