#include "androidstatus.h"
#include "fakeandroid.h"
#include "fakemanager.h"
#include "harness.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QSignalSpy>
#include <QTest>

#include <memory>

using namespace Qt::StringLiterals;

class AndroidStatusTest : public QObject
{
    Q_OBJECT

    Harness m_h;
    std::unique_ptr<FakeManager> m_manager;

    QDBusConnection bus(const QString &name) { return QDBusConnection::connectToBus(m_h.address(), name); }

    static QVariantMap ready()
    {
        return {{u"Image"_s, u"ready"_s}, {u"ImageSet"_s, u"los20av-20260927-20261022"_s}, {u"GooglePlay"_s, u"off"_s},
                {u"GsfId"_s, QString()}, {u"LastResult"_s, u"ready"_s}, {u"LastReason"_s, QString()},
                {u"Storage"_s, u"card"_s}, {u"StorageCard"_s, u"0f6c5f0e-8a4e-4c0b-9d55-2f1d2c3b4a59"_s}};
    }

    bool startService(FakeAndroid *service) { return service->publish(bus(u"android"_s)); }

    void stopService()
    {
        bus(u"android"_s).interface()->unregisterService(AndroidStatus::Service);
        QDBusConnection::disconnectFromBus(u"android"_s);
    }

    bool startManager()
    {
        QDBusConnection conn = bus(u"userd"_s);
        m_manager = std::make_unique<FakeManager>(conn);
        return conn.registerVirtualObject(FakeManager::Path, m_manager.get(), QDBusConnection::SubPath)
            && conn.registerService(FakeManager::Service);
    }

private slots:
    void initTestCase() { QVERIFY(m_h.startBus()); }

    void cleanup()
    {
        stopService();
        QDBusConnection::disconnectFromBus(u"android2"_s);
        QDBusConnection::disconnectFromBus(u"userd"_s);
        m_manager.reset();
        QDBusConnection::disconnectFromBus(u"overlay"_s);
    }

    void noServiceNoStatus()
    {
        AndroidStatus android(bus(u"overlay"_s));
        QSignalSpy changed(&android, &AndroidStatus::changed);
        QTest::qWait(300);
        QVERIFY(android.status().isEmpty());
        QCOMPARE(changed.size(), 0);
    }

    void followsTheServiceOnTheBus()
    {
        AndroidStatus android(bus(u"overlay"_s));
        QSignalSpy changed(&android, &AndroidStatus::changed);
        FakeAndroid service;
        service.status = ready();
        QVERIFY(startService(&service));
        QTRY_COMPARE(android.status(), ready());
        QCOMPARE(changed.size(), 1);
        QCOMPARE(android.status().value(u"ImageSet"_s).toString(), u"los20av-20260927-20261022"_s);
        stopService();
        QTRY_VERIFY(android.status().isEmpty());
        QCOMPARE(changed.size(), 2);
    }

    void seesAServiceThatWasThereFirst()
    {
        FakeAndroid service;
        service.status = ready();
        QVERIFY(startService(&service));
        AndroidStatus android(bus(u"overlay"_s));
        QSignalSpy changed(&android, &AndroidStatus::changed);
        QVERIFY(android.status().isEmpty());
        QTRY_COMPARE(android.status(), ready());
        QCOMPARE(changed.size(), 1);
        QCOMPARE(service.calls, 1);
    }

    void takesThePropertiesChanged()
    {
        FakeAndroid service;
        service.status = ready();
        QVERIFY(startService(&service));
        AndroidStatus android(bus(u"overlay"_s));
        QSignalSpy changed(&android, &AndroidStatus::changed);
        QTRY_COMPARE(android.status(), ready());
        service.change({{u"GooglePlay"_s, u"updating"_s}, {u"GsfId"_s, u"4214478130512345678"_s}});
        QTRY_COMPARE(android.status().value(u"GooglePlay"_s).toString(), u"updating"_s);
        QCOMPARE(android.status().value(u"GsfId"_s).toString(), u"4214478130512345678"_s);
        QCOMPARE(android.status().value(u"Image"_s).toString(), u"ready"_s);
        QCOMPARE(changed.size(), 2);
        service.change({{u"GooglePlay"_s, u"on"_s}}, u"net.seilent.MicroOs.Other1"_s);
        QTest::qWait(200);
        QCOMPARE(android.status().value(u"GooglePlay"_s).toString(), u"updating"_s);
        QCOMPARE(changed.size(), 2);
        QCOMPARE(service.calls, 1);
        service.change({}, AndroidStatus::Interface, {u"GsfId"_s});
        QTRY_VERIFY(!android.status().contains(u"GsfId"_s));
        QCOMPARE(android.status().value(u"GooglePlay"_s).toString(), u"updating"_s);
        QCOMPARE(changed.size(), 3);
    }

    void aHeldFetchKeepsALaterChange()
    {
        FakeAndroid service;
        service.status = ready();
        service.hold = true;
        QVERIFY(startService(&service));
        AndroidStatus android(bus(u"overlay"_s));
        QTRY_COMPARE(service.held.size(), 1);
        service.change({{u"GooglePlay"_s, u"updating"_s}});
        QTRY_COMPARE(android.status().value(u"GooglePlay"_s).toString(), u"updating"_s);
        service.release();
        QVariantMap expected = ready();
        expected.insert(u"GooglePlay"_s, u"updating"_s);
        QTRY_COMPARE(android.status(), expected);
        QTest::qWait(200);
        QCOMPARE(android.status(), expected);
    }

