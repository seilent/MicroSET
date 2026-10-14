#pragma once

#include <QDBusConnection>
#include <QString>

class Auth
{
public:
    Auth(const QDBusConnection &bus, const QString &procRoot);

    QString refusal(const QString &sender, qint64 *uid) const;

private:
    QDBusConnection m_bus;
    QString m_proc;
};
