#pragma once

#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QObject>
#include <QString>

class Wifi : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(QString ssid READ ssid NOTIFY ssidChanged)

public:
    static QString service();
    static QString path();
    static QString managerInterface();
    static QString connectionInterface();
    static QString deviceInterface();
    static QString accessPointInterface();
    static QString wirelessType();
    static QString propertiesInterface();

    explicit Wifi(const QDBusConnection &bus, QObject *parent = nullptr);

    bool connected() const;
    QString ssid() const;

signals:
    void connectedChanged();
    void ssidChanged();

private slots:
    void onManagerProperties(const QString &interface, const QVariantMap &changed, const QStringList &invalidated);
    void onPrimaryProperties(const QString &interface, const QVariantMap &changed, const QStringList &invalidated);
    void onDeviceProperties(const QString &interface, const QVariantMap &changed, const QStringList &invalidated);
    void onAccessPointProperties(const QString &interface, const QVariantMap &changed,
                                 const QStringList &invalidated);

private:
    void subscribeManager();
    void readState();
    void readPrimary(QDBusObjectPath connection, bool reload = false);
    void dropPrimary();
    void readDevice(QDBusObjectPath device);
    void dropDevice();
    void readSsid(QDBusObjectPath accessPoint);
    void dropAccessPoint();
    void setConnected(bool connected, const QString &ssid);

    QDBusConnection m_bus;
    QDBusObjectPath m_primary;
    QDBusObjectPath m_device;
    QDBusObjectPath m_accessPoint;
    bool m_primarySubscribed = false;
    bool m_deviceSubscribed = false;
    bool m_apSubscribed = false;
    int m_generation = 0;
    bool m_connected = false;
    QString m_ssid;
};
