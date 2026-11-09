#include "fakebackend.h"
#include "storecatalogmodel.h"
#include "storejob.h"
#include "storeworker.h"

#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTest>

#include <algorithm>
#include <memory>

using namespace Qt::StringLiterals;

namespace {

StoreEntry entryFor(const QString &id, const QString &name)
{
    StoreEntry entry;
    entry.id = id;
    entry.name = name;
    entry.summary = name + u" summary"_s;
    return entry;
}

}

class StoreWorkerTest : public QObject
{
    Q_OBJECT

    FakeBackend *m_fake = nullptr;
    std::unique_ptr<StoreWorker> m_worker;
    StoreCatalogModel m_model;

    void open()
    {
        auto backend = std::make_unique<FakeBackend>();
        m_fake = backend.get();
        m_worker = std::make_unique<StoreWorker>(std::move(backend));
        m_worker->start();
        connect(m_worker.get(), &StoreWorker::catalogReady, &m_model, &StoreCatalogModel::setEntries);
        connect(m_worker.get(), &StoreWorker::installedReady, &m_model, &StoreCatalogModel::setInstalled);
        connect(m_worker.get(), &StoreWorker::syncFinished, &m_model, [this](bool ok, const QString &) {
            if (ok)
                m_model.setSynced(true);
        });
    }

private slots:
    void cleanup()
    {
        m_worker.reset();
        m_fake = nullptr;
        m_model.release();
        m_model.setSynced(false);
    }

    void catalogSyncPopulatesTheModel()
    {
        open();
        m_fake->setEntries({entryFor(u"org.example.Alpha"_s, u"Alpha"_s),
                            entryFor(u"org.example.Beta"_s, u"Beta"_s)});
        QSignalSpy sync(&*m_worker, &StoreWorker::syncFinished);
        m_worker->refresh();
        QTRY_COMPARE(sync.count(), 1);
        QCOMPARE(sync.at(0).at(0).toBool(), true);
        QCOMPARE(m_model.count(), 2);
        QVERIFY(m_model.synced());
        QVERIFY(!m_model.offline());
    }

    void loadEmitsTheCachedCatalogWithoutSyncing()
    {
        open();
        m_fake->setEntries({entryFor(u"org.example.Alpha"_s, u"Alpha"_s),
                            entryFor(u"org.example.Beta"_s, u"Beta"_s)});
        m_fake->setInstalled({entryFor(u"org.example.Beta"_s, u"Beta"_s)});
        QSignalSpy sync(&*m_worker, &StoreWorker::syncFinished);
        m_worker->load();
        QTRY_COMPARE(m_model.count(), 2);
        QCOMPARE(m_fake->syncCount(), 0);
        QCOMPARE(sync.count(), 0);
        QCOMPARE(m_model.rows().at(1).toMap().value(u"installed"_s).toBool(), true);
        QVERIFY(!m_model.synced());
        QSignalSpy installedReady(&*m_worker, &StoreWorker::installedReady);
        m_worker->load();
        QTRY_COMPARE(installedReady.count(), 1);
        QCOMPARE(installedReady.last().at(0).value<QList<StoreEntry>>().size(), 1);
        m_worker->refresh();
        QTRY_COMPARE(sync.count(), 1);
        QCOMPARE(m_fake->syncCount(), 1);
    }

    void failedSyncKeepsTheCatalog()
    {
        open();
        m_fake->setEntries({entryFor(u"org.example.Alpha"_s, u"Alpha"_s),
                            entryFor(u"org.example.Beta"_s, u"Beta"_s)});
        QSignalSpy sync(&*m_worker, &StoreWorker::syncFinished);
        m_worker->refresh();
        QTRY_COMPARE(sync.count(), 1);
        QCOMPARE(m_model.count(), 2);

        m_fake->setSyncFails(true, u"offline"_s);
        m_worker->refresh();
        QTRY_COMPARE(sync.count(), 2);
        QCOMPARE(sync.at(1).at(0).toBool(), false);
        QCOMPARE(sync.at(1).at(1).toString(), u"offline"_s);
        QCOMPARE(m_model.count(), 2);
        QVERIFY(m_model.synced());
        QVERIFY(!m_model.offline());
    }

