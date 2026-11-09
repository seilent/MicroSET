#include "battery.h"
#include "wifi.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusConnectionInterface>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QDBusVirtualObject>
#include <QDir>
#include <QElapsedTimer>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using namespace Qt::StringLiterals;

namespace {

class FakeObject : public QDBusVirtualObject
{
public:
    FakeObject(const QString &path, const QString &interface, QObject *parent = nullptr)
        : QDBusVirtualObject(parent)
        , m_path(path)
        , m_interface(interface)
    {
    }

    ~FakeObject() override { bus().unregisterObject(m_path); }

    QString introspect(const QString &) const override { return {}; }

    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override
    {
        if (message.member() == u"GetAll"_s) {
            connection.send(message.createReply(values()));
            return true;
        }
        if (message.member() == u"Get"_s) {
            const QString name = message.arguments().value(1).toString();
            if (!values().contains(name)) {
                connection.send(message.createErrorReply(u"org.freedesktop.DBus.Error.UnknownProperty"_s, name));
                return true;
            }
            connection.send(message.createReply(QVariant::fromValue(QDBusVariant(values().value(name)))));
            return true;
        }
        return false;
    }

    void announce(const QString &interface, const QVariantMap &changed, const QStringList &invalidated = {})
    {
        QDBusMessage signal =
            QDBusMessage::createSignal(m_path, u"org.freedesktop.DBus.Properties"_s, u"PropertiesChanged"_s);
        signal << interface << changed << invalidated;
        bus().send(signal);
    }

    QDBusConnection bus() const { return QDBusConnection::connectToBus(m_address, u"status-test"_s); }

    QString m_address;

    virtual QVariantMap values() const = 0;
    QString m_path;
    QString m_interface;
};

class FakeUPower : public FakeObject
{
public:
    FakeUPower()
        : FakeObject(u"/org/freedesktop/UPower/devices/DisplayDevice"_s, u"org.freedesktop.UPower.Device"_s)
    {
    }

    QVariantMap values() const override
    {
        if (!present)
            return {{u"IsPresent"_s, false}, {u"State"_s, 0}};
        return {{u"IsPresent"_s, true}, {u"Percentage"_s, percent}, {u"State"_s, 2}};
    }

    bool present = true;
    int percent = 72;
};

class FakeAccessPoint : public FakeObject
{
public:
    FakeAccessPoint()
        : FakeObject(u"/org/freedesktop/NetworkManager/AccessPoint/1"_s,
                     u"org.freedesktop.NetworkManager.AccessPoint"_s)
    {
    }

    QVariantMap values() const override { return {{u"Ssid"_s, ssid}}; }

    QByteArray ssid = QByteArrayLiteral("NSD1K-DB30-a");
};

class FakeConnection : public FakeObject
{
public:
    FakeConnection()
        : FakeObject(u"/org/freedesktop/NetworkManager/ActiveConnection/1"_s,
                     u"org.freedesktop.NetworkManager.Connection.Active"_s)
    {
    }

    QVariantMap values() const override
    {
        QVariantMap out;
        out.insert(u"Type"_s, type);
        out.insert(u"Devices"_s, QVariant::fromValue(devices));
        return out;
    }

    QString type = u"802-11-wireless"_s;
    QList<QDBusObjectPath> devices{QDBusObjectPath(u"/org/freedesktop/NetworkManager/Devices/2"_s)};
};

class FakeDevice : public FakeObject
{
public:
    FakeDevice()
        : FakeObject(u"/org/freedesktop/NetworkManager/Devices/2"_s, u"org.freedesktop.NetworkManager.Device"_s)
    {
    }

    QVariantMap values() const override
    {
        return {{u"ActiveAccessPoint"_s, QVariant::fromValue(QDBusObjectPath(accessPoint))}};
    }

    QString accessPoint = u"/org/freedesktop/NetworkManager/AccessPoint/1"_s;
};

class FakeNM : public FakeObject
{
public:
    FakeNM()
        : FakeObject(u"/org/freedesktop/NetworkManager"_s, u"org.freedesktop.NetworkManager"_s)
    {
    }

    QVariantMap values() const override
    {
        return {{u"PrimaryConnection"_s, QVariant::fromValue(QDBusObjectPath(primary))}, {u"State"_s, 70}};
    }

    QString primary = u"/org/freedesktop/NetworkManager/ActiveConnection/1"_s;
};

}

class StatusTest : public QObject
{
    Q_OBJECT

