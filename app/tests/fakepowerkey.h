#pragma once

#include "quickaccess.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusVirtualObject>

class FakePowerKey : public QDBusVirtualObject
{
public:
    QString introspect(const QString &) const override { return {}; }

    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override
    {
        if (message.interface() != QuickAccess::Interface || message.member() != QStringLiteral("OpenQuickAccess"))
            return false;
        ++calls;
        connection.send(fail ? message.createErrorReply(QStringLiteral("net.seilent.MicroOS.PowerKey1.Error.NotConnected"),
                                                        QStringLiteral("vpad is not connected"))
                             : message.createReply());
        return true;
    }

    bool publish(QDBusConnection connection)
    {
        return connection.registerVirtualObject(QuickAccess::Path, this)
            && connection.interface()->registerService(QuickAccess::Service)
            == QDBusConnectionInterface::ServiceRegistered;
    }

    int calls = 0;
    bool fail = false;
};