    void failedCatalogKeepsTheModel()
    {
        open();
        m_fake->setEntries({entryFor(u"org.example.Alpha"_s, u"Alpha"_s),
                            entryFor(u"org.example.Beta"_s, u"Beta"_s)});
        QSignalSpy sync(&*m_worker, &StoreWorker::syncFinished);
        m_worker->refresh();
        QTRY_COMPARE(sync.count(), 1);
        QCOMPARE(m_model.count(), 2);

        m_fake->setCatalogFails(true, u"broken cache"_s);
        m_worker->refresh();
        QTRY_COMPARE(sync.count(), 2);
        QCOMPARE(sync.at(1).at(0).toBool(), false);
        QCOMPARE(sync.at(1).at(1).toString(), u"broken cache"_s);
        QCOMPARE(m_model.count(), 2);
        QVERIFY(m_model.synced());
    }

    void failedInstalledKeepsTheInstalledState()
    {
        open();
        StoreEntry installed = entryFor(u"org.example.Alpha"_s, u"Alpha"_s);
        installed.installed = true;
        m_fake->setEntries({entryFor(u"org.example.Alpha"_s, u"Alpha"_s)});
        m_fake->setInstalled({installed});
        QSignalSpy sync(&*m_worker, &StoreWorker::syncFinished);
        m_worker->refresh();
        QTRY_COMPARE(sync.count(), 1);
        QVERIFY(m_model.rows().at(0).toMap().value(u"installed"_s).toBool());

        m_fake->setInstalledFails(true, u"database locked"_s);
        m_worker->refresh();
        QTRY_COMPARE(sync.count(), 2);
        QCOMPARE(sync.at(1).at(0).toBool(), false);
        QCOMPARE(sync.at(1).at(1).toString(), u"database locked"_s);
        QCOMPARE(m_model.count(), 1);
        QVERIFY(m_model.rows().at(0).toMap().value(u"installed"_s).toBool());
    }

    void neverSyncedIsOffline()
    {
        open();
        m_fake->setSyncFails(true, u"network down"_s);
        QSignalSpy sync(&*m_worker, &StoreWorker::syncFinished);
        m_worker->refresh();
        QTRY_COMPARE(sync.count(), 1);
        QCOMPARE(sync.at(0).at(0).toBool(), false);
        QCOMPARE(m_model.count(), 0);
        QVERIFY(m_model.offline());
    }

    void installReportsProgress()
    {
        open();
        m_fake->setInstallSteps(5);
        m_fake->setInstallDelayMs(1);
        m_worker->install(u"org.example.Alpha"_s);
        StoreJob *job = m_worker->job();
        QVERIFY(job != nullptr);
        QCOMPARE(job->kind(), StoreJob::Install);
        auto states = std::make_shared<QStringList>();
        states->append(u"Queued"_s);
        auto percents = std::make_shared<QList<int>>();
        connect(job, &StoreJob::stateChanged, this,
                [states, job] { states->append(QString::number(int(job->state()))); });
        connect(job, &StoreJob::percentChanged, this, [percents, job] { percents->append(job->percent()); });
        QTRY_VERIFY(job->state() == StoreJob::Done);
        QVERIFY(states->contains(QString::number(int(StoreJob::Downloading))));
        QVERIFY(states->contains(QString::number(int(StoreJob::Installing))));
        QVERIFY(std::any_of(percents->begin(), percents->end(), [](int percent) {
            return percent > 0 && percent < 100;
        }));
        QCOMPARE(job->percent(), 100);
    }

