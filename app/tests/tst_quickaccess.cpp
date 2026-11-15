#include "fakepowerkey.h"
#include "harness.h"
#include "quickaccess.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QSignalSpy>
#include <QTest>

using namespace Qt::StringLiterals;

class QuickAccessTest : public QObject
{
    Q_OBJECT

    Harness m_h;

    QDBusConnection bus(const QString &name) { return QDBusConnection::connectToBus(m_h.address(), name); }

    bool startHelper(FakePowerKey *helper) { return helper->publish(bus(u"helper"_s)); }

    void stopHelper()
    {
        bus(u"helper"_s).interface()->unregisterService(QuickAccess::Service);
        QDBusConnection::disconnectFromBus(u"helper"_s);
    }

private slots:
    void initTestCase() { QVERIFY(m_h.startBus()); }

    void cleanup()
    {
        stopHelper();
        QDBusConnection::disconnectFromBus(u"overlay"_s);
    }

    void followsTheHelperOnTheBus()
    {
        QuickAccess quick(bus(u"overlay"_s), u"steam"_s);
        QSignalSpy changed(&quick, &QuickAccess::availableChanged);
        QVERIFY(!quick.available());
        FakePowerKey helper;
        QVERIFY(startHelper(&helper));
        QTRY_VERIFY(quick.available());
        QCOMPARE(changed.size(), 1);
        stopHelper();
        QTRY_VERIFY(!quick.available());
        QCOMPARE(changed.size(), 2);
    }

    void seesAHelperThatWasThereFirst()
    {
        FakePowerKey helper;
        QVERIFY(startHelper(&helper));
        QuickAccess quick(bus(u"overlay"_s), u"steam"_s);
        QSignalSpy changed(&quick, &QuickAccess::availableChanged);
        QVERIFY(!quick.available());
        QTRY_VERIFY(quick.available());
        QCOMPARE(changed.size(), 1);
    }

    void onlyInTheSteamSession()
    {
        FakePowerKey helper;
        QVERIFY(startHelper(&helper));
        for (const QString &session : {u"android"_s, u"microdap"_s, QString()}) {
            QuickAccess quick(bus(u"overlay"_s), session);
            QSignalSpy failed(&quick, &QuickAccess::failed);
            QVERIFY(!quick.available());
            quick.open();
            QTest::qWait(200);
            QCOMPARE(helper.calls, 0);
            QCOMPARE(failed.size(), 0);
        }
    }

    void openCallsTheHelperOnce()
    {
        FakePowerKey helper;
        QVERIFY(startHelper(&helper));
        QuickAccess quick(bus(u"overlay"_s), u"steam"_s);
        QSignalSpy opened(&quick, &QuickAccess::opened);
        QSignalSpy failed(&quick, &QuickAccess::failed);
        QTRY_VERIFY(quick.available());
        quick.open();
        quick.open();
        QTRY_COMPARE(opened.size(), 1);
        QTest::qWait(200);
        QCOMPARE(helper.calls, 1);
        QCOMPARE(failed.size(), 0);
        quick.open();
        QTRY_COMPARE(opened.size(), 2);
        QCOMPARE(helper.calls, 2);
    }

    void aRefusedCallFails()
    {
        FakePowerKey helper;
        helper.fail = true;
        QVERIFY(startHelper(&helper));
        QuickAccess quick(bus(u"overlay"_s), u"steam"_s);
        QSignalSpy opened(&quick, &QuickAccess::opened);
        QSignalSpy failed(&quick, &QuickAccess::failed);
        QTRY_VERIFY(quick.available());
        quick.open();
        QTRY_COMPARE(failed.size(), 1);
        QCOMPARE(opened.size(), 0);
        QCOMPARE(helper.calls, 1);
    }

    void noHelperNoCall()
    {
        QuickAccess quick(bus(u"overlay"_s), u"steam"_s);
        QSignalSpy failed(&quick, &QuickAccess::failed);
        quick.open();
        QTest::qWait(200);
        QCOMPARE(failed.size(), 0);
    }
};

QTEST_GUILESS_MAIN(QuickAccessTest)
#include "tst_quickaccess.moc"