    QTemporaryDir m_data;
    QProcess m_bus;
    QString m_address;
    std::unique_ptr<FakeUPower> m_upower;
    std::unique_ptr<FakeNM> m_nm;
    std::unique_ptr<FakeConnection> m_connection;
    std::unique_ptr<FakeDevice> m_device;
    std::unique_ptr<FakeAccessPoint> m_accessPoint;

private slots:
    void initTestCase()
    {
        const QString path = QDir(m_data.path()).filePath(u"bus"_s);
        m_bus.setStandardOutputFile(QProcess::nullDevice());
        m_bus.setStandardErrorFile(QProcess::nullDevice());
        m_bus.start(u"dbus-daemon"_s,
                    {u"--session"_s, u"--nofork"_s, u"--nopidfile"_s, u"--address=unix:path="_s + path});
        QVERIFY(m_bus.waitForStarted());
        QElapsedTimer ready;
        ready.start();
        while (!QFile::exists(path) && ready.elapsed() < 10000)
            QTest::qWait(20);
        QVERIFY(QFile::exists(path));
        m_address = u"unix:path="_s + path;
        QVERIFY(QDBusConnection::connectToBus(m_address, u"status-test"_s).isConnected());
    }

    void cleanupTestCase()
    {
        QDBusConnection::disconnectFromBus(u"status-test"_s);
        m_bus.terminate();
        if (!m_bus.waitForFinished(3000)) {
            m_bus.kill();
            m_bus.waitForFinished();
        }
    }

    void init()
    {
        m_connection.reset();
        m_device.reset();
        m_accessPoint.reset();
        m_upower.reset();
        m_nm.reset();
    }

    void cleanup()
    {
        m_accessPoint.reset();
        m_device.reset();
        m_connection.reset();
        m_nm.reset();
        m_upower.reset();
    }

    QDBusConnection bus() const { return QDBusConnection::connectToBus(m_address, u"status-test"_s); }

    bool serve(FakeObject *object, const QString &service = QString())
    {
        object->m_address = m_address;
        if (!bus().registerVirtualObject(object->m_path, object))
            return false;
        if (service.isEmpty())
            return true;
        if (bus().interface()->isServiceRegistered(service).value())
            return true;
        return bus().registerService(service);
    }

    void batteryFollowsDisplayDevice()
    {
        m_upower = std::make_unique<FakeUPower>();
        QVERIFY(serve(m_upower.get(), Battery::service()));
        Battery battery(bus());
        QTRY_VERIFY_WITH_TIMEOUT(battery.present(), 5000);
        QCOMPARE(battery.percent(), 72);
        QVERIFY(!battery.low());
        QSignalSpy spy(&battery, &Battery::percentChanged);
        m_upower->percent = 9;
        m_upower->announce(Battery::deviceInterface(), {{u"Percentage"_s, 9}});
        QTRY_COMPARE_WITH_TIMEOUT(battery.percent(), 9, 5000);
        QVERIFY(battery.low());
        QVERIFY(spy.count() >= 1);
    }

    void noDisplayDeviceHidesTheGlyph()
    {
        Battery battery(bus());
        QTest::qWait(300);
        QVERIFY(!battery.present());
    }

    void presentFalseHidesTheGlyph()
    {
        m_upower = std::make_unique<FakeUPower>();
        m_upower->present = false;
        QVERIFY(serve(m_upower.get(), Battery::service()));
        Battery battery(bus());
        QTest::qWait(300);
        QVERIFY(!battery.present());
    }

    void wifiFollowsThePrimaryConnection()
    {
        m_accessPoint = std::make_unique<FakeAccessPoint>();
        m_device = std::make_unique<FakeDevice>();
        m_connection = std::make_unique<FakeConnection>();
        m_nm = std::make_unique<FakeNM>();
        QVERIFY(serve(m_accessPoint.get(), Wifi::service()));
        QVERIFY(serve(m_device.get()));
        QVERIFY(serve(m_connection.get()));
        QVERIFY(serve(m_nm.get()));
        Wifi wifi(bus());
        QTRY_VERIFY_WITH_TIMEOUT(wifi.connected(), 5000);
        QCOMPARE(wifi.ssid(), u"NSD1K-DB30-a"_s);
    }

    void wifiDownWhenThePrimaryLeaves()
    {
        m_accessPoint = std::make_unique<FakeAccessPoint>();
        m_device = std::make_unique<FakeDevice>();
        m_connection = std::make_unique<FakeConnection>();
        m_nm = std::make_unique<FakeNM>();
        QVERIFY(serve(m_accessPoint.get(), Wifi::service()));
        QVERIFY(serve(m_device.get()));
        QVERIFY(serve(m_connection.get()));
        QVERIFY(serve(m_nm.get()));
        Wifi wifi(bus());
        QTRY_VERIFY_WITH_TIMEOUT(wifi.connected(), 5000);
        m_nm->primary = QString();
        m_nm->announce(Wifi::managerInterface(),
                       {{u"PrimaryConnection"_s, QVariant::fromValue(QDBusObjectPath(u"/"_s))}});
        QTRY_VERIFY_WITH_TIMEOUT(!wifi.connected(), 5000);
    }

