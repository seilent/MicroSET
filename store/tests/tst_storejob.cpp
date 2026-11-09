#include "storejob.h"

#include <QSignalSpy>
#include <QTest>

using namespace Qt::StringLiterals;

class StoreJobTest : public QObject
{
    Q_OBJECT

private slots:
    void installRunsToDone()
    {
        StoreJob job(StoreJob::Install, u"org.example.Alpha"_s);
        QCOMPARE(job.state(), StoreJob::Queued);
        QVERIFY(job.running());

        job.start();
        QCOMPARE(job.state(), StoreJob::Downloading);

        job.progress(42);
        QCOMPARE(job.percent(), 42);

        job.beginInstall();
        QCOMPARE(job.state(), StoreJob::Installing);
        QCOMPARE(job.percent(), 100);

        job.finish();
        QCOMPARE(job.state(), StoreJob::Done);
        QVERIFY(!job.running());
    }

    void uninstallStartsInstalling()
    {
        StoreJob job(StoreJob::Uninstall, u"org.example.Alpha"_s);
        job.start();
        QCOMPARE(job.state(), StoreJob::Installing);
        job.finish();
        QCOMPARE(job.state(), StoreJob::Done);
    }

    void failureKeepsTheMessage()
    {
        StoreJob job(StoreJob::Install, u"org.example.Alpha"_s);
        job.start();
        job.fail(u"disk full"_s);
        QCOMPARE(job.state(), StoreJob::Failed);
        QCOMPARE(job.message(), u"disk full"_s);
    }

    void failureFromQueued()
    {
        StoreJob job(StoreJob::Install, u"org.example.Alpha"_s);
        job.fail(u"no remote"_s);
        QCOMPARE(job.state(), StoreJob::Failed);
        QCOMPARE(job.message(), u"no remote"_s);
    }

    void cancelStopsTheJob()
    {
        StoreJob job(StoreJob::Install, u"org.example.Alpha"_s);
        job.start();
        job.progress(10);
        job.cancel();
        QCOMPARE(job.state(), StoreJob::Cancelled);
        job.finish();
        QCOMPARE(job.state(), StoreJob::Cancelled);
    }

    void terminalStatesDoNotMove()
    {
        StoreJob done(StoreJob::Install, u"org.example.Alpha"_s);
        done.start();
        done.finish();
        done.fail(u"late"_s);
        done.cancel();
        QCOMPARE(done.state(), StoreJob::Done);

        StoreJob failed(StoreJob::Install, u"org.example.Beta"_s);
        failed.start();
        failed.fail(u"broken"_s);
        failed.finish();
        QCOMPARE(failed.state(), StoreJob::Failed);
    }

    void progressOnlyWhileDownloading()
    {
        StoreJob job(StoreJob::Install, u"org.example.Alpha"_s);
        job.progress(30);
        QCOMPARE(job.percent(), 0);
        job.start();
        job.progress(150);
        QCOMPARE(job.percent(), 100);
        job.beginInstall();
        job.progress(20);
        QCOMPARE(job.percent(), 100);
    }

    void progressNeverGoesBackwards()
    {
        StoreJob job(StoreJob::Install, u"org.example.Alpha"_s);
        job.start();
        job.progress(40);
        job.progress(20);
        QCOMPARE(job.percent(), 40);
        job.progress(-1);
        QCOMPARE(job.percent(), 40);
        job.progress(60);
        QCOMPARE(job.percent(), 60);
    }

    void messageIsSetWithoutChangingTheState()
    {
        StoreJob job(StoreJob::Install, u"org.example.Alpha"_s);
        job.start();
        QSignalSpy messages(&job, &StoreJob::messageChanged);
        job.setMessage(u"org.example.Alpha is end of life"_s);
        QCOMPARE(job.message(), u"org.example.Alpha is end of life"_s);
        QCOMPARE(job.state(), StoreJob::Downloading);
        QCOMPARE(messages.count(), 1);
        job.setMessage(u"org.example.Alpha is end of life"_s);
        QCOMPARE(messages.count(), 1);
    }

    void signalsFollowTheState()
    {
        StoreJob job(StoreJob::Install, u"org.example.Alpha"_s);
        QSignalSpy states(&job, &StoreJob::stateChanged);
        QSignalSpy percents(&job, &StoreJob::percentChanged);
        job.start();
        job.progress(50);
        job.beginInstall();
        job.finish();
        QCOMPARE(states.count(), 3);
        QVERIFY(percents.count() >= 2);
    }
};

QTEST_GUILESS_MAIN(StoreJobTest)

#include "tst_storejob.moc"
