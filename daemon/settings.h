#pragma once

#include "schema.h"
#include "store.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QTimer>

class Settings : public QObject
{
    Q_OBJECT

public:
    enum Result { Unchanged, Changed, Failed };

    Settings(const Schema &schema, Store &store, QObject *parent = nullptr);

    const Schema &schema() const;
    QVariant value(const QString &key) const;
    QVariant stored(const QString &key) const;
    QVariant storedUnchecked(const QString &key) const;
    bool isStored(const QString &key) const;
    bool isSet(const QString &key) const;
    QStringList perAppKeys() const;
    QSet<QString> apps() const;
    bool hasApp(const QString &appid) const;
    Result store(const QString &key, const QVariant &value);
    Result store(const QVariantMap &values);
    void setLive(const QString &key, const QVariant &value);
    void setOverride(const QString &key, const QString &owner, const QVariant &value);
    bool clearOverride(const QString &key, const QString &owner);
    QStringList dropOverrides(const QString &owner);
    bool holdsOverride(const QString &owner) const;
    bool overridden(const QString &key) const;
    bool flush();
    void setCoalesceDelay(int ms);
    void moveBtCodec();

signals:
    void changed(const QString &key, const QVariant &value);

private:
    struct Override
    {
        QString owner;
        QVariant value;
    };

    QVariant base(const QString &key) const;
    void emitIfMoved(const QString &key, const QVariant &before);
    bool removeOverride(const QString &key, const QString &owner);

    const Schema &m_schema;
    Store &m_store;
    QVariantMap m_live;
    QVariantMap m_pending;
    QTimer m_flush;
    int m_delay = 1000;
    bool m_flushWarned = false;
    QHash<QString, QList<Override>> m_overrides;
};