    void installCanBeCancelled()
    {
        open();
        m_fake->setInstallSteps(40);
        m_fake->setInstallDelayMs(5);
        QSignalSpy finished(&*m_worker, &StoreWorker::jobFinished);
        m_worker->install(u"org.example.Alpha"_s);
        StoreJob *job = m_worker->job();
        QVERIFY(job != nullptr);
        QTRY_VERIFY(job->state() == StoreJob::Downloading && job->percent() > 0);
        m_worker->cancel();
        QCOMPARE(job->state(), StoreJob::Cancelled);
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(job->state(), StoreJob::Cancelled);
        QVERIFY(m_fake->installed(nullptr).isEmpty());
    }

    void installFailureReachesTheJob()
    {
        open();
        m_fake->setInstallFails(u"org.example.Alpha"_s, u"disk full"_s);
        m_worker->install(u"org.example.Alpha"_s);
        StoreJob *job = m_worker->job();
        QVERIFY(job != nullptr);
        QTRY_VERIFY(job->state() == StoreJob::Failed);
        QCOMPARE(job->message(), u"disk full"_s);
        QVERIFY(!job->running());
    }

    void uninstallRemoves()
    {
        open();
        StoreEntry installed = entryFor(u"org.example.Alpha"_s, u"Alpha"_s);
        installed.installed = true;
        m_fake->setInstalled({installed});
        QSignalSpy installedReady(&*m_worker, &StoreWorker::installedReady);
        m_worker->uninstall(u"org.example.Alpha"_s);
        StoreJob *job = m_worker->job();
        QVERIFY(job != nullptr);
        QCOMPARE(job->kind(), StoreJob::Uninstall);
        QTRY_VERIFY(job->state() == StoreJob::Done);
        QCOMPARE(m_fake->uninstalled(), QStringList{u"org.example.Alpha"_s});
        QTRY_VERIFY(!installedReady.isEmpty());
        QVERIFY(installedReady.last().at(0).value<QList<StoreEntry>>().isEmpty());
    }

    void uninstallFailureReachesTheJob()
    {
        open();
        StoreEntry installed = entryFor(u"org.example.Alpha"_s, u"Alpha"_s);
        installed.installed = true;
        m_fake->setInstalled({installed});
        m_fake->setUninstallFails(u"org.example.Alpha"_s, u"app is running"_s);
        m_worker->uninstall(u"org.example.Alpha"_s);
        StoreJob *job = m_worker->job();
        QVERIFY(job != nullptr);
        QTRY_VERIFY(job->state() == StoreJob::Failed);
        QCOMPARE(job->message(), u"app is running"_s);
    }

    void uninstallCleanupWarningKeepsTheJobDone()
    {
        open();
        StoreEntry installed = entryFor(u"org.example.Alpha"_s, u"Alpha"_s);
        installed.installed = true;
        m_fake->setInstalled({installed});
        m_fake->setUninstallWarning(u"org.example.Alpha"_s, u"runtime cleanup failed"_s);
        m_worker->uninstall(u"org.example.Alpha"_s);
        StoreJob *job = m_worker->job();
        QVERIFY(job != nullptr);
        QTRY_VERIFY(job->state() == StoreJob::Done);
        QCOMPARE(job->message(), u"runtime cleanup failed"_s);
    }

    void jobsRunInOrder()
    {
        open();
        StoreEntry installed = entryFor(u"org.example.Beta"_s, u"Beta"_s);
        installed.installed = true;
        m_fake->setInstalled({installed});
        QSignalSpy sync(&*m_worker, &StoreWorker::syncFinished);
        m_worker->refresh();
        m_worker->install(u"org.example.Alpha"_s);
        m_worker->uninstall(u"org.example.Beta"_s);
        QTRY_COMPARE(sync.count(), 1);
        QTRY_COMPARE(int(m_fake->calls().size()), 3);
        QCOMPARE(m_fake->calls(),
                 (QStringList{u"sync"_s, u"install:org.example.Alpha"_s, u"uninstall:org.example.Beta"_s}));
    }

