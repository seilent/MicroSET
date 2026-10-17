#pragma once

#include <QDBusConnection>
#include <QDBusMessage>
#include <QString>

#include <functional>

class Auth
{
public:
    Auth(const QDBusConnection &bus, const QString &procRoot);

    QString refusal(const QString &sender, qint64 *uid) const;
    void check(const QString &sender, std::function<void(const QString &refusal, qint64 uid)> done) const;

private:
    static QString judge(const QDBusMessage &reply, const QString &procRoot, qint64 *uid);

    QDBusConnection m_bus;
    QString m_proc;
};
