#pragma once

#include <QDBusMessage>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QMutex>
#include <QPair>
#include <QStringList>
#include <QThread>
#include <QVariant>

#include <memory>

class FakeMicroSetd
{
public:
    using Sets = QList<QPair<QString, QVariant>>;

    static QJsonObject describe(const QStringList &files);
    static QString fixture(const QString &name);
    static QString shipped();

    explicit FakeMicroSetd(const QString &address);
    ~FakeMicroSetd();

    bool start();
    void stop();
    void release(int count = -1);
    void emitChanged(const QString &key, const QVariant &value);
    void emitHoldRefused(const QString &key, const QString &reason);

    void setSchema(const QJsonObject &schema);
    void setValues(const QVariantMap &values);
    void setStored(const QVariantMap &stored);
    void setHold(bool hold);
    void refuse(const QString &key, const QString &error);
    QVariantMap stored() const;
    Sets sets() const;
    QStringList subscriptions() const;

private:
    class Handler;

    bool handle(const QDBusMessage &message);
    void apply(const QDBusMessage &message);

    QString m_address;
    QString m_connection;
    QThread m_thread;
    std::unique_ptr<Handler> m_handler;
    mutable QMutex m_lock;
    QJsonObject m_schema;
    QVariantMap m_values;
    QVariantMap m_stored;
    QMap<QString, QString> m_refuse;
    bool m_hold = false;
    Sets m_sets;
    QStringList m_subscriptions;
    QList<QDBusMessage> m_held;
    int m_starts = 0;
};
