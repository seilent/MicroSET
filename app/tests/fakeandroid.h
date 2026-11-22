#pragma once

#include "androidstatus.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusVirtualObject>
#include <QList>
#include <QStringList>
#include <QVariantMap>

class FakeAndroid : public QDBusVirtualObject
{
public:
    static inline const QString Properties = QStringLiteral("org.freedesktop.DBus.Properties");

    QString introspect(const QString &) const override { return {}; }

    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override
    {
        if (message.interface() != Properties || message.member() != QStringLiteral("GetAll"))
            return false;
        ++calls;
        const QDBusMessage reply = fail || message.arguments().value(0).toString() != AndroidStatus::Interface
            ? message.createErrorReply(QStringLiteral("org.freedesktop.DBus.Error.Failed"), QStringLiteral("refused"))
            : message.createReply(status);
        if (hold)
            held << reply;
        else
            connection.send(reply);
        return true;
    }

    bool publish(QDBusConnection connection)
    {
        m_bus = connection;
        return connection.registerVirtualObject(AndroidStatus::Path, this)
            && connection.interface()->registerService(AndroidStatus::Service)
            == QDBusConnectionInterface::ServiceRegistered;
    }

    void release()
    {
        for (const QDBusMessage &reply : std::as_const(held))
            m_bus.send(reply);
        held.clear();
    }

    void change(const QVariantMap &values, const QString &interface = AndroidStatus::Interface,
                const QStringList &invalidated = {})
    {
        status.insert(values);
        for (const QString &name : invalidated)
            status.remove(name);
        QDBusMessage signal = QDBusMessage::createSignal(AndroidStatus::Path, Properties,
                                                         QStringLiteral("PropertiesChanged"));
        signal << interface << values << invalidated;
        m_bus.send(signal);
    }

    QVariantMap status;
    QList<QDBusMessage> held;
    int calls = 0;
    bool fail = false;
    bool hold = false;

private:
    QDBusConnection m_bus{QString()};
};
