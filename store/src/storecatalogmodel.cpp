#include "storecatalogmodel.h"

#include <QVariantMap>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

QString letter(const QString &name)
{
    const QString trimmed = name.trimmed();
    return trimmed.isEmpty() ? QString() : trimmed.left(1).toUpper();
}

}

StoreCatalogModel::StoreCatalogModel(QObject *parent)
    : QObject(parent)
{
    m_collator.setCaseSensitivity(Qt::CaseInsensitive);
    m_collator.setNumericMode(true);
}

QVariantList StoreCatalogModel::rows() const
{
    return m_rows;
}

int StoreCatalogModel::count() const
{
    return int(m_rows.size());
}

QString StoreCatalogModel::search() const
{
    return m_search;
}

bool StoreCatalogModel::synced() const
{
    return m_synced;
}

bool StoreCatalogModel::offline() const
{
    return !m_synced && m_entries.isEmpty();
}

bool StoreCatalogModel::contains(const QString &id) const
{
    for (const StoreEntry &entry : m_entries) {
        if (entry.id == id)
            return true;
    }
    return false;
}

bool StoreCatalogModel::find(const QString &id, StoreEntry *entry) const
{
    for (const StoreEntry &candidate : m_entries) {
        if (candidate.id != id)
            continue;
        if (entry != nullptr)
            *entry = candidate;
        return true;
    }
    return false;
}

void StoreCatalogModel::setEntries(const QList<StoreEntry> &entries)
{
    m_entries = entries;
    rebuild();
    updateOffline();
}

void StoreCatalogModel::setInstalled(const QStringList &ids)
{
    m_installed = QSet<QString>(ids.begin(), ids.end());
    rebuild();
}

void StoreCatalogModel::setSynced(bool synced)
{
    if (m_synced == synced)
        return;
    m_synced = synced;
    emit syncedChanged();
    updateOffline();
}

void StoreCatalogModel::setSearch(const QString &search)
{
    if (m_search == search)
        return;
    m_search = search;
    emit searchChanged();
    rebuild();
}

void StoreCatalogModel::release()
{
    m_entries = {};
    m_rows = {};
    m_installed.clear();
    emit rowsChanged();
    updateOffline();
}

void StoreCatalogModel::updateOffline()
{
    const bool offline = this->offline();
    if (m_offline == offline)
        return;
    m_offline = offline;
    emit offlineChanged();
}

void StoreCatalogModel::rebuild()
{
    const QString search = m_search.trimmed();
    QList<const StoreEntry *> matched;
    for (const StoreEntry &entry : m_entries) {
        if (!search.isEmpty() && !entry.name.contains(search, Qt::CaseInsensitive)
            && !entry.summary.contains(search, Qt::CaseInsensitive)
            && !entry.developer.contains(search, Qt::CaseInsensitive)
            && !entry.id.contains(search, Qt::CaseInsensitive))
            continue;
        matched << &entry;
    }

    std::stable_sort(matched.begin(), matched.end(), [this](const StoreEntry *left, const StoreEntry *right) {
        const int order = m_collator.compare(left->name, right->name);
        return order != 0 ? order < 0 : left->id < right->id;
    });

    m_rows.clear();
    m_rows.reserve(matched.size());
    for (const StoreEntry *entry : matched) {
        m_rows << QVariantMap{
            {u"id"_s, entry->id},
            {u"ref"_s, entry->ref},
            {u"name"_s, entry->name},
            {u"summary"_s, entry->summary},
            {u"developer"_s, entry->developer},
            {u"icon"_s, entry->icon},
            {u"iconLarge"_s, entry->iconLarge},
            {u"letter"_s, letter(entry->name)},
            {u"installed"_s, m_installed.contains(entry->id)},
            {u"installedSize"_s, entry->installedSize},
        };
    }
    emit rowsChanged();
}