    void cancellingAQueuedJobKeepsTheRunningJob()
    {
        open();
        m_fake->setInstallSteps(20);
        m_fake->setInstallDelayMs(10);
        m_worker->install(u"org.example.Alpha"_s);
        StoreJob *running = m_worker->job();
        QVERIFY(running != nullptr);
        QTRY_VERIFY(running->state() == StoreJob::Downloading && running->percent() > 0);
        m_worker->install(u"org.example.Beta"_s);
        StoreJob *queued = m_worker->job();
        QVERIFY(queued != nullptr);
        QVERIFY(queued != running);
        QCOMPARE(queued->state(), StoreJob::Queued);
        QSignalSpy installedReady(&*m_worker, &StoreWorker::installedReady);
        m_worker->cancel();
        QCOMPARE(queued->state(), StoreJob::Cancelled);
        QTRY_COMPARE(installedReady.count(), 1);
        QTRY_COMPARE(m_fake->installed(nullptr).size(), 1);
        QCOMPARE(m_fake->installed(nullptr).at(0).id, u"org.example.Alpha"_s);
        QCOMPARE(m_fake->calls(), QStringList{u"install:org.example.Alpha"_s});
    }

    void jobsKeepTheirOwnApp()
    {
        open();
        m_fake->setInstallSteps(20);
        m_fake->setInstallDelayMs(10);
        m_worker->install(u"org.example.Alpha"_s);
        m_worker->install(u"org.example.Beta"_s);
        StoreJob *alpha = m_worker->jobFor(u"org.example.Alpha"_s);
        StoreJob *beta = m_worker->jobFor(u"org.example.Beta"_s);
        QVERIFY(alpha != nullptr);
        QVERIFY(beta != nullptr);
        QVERIFY(alpha != beta);
        QCOMPARE(alpha->id(), u"org.example.Alpha"_s);
        QCOMPARE(beta->id(), u"org.example.Beta"_s);
        QTRY_VERIFY(alpha->state() == StoreJob::Downloading);
        QCOMPARE(beta->state(), StoreJob::Queued);
        QVERIFY(!m_worker->idle());
        m_worker->cancelFor(u"org.example.Beta"_s);
        QCOMPARE(beta->state(), StoreJob::Cancelled);
        QVERIFY(alpha->running());
        QTRY_COMPARE(m_worker->jobFor(u"org.example.Beta"_s), nullptr);
        QTRY_VERIFY(alpha->state() == StoreJob::Done);
        QVERIFY(m_worker->idle());
        QCOMPARE(m_worker->jobFor(u"org.example.Alpha"_s), nullptr);
    }

    void cancellingDoesNotAbortASync()
    {
        open();
        m_fake->setEntries({entryFor(u"org.example.Alpha"_s, u"Alpha"_s)});
        m_fake->setSyncDelayMs(150);
        m_fake->setInstallSteps(20);
        m_fake->setInstallDelayMs(5);
        QSignalSpy sync(&*m_worker, &StoreWorker::syncFinished);
        QSignalSpy finished(&*m_worker, &StoreWorker::jobFinished);
        m_worker->refresh();
        m_worker->install(u"org.example.Alpha"_s);
        StoreJob *job = m_worker->job();
        QVERIFY(job != nullptr);
        m_worker->cancel();
        QCOMPARE(job->state(), StoreJob::Cancelled);
        QTRY_COMPARE(sync.count(), 1);
        QCOMPARE(sync.at(0).at(0).toBool(), true);
        QCOMPARE(m_fake->syncCount(), 1);
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(m_model.count(), 1);
        QCOMPARE(m_fake->calls(), QStringList{u"sync"_s});
    }

    void shutdownWithAnInstallInFlight()
    {
        open();
        m_fake->setInstallSteps(100);
        m_fake->setInstallDelayMs(40);
        m_worker->install(u"org.example.Alpha"_s);
        StoreJob *job = m_worker->job();
        QVERIFY(job != nullptr);
        QTRY_VERIFY(job->state() == StoreJob::Downloading && job->percent() > 0);
        QElapsedTimer timer;
        timer.start();
        m_worker.reset();
        QVERIFY2(timer.elapsed() < 2000, "stop waited for the install to finish");
    }
};

QTEST_GUILESS_MAIN(StoreWorkerTest)

#include "tst_storeworker.moc"
