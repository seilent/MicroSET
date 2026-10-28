#pragma once

#include "schema.h"

#include <QJsonObject>
#include <QMap>
#include <QString>

class ProtonProfiles
{
public:
    struct Entry
    {
        QString tool;
        QMap<QString, QString> set;
    };

    bool load(const QString &path, const Schema &schema);
    const Entry *entry(const QString &appid) const;
    QStringList apps() const;
    QJsonObject toJson() const;

private:
    QMap<QString, Entry> m_entries;
};