    void aRefusedFetchKeepsTheStatus()
    {
        FakeAndroid service;
        service.status = ready();
        service.fail = true;
        QTest::ignoreMessage(QtInfoMsg,
                             "microset overlay: GetAll of net.seilent.MicroOs.Android1 failed: org.freedesktop.DBus.Error.Failed refused");
        QVERIFY(startService(&service));
        AndroidStatus android(bus(u"overlay"_s));
        QSignalSpy changed(&android, &AndroidStatus::changed);
        QTRY_COMPARE(service.calls, 1);
        QTest::qWait(200);
        QVERIFY(android.status().isEmpty());
        QCOMPARE(changed.size(), 0);
        service.change(ready());
        QTRY_COMPARE(android.status(), ready());
        QCOMPARE(changed.size(), 1);
    }

    void aRefusedRefetchKeepsTheStatus()
    {
        FakeAndroid first;
        first.status = ready();
        QDBusConnection one = bus(u"android"_s);
        QVERIFY(one.registerVirtualObject(AndroidStatus::Path, &first));
        QCOMPARE(one.interface()->registerService(AndroidStatus::Service, QDBusConnectionInterface::ReplaceExistingService,
                                                  QDBusConnectionInterface::AllowReplacement)
                     .value(),
                 QDBusConnectionInterface::ServiceRegistered);
        AndroidStatus android(bus(u"overlay"_s));
        QTRY_COMPARE(android.status(), ready());
        FakeAndroid second;
        second.status = ready();
        second.fail = true;
        QDBusConnection two = bus(u"android2"_s);
        QVERIFY(two.registerVirtualObject(AndroidStatus::Path, &second));
        QTest::ignoreMessage(QtInfoMsg,
                             "microset overlay: GetAll of net.seilent.MicroOs.Android1 failed: org.freedesktop.DBus.Error.Failed refused");
        QCOMPARE(two.interface()->registerService(AndroidStatus::Service, QDBusConnectionInterface::ReplaceExistingService)
                     .value(),
                 QDBusConnectionInterface::ServiceRegistered);
        QTRY_COMPARE(second.calls, 1);
        QTest::qWait(200);
        QCOMPARE(android.status(), ready());
        QCOMPARE(first.calls, 1);
        two.interface()->unregisterService(AndroidStatus::Service);
        QDBusConnection::disconnectFromBus(u"android2"_s);
        QTRY_VERIFY(android.status().isEmpty());
    }

    void aStaleReplyAfterTheServiceLeftIsDropped()
    {
        FakeAndroid service;
        service.status = ready();
        service.hold = true;
        QVERIFY(startService(&service));
        AndroidStatus android(bus(u"overlay"_s));
        QSignalSpy changed(&android, &AndroidStatus::changed);
        QTRY_COMPARE(service.held.size(), 1);
        bus(u"android"_s).interface()->unregisterService(AndroidStatus::Service);
        QTest::qWait(200);
        service.release();
        QTest::qWait(200);
        QVERIFY(android.status().isEmpty());
        QCOMPARE(changed.size(), 0);
    }

    void aRestartedServiceIsReadAgain()
    {
        FakeAndroid service;
        service.status = ready();
        QVERIFY(startService(&service));
        AndroidStatus android(bus(u"overlay"_s));
        QTRY_COMPARE(android.status(), ready());
        stopService();
        QTRY_VERIFY(android.status().isEmpty());
        FakeAndroid again;
        again.status = ready();
        again.status.insert({{u"GooglePlay"_s, u"on"_s}, {u"GsfId"_s, u"4214478130512345678"_s}});
        QVERIFY(startService(&again));
        QTRY_COMPARE(android.status(), again.status);
        QCOMPARE(service.calls, 1);
        QCOMPARE(again.calls, 1);
    }

    void installStartsTheUnit()
    {
        QVERIFY(startManager());
        AndroidStatus android(bus(u"overlay"_s));
        QSignalSpy failed(&android, &AndroidStatus::failed);
        android.installGooglePlay();
        QTRY_COMPARE(m_manager->calls, QStringList{u"StartUnit"_s});
        QCOMPARE(m_manager->units, QStringList{u"microos-android-gapps.service"_s});
        QCOMPARE(m_manager->modes, QStringList{u"replace"_s});
        QTest::qWait(200);
        QCOMPARE(failed.size(), 0);
    }

    void oneStartAtATime()
    {
        QVERIFY(startManager());
        m_manager->hold = true;
        AndroidStatus android(bus(u"overlay"_s));
        android.installGooglePlay();
        android.installGooglePlay();
        QTRY_COMPARE(m_manager->held.size(), 1);
        QTest::qWait(200);
        QCOMPARE(m_manager->units.size(), 1);
        m_manager->hold = false;
        m_manager->answer(m_manager->held.takeFirst());
        QTest::qWait(200);
        android.installGooglePlay();
        QTRY_COMPARE(m_manager->units.size(), 2);
        QCOMPARE(m_manager->units, (QStringList{u"microos-android-gapps.service"_s, u"microos-android-gapps.service"_s}));
    }

    void aRefusedStartFails()
    {
        QVERIFY(startManager());
        m_manager->fail = true;
        AndroidStatus android(bus(u"overlay"_s));
        QSignalSpy failed(&android, &AndroidStatus::failed);
        QTest::ignoreMessage(QtInfoMsg,
                             "microset overlay: start of microos-android-gapps.service failed: org.freedesktop.DBus.Error.Failed failed");
        android.installGooglePlay();
        QTRY_COMPARE(failed.size(), 1);
        QCOMPARE(m_manager->units, QStringList{u"microos-android-gapps.service"_s});
    }
};

QTEST_GUILESS_MAIN(AndroidStatusTest)
#include "tst_androidstatus.moc"
