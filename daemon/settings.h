#pragma once

#include "schema.h"
#include "store.h"

#include <QObject>

class Settings : public QObject
{
    Q_OBJECT

public:
    enum Result { Unchanged, Changed, Failed };

    Settings(const Schema &schema, Store &store, QObject *parent = nullptr);

    const Schema &schema() const;
    QVariant value(const QString &key) const;
    bool isStored(const QString &key) const;
    Result store(const QString &key, const QVariant &value);
    Result store(const QVariantMap &values);

signals:
    void changed(const QString &key, const QVariant &value);

private:
    const Schema &m_schema;
    Store &m_store;
};
