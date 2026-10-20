#pragma once

#include "settings.h"
#include "undervoltsprovider.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QObject>

#include <functional>

class PowerBridge : public QObject
{
    Q_OBJECT

public:
    using Done = std::function<void(const QString &error, const QString &message)>;

    PowerBridge(const QDBusConnection &bus, Settings &settings, const UndervoltsProvider &undervolts,
                QObject *parent = nullptr);

    static void importState(const QString &path, Settings &settings);
    void start();
    void set(const QString &key, const QVariant &value, Done done);

signals:
    void holdRefused(const QString &key, const QString &reason);

private slots:
    void ownerChanged(const QString &name, const QString &oldOwner, const QString &newOwner);
    void propertiesChanged(const QString &interface, const QVariantMap &changed, const QStringList &invalidated,
                           const QDBusMessage &message);
    void undervoltDone(uint serial, const QString &result, const QString &name, const QString &levels,
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
    void call(const QString &interface, const QString &method, const QVariantList &arguments,
              std::function<void(const QDBusMessage &)> done);
    void push();
    void pushNext(quint64 generation, QList<Property> queue, QStringList pushed);
    void pushUndervolt(quint64 generation, QStringList pushed);
    bool record(const QString &key, const QVariant &value);
    QString undervoltName() const;
    QString undervoltLevels() const;
    QString undervoltValue() const;
    bool deeper(const QString &levels) const;
    void setUndervolt(const QString &name, Done done);
    void sendUndervolt();
    bool recordUndervolt(const QVariantMap &values);

    QDBusConnection m_bus;
    Settings &m_settings;
    const UndervoltsProvider &m_undervolts;
    QString m_owner;
    quint64 m_generation = 0;
    bool m_recording = false;
    uint m_pending = 0;
};
