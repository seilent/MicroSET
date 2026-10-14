#pragma once

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QObject>
#include <QSet>
#include <QVariant>

#include <functional>

namespace MicroSet {

struct Error
{
    QString name;
    QString message;

    bool ok() const { return name.isEmpty(); }
};

class Client : public QObject
{
    Q_OBJECT

public:
    explicit Client(QObject *parent = nullptr);
    explicit Client(const QDBusConnection &bus, QObject *parent = nullptr);

    bool available() const;
    QVariant get(const QString &key, Error *error = nullptr) const;
    void set(const QString &key, const QVariant &value, std::function<void(const Error &)> done = {});
    QVariantMap list(const QString &prefix = {}, Error *error = nullptr) const;
    QString schema(const QString &prefix = {}, Error *error = nullptr) const;
    void watch(const QString &key);
    void subscribe(const QString &key);
    void unsubscribe(const QString &key);

Q_SIGNALS:
    void changed(const QString &key, const QVariant &value);

private Q_SLOTS:
    void onChanged(const QString &key, const QDBusVariant &value);
    void onOwnerChanged(const QString &name, const QString &oldOwner, const QString &newOwner);

private:
    QDBusMessage method(const QString &name, const QVariantList &arguments) const;
    QDBusMessage call(const QString &name, const QVariantList &arguments, Error *error) const;
    void refresh(const QString &key);

    QDBusConnection m_bus;
    QSet<QString> m_watched;
    QSet<QString> m_subscribed;
};

}
