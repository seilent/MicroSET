#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

class Store
{
public:
    explicit Store(const QString &dir);

    void load();
    QJsonValue value(const QString &key) const;
    bool contains(const QString &key) const;
    QStringList keys() const;
    bool set(const QString &key, const QJsonValue &value);
    bool set(const QJsonObject &changes, const QStringList &removed = {});
    QString path() const;

private:
    bool write(const QJsonObject &values);
    void moveAside(const QString &why);

    QString m_dir;
    QJsonObject m_values;
};
