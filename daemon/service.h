#pragma once

#include "auth.h"
#include "powerbridge.h"
#include "settings.h"

#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QHash>
#include <QMap>
#include <QObject>
#include <QSet>

class Service : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "net.seilent.MicroSet1")

public:
    Service(const QDBusConnection &bus, Settings &settings, const Auth &auth, PowerBridge &bridge,
            QObject *parent = nullptr);

    void addLive(const QString &key);
    bool held(const QString &key) const;

public slots:
    Q_SCRIPTABLE QDBusVariant Get(const QString &key);
    Q_SCRIPTABLE void Set(const QString &key, const QDBusVariant &value);
    Q_SCRIPTABLE QVariantMap List(const QString &prefix);
    Q_SCRIPTABLE QVariantMap Stored(const QString &prefix);
    Q_SCRIPTABLE QStringList Import(const QVariantMap &values, QMap<QString, QString> &refused);
    Q_SCRIPTABLE void Override(const QString &key, const QDBusVariant &value);
    Q_SCRIPTABLE void ClearOverride(const QString &key);
    Q_SCRIPTABLE QString Schema(const QString &prefix);
    Q_SCRIPTABLE void Subscribe(const QString &key);
    Q_SCRIPTABLE void Unsubscribe(const QString &key);

signals:
    Q_SCRIPTABLE void Changed(const QString &key, const QDBusVariant &value);
    Q_SCRIPTABLE void HoldRefused(const QString &key, const QString &reason);
    void subscribersChanged(const QString &key, bool held);

private:
    bool live(const QString &key);
    const Key *overridable(const QString &name, const char *verb, qint64 *uid);
    bool watch(const QString &name);
    void unwatch(const QString &name);
    void drop(const QString &key, const QString &name);
    void gone(const QString &name);

    Settings &m_settings;
    const Auth &m_auth;
    PowerBridge &m_bridge;
    QDBusServiceWatcher m_watcher;
    QSet<QString> m_live;
    QHash<QString, QSet<QString>> m_subscribers;
};