    void wifiIgnoresAWiredPrimary()
    {
        m_accessPoint = std::make_unique<FakeAccessPoint>();
        m_device = std::make_unique<FakeDevice>();
        m_connection = std::make_unique<FakeConnection>();
        m_connection->type = u"802-3-ethernet"_s;
        m_nm = std::make_unique<FakeNM>();
        QVERIFY(serve(m_accessPoint.get(), Wifi::service()));
        QVERIFY(serve(m_device.get()));
        QVERIFY(serve(m_connection.get()));
        QVERIFY(serve(m_nm.get()));
        Wifi wifi(bus());
        QTest::qWait(500);
        QVERIFY(!wifi.connected());
    }

    void wifiLongSsidIsKept()
    {
        m_accessPoint = std::make_unique<FakeAccessPoint>();
        m_device = std::make_unique<FakeDevice>();
        m_connection = std::make_unique<FakeConnection>();
        m_nm = std::make_unique<FakeNM>();
        QVERIFY(serve(m_accessPoint.get(), Wifi::service()));
        QVERIFY(serve(m_device.get()));
        QVERIFY(serve(m_connection.get()));
        QVERIFY(serve(m_nm.get()));
        m_accessPoint->ssid = QByteArray(32, 'a');
        Wifi wifi(bus());
        QTRY_COMPARE_WITH_TIMEOUT(wifi.ssid().size(), 32, 5000);
        QVERIFY(wifi.connected());
    }

    void wifiFollowsTheDeviceAccessPoint()
    {
        m_accessPoint = std::make_unique<FakeAccessPoint>();
        m_device = std::make_unique<FakeDevice>();
        m_connection = std::make_unique<FakeConnection>();
        m_nm = std::make_unique<FakeNM>();
        QVERIFY(serve(m_accessPoint.get(), Wifi::service()));
        QVERIFY(serve(m_device.get()));
        QVERIFY(serve(m_connection.get()));
        QVERIFY(serve(m_nm.get()));
        Wifi wifi(bus());
        QTRY_COMPARE_WITH_TIMEOUT(wifi.ssid(), u"NSD1K-DB30-a"_s, 5000);
        m_device->accessPoint = u"/org/freedesktop/NetworkManager/AccessPoint/2"_s;
        m_device->announce(Wifi::deviceInterface(),
                           {{u"ActiveAccessPoint"_s,
                             QVariant::fromValue(QDBusObjectPath(m_device->accessPoint))}});
        QTRY_VERIFY_WITH_TIMEOUT(!wifi.connected(), 5000);
    }

    void wifiReloadsTheDeviceListOnTheSamePrimary()
    {
        m_accessPoint = std::make_unique<FakeAccessPoint>();
        m_device = std::make_unique<FakeDevice>();
        m_connection = std::make_unique<FakeConnection>();
        m_nm = std::make_unique<FakeNM>();
        QVERIFY(serve(m_accessPoint.get(), Wifi::service()));
        QVERIFY(serve(m_device.get()));
        QVERIFY(serve(m_connection.get()));
        QVERIFY(serve(m_nm.get()));
        Wifi wifi(bus());
        QTRY_COMPARE_WITH_TIMEOUT(wifi.ssid(), u"NSD1K-DB30-a"_s, 5000);
        auto *second = new FakeDevice();
        second->m_address = m_address;
        second->accessPoint = u"/org/freedesktop/NetworkManager/AccessPoint/2"_s;
        QVERIFY(bus().registerVirtualObject(u"/org/freedesktop/NetworkManager/Devices/3"_s, second));
        m_connection->devices = {QDBusObjectPath(u"/org/freedesktop/NetworkManager/Devices/3"_s)};
        m_connection->announce(Wifi::connectionInterface(),
                              {{u"Devices"_s, QVariant::fromValue(m_connection->devices)}});
        QTRY_VERIFY_WITH_TIMEOUT(!wifi.connected(), 5000);
        QCOMPARE(wifi.ssid(), QString());
        bus().unregisterObject(u"/org/freedesktop/NetworkManager/Devices/3"_s);
        delete second;
    }

    void wifiFollowsTheAccessPoint()
    {
        m_accessPoint = std::make_unique<FakeAccessPoint>();
        m_device = std::make_unique<FakeDevice>();
        m_connection = std::make_unique<FakeConnection>();
        m_nm = std::make_unique<FakeNM>();
        QVERIFY(serve(m_accessPoint.get(), Wifi::service()));
        QVERIFY(serve(m_device.get()));
        QVERIFY(serve(m_connection.get()));
        QVERIFY(serve(m_nm.get()));
        Wifi wifi(bus());
        QTRY_COMPARE_WITH_TIMEOUT(wifi.ssid(), u"NSD1K-DB30-a"_s, 5000);
        m_accessPoint->ssid = QByteArrayLiteral("NSD1K-DB30-g");
        m_accessPoint->announce(Wifi::accessPointInterface(), {{u"Ssid"_s, m_accessPoint->ssid}});
        QTRY_COMPARE_WITH_TIMEOUT(wifi.ssid(), u"NSD1K-DB30-g"_s, 5000);
    }
};

QTEST_GUILESS_MAIN(StatusTest)

#include "tst_launcherstatus.moc"
