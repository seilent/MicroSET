#pragma once

#include <QDBusConnection>
#include <QDBusVirtualObject>
#include <QMap>
#include <QStringList>
#include <QVariant>

#include <functional>

class FakePowerd : public QDBusVirtualObject
{
public:
    static inline const QString Power1 = QStringLiteral("net.seilent.MicroOs.Power1");
    static inline const QString Tdp1 = QStringLiteral("com.steampowered.SteamOSManager1.TdpLimit1");

    explicit FakePowerd(const QString &address);
    ~FakePowerd() override;

    bool start();
    void stop();
    QString unique() const;
    void emitChanged(const QString &interface, const QVariantMap &changed);
    void emitUndervoltDone(uint serial, const QString &result, const QString &name, const QString &levels);

    QString introspect(const QString &path) const override;
    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override;

    QMap<QString, QVariantMap> values;
    QStringList sets;
    QStringList undervolts;
    QStringList requests;
    QString requestError;
    int failSets = 0;
    QVariantMap beforeReply;
    QVariantMap afterFailure;
    std::function<QVariant(const QString &, const QVariant &)> adjust;

private:
    QString m_address;
    QString m_connection;
    int m_starts = 0;
};
