#pragma once

#include "auth.h"
#include "powerbridge.h"
#include "settings.h"

#include <QDBusContext>
#include <QDBusVariant>
#include <QObject>

class Service : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "net.seilent.MicroSet1")

public:
    Service(Settings &settings, const Auth &auth, PowerBridge &bridge, QObject *parent = nullptr);

public slots:
    Q_SCRIPTABLE QDBusVariant Get(const QString &key);
    Q_SCRIPTABLE void Set(const QString &key, const QDBusVariant &value);
    Q_SCRIPTABLE QVariantMap List(const QString &prefix);
    Q_SCRIPTABLE QString Schema(const QString &prefix);

signals:
    Q_SCRIPTABLE void Changed(const QString &key, const QDBusVariant &value);

private:
    Settings &m_settings;
    const Auth &m_auth;
    PowerBridge &m_bridge;
};
