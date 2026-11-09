#pragma once

#include "storeentry.h"

#include <QCollator>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantList>

class StoreCatalogModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList rows READ rows NOTIFY rowsChanged)
    Q_PROPERTY(int count READ count NOTIFY rowsChanged)
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY searchChanged)
    Q_PROPERTY(bool synced READ synced NOTIFY syncedChanged)
    Q_PROPERTY(bool offline READ offline NOTIFY offlineChanged)

public:
    explicit StoreCatalogModel(QObject *parent = nullptr);

    QVariantList rows() const;
    int count() const;
    QString search() const;
    bool synced() const;
    bool offline() const;

    bool contains(const QString &id) const;
    bool find(const QString &id, StoreEntry *entry) const;

    void setEntries(const QList<StoreEntry> &entries);
    void setInstalled(const QStringList &ids);
    void setSynced(bool synced);
    void setSearch(const QString &search);
    void release();

signals:
    void rowsChanged();
    void searchChanged();
    void syncedChanged();
    void offlineChanged();

private:
    void rebuild();
    void updateOffline();

    QList<StoreEntry> m_entries;
    QVariantList m_rows;
    QSet<QString> m_installed;
    QString m_search;
    QCollator m_collator;
    bool m_synced = false;
    bool m_offline = true;
};
