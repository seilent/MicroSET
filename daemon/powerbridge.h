#pragma once

#include "settings.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QObject>

#include <functional>

class PowerBridge : public QObject
{
    Q_OBJECT

public:
    using Done = std::function<void(const QString &error, const QString &message)>;

    PowerBridge(const QDBusConnection &bus, Settings &settings, QObject *parent = nullptr);

    static void importState(const QString &path, Settings &settings);
    void start();
    void set(const QString &key, const QVariant &value, Done done);

private slots:
    void ownerChanged(const QString &name, const QString &oldOwner, const QString &newOwner);
    void propertiesChanged(const QString &interface, const QVariantMap &changed, const QStringList &invalidated,
                           const QDBusMessage &message);

private:
    struct Property
    {
        QString key;
        QString interface;
        QString name;
    };

    static const QList<Property> &properties();
    void call(const QString &method, const QVariantList &arguments, std::function<void(const QDBusMessage &)> done);
    void push();
    void pushNext(quint64 generation, QList<Property> queue, QStringList pushed);
    bool record(const QString &key, const QVariant &value);

    QDBusConnection m_bus;
    Settings &m_settings;
    QString m_owner;
    quint64 m_generation = 0;
    bool m_recording = false;
};
