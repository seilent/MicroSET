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

    static inline const QByteArray Profiles =
        R"({"version": 1, "profiles": {"292120": {"tool": "proton-11.0-", "set": {"fex.tso": "off", "wine.sync": "fsync"}}}})";

    static QJsonObject describe(const QStringList &files);
    static QJsonObject effective(const QStringList &files, const QVariantMap &stored, const QByteArray &profiles,
                                 const QString &appid);
    static QString fixture(const QString &name);
    static QString shipped();
    static void writeSteam(const QString &root, const QList<QPair<QString, QString>> &games,
                           const QList<QPair<quint32, QString>> &shortcuts = {});
    static void writeAppInfo(const QString &root, const QList<QPair<quint32, QByteArray>> &types,
                             const QMap<quint32, QByteArray> &extra = {});

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
    void setProton(const QStringList &files, const QByteArray &profiles);
    void refuse(const QString &key, const QString &error);
    QVariantMap stored() const;
    Sets sets() const;
    QStringList calls() const;
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
    QStringList m_files;
    QByteArray m_profiles;
    QVariantMap m_values;
    QVariantMap m_stored;
    QMap<QString, QString> m_refuse;
    bool m_hold = false;
    Sets m_sets;
    QStringList m_calls;
    QVariantMap m_overrides;
    QVariantMap m_base;
    QStringList m_subscriptions;
    QList<QDBusMessage> m_held;
    int m_starts = 0;
};
