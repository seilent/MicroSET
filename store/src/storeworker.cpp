#include "storeworker.h"

#include "storeglibthread.h"
#include "storebackend.h"
#include "storejob.h"

#include <QHash>
#include <QMetaObject>

#include <atomic>

#pragma push_macro("signals")
#undef signals
#include <gio/gio.h>
#pragma pop_macro("signals")

using namespace Qt::StringLiterals;

namespace {

using CancelFlag = std::shared_ptr<std::atomic<bool>>;

CancelFlag makeCancelFlag()
{
    return std::make_shared<std::atomic<bool>>(false);
}

}

StoreWorker::StoreWorker(std::unique_ptr<StoreBackend> backend, QObject *parent)
    : QObject(parent)
    , m_backend(std::move(backend))
    , m_thread(std::make_unique<StoreGlibThread>())
{
    qRegisterMetaType<StoreEntry>();
    qRegisterMetaType<QList<StoreEntry>>();
}

StoreWorker::~StoreWorker()
{
    stop();
}

void StoreWorker::start()
{
    if (m_running)
        return;
    if (m_thread == nullptr)
        m_thread = std::make_unique<StoreGlibThread>();
    m_thread->startAndWait();
    m_running = true;
}

void StoreWorker::stop()
{
    m_running = false;
    QList<_GCancellable *> cancellables;
    for (auto it = m_requests.begin(); it != m_requests.end(); ++it) {
        it->cancelled->store(true);
        if (it->cancellable == nullptr)
            continue;
        g_cancellable_cancel(it->cancellable);
        cancellables << it->cancellable;
    }
    m_requests.clear();
    m_backend->cancel();
    if (m_thread != nullptr) {
        m_thread->stop();
        m_thread.reset();
    }
    for (_GCancellable *cancellable : cancellables)
        g_object_unref(cancellable);
}

StoreJob *StoreWorker::job() const
{
    return m_job;
}

bool StoreWorker::enqueue(std::function<void()> work)
{
    if (!m_running || m_thread == nullptr)
        return false;
    return m_thread->post(std::move(work));
}

void StoreWorker::setJob(StoreJob *job)
{
    StoreJob *previous = m_job;
    m_job = job;
    if (previous != nullptr && previous != job && !previous->running())
        previous->deleteLater();
    emit jobChanged();
}

void StoreWorker::releaseRequest(StoreJob *job)
{
    const auto it = m_requests.find(job);
    if (it == m_requests.end())
        return;
    _GCancellable *cancellable = it->cancellable;
    m_requests.erase(it);
    if (cancellable != nullptr)
        g_object_unref(cancellable);
}

void StoreWorker::finishJob(StoreJob *job, bool ok, bool cancelled, const QString &error)
{
    releaseRequest(job);
    if (ok)
        job->finish();
    else if (cancelled)
        job->cancel();
    else
        job->fail(error);
    if (job != m_job)
        job->deleteLater();
    emit jobFinished(job);
}

void StoreWorker::setInstalledIds(const QStringList &ids)
{
    if (m_installedValid && m_installedIds == ids)
        return;
    m_installedIds = ids;
    m_installedValid = true;
    emit installedReady(ids);
}

bool StoreWorker::installedIds(QStringList *ids)
{
    QString error;
    const QList<StoreEntry> installed = m_backend->installed(&error);
    if (!error.isEmpty())
        return false;
    QStringList collected;
    for (const StoreEntry &entry : installed)
        collected << entry.id;
    collected.sort();
    *ids = collected;
    return true;
}

void StoreWorker::refresh()
{
    if (!m_running || m_syncing)
        return;
    m_syncing = true;
    const bool posted = enqueue([this] {
        QString syncError;
        const bool synced = m_backend->syncCatalog(&syncError);
        QString catalogError;
        const QList<StoreEntry> entries = m_backend->catalog(&catalogError);
        QString installedError;
        const QList<StoreEntry> installed = m_backend->installed(&installedError);
        QStringList ids;
        for (const StoreEntry &entry : installed)
            ids << entry.id;
        ids.sort();
        const bool catalogOk = catalogError.isEmpty();
        const bool installedOk = installedError.isEmpty();
        QString error = syncError;
        if (error.isEmpty())
            error = catalogError;
        if (error.isEmpty())
            error = installedError;
        const bool ok = synced && catalogOk && installedOk;
        QMetaObject::invokeMethod(
            this,
            [this, ok, error, entries, catalogOk, installedOk, ids] {
                m_syncing = false;
                if (catalogOk)
                    emit catalogReady(entries);
                if (installedOk)
                    setInstalledIds(ids);
                emit syncFinished(ok, error);
            },
            Qt::QueuedConnection);
    });
    if (!posted)
        m_syncing = false;
}

void StoreWorker::install(const QString &id)
{
    if (id.isEmpty() || !m_running)
        return;
    auto *job = new StoreJob(StoreJob::Install, id, this);
    Request request;
    request.cancelled = makeCancelFlag();
    request.cancellable = g_cancellable_new();
    m_requests.insert(job, request);
    setJob(job);
    _GCancellable *cancellable = request.cancellable;
    const CancelFlag cancelled = request.cancelled;
    const bool posted = enqueue([this, id, job, cancellable, cancelled] {
        if (cancelled->load() || g_cancellable_is_cancelled(cancellable)) {
            QMetaObject::invokeMethod(
                this,
                [this, job] {
                    releaseRequest(job);
                    if (job != m_job)
                        job->deleteLater();
                    emit jobFinished(job);
                },
                Qt::QueuedConnection);
            return;
        }
        QMetaObject::invokeMethod(
            this,
            [job] {
                if (job->state() == StoreJob::Queued)
                    job->start();
            },
            Qt::QueuedConnection);
        StoreBackend::Progress progress = [this, job](const StoreProgress &update) {
            QMetaObject::invokeMethod(
                this,
                [job, update] {
                    if (!update.message.isEmpty())
                        job->setMessage(update.message);
                    if (update.installing)
                        job->beginInstall();
                    else
                        job->progress(update.percent);
                },
                Qt::QueuedConnection);
        };
        QString error;
        const bool ok = m_backend->install(id, cancellable, progress, &error);
        const bool cancelledNow = g_cancellable_is_cancelled(cancellable);
        QStringList ids;
        const bool installedOk = installedIds(&ids);
        QMetaObject::invokeMethod(
            this,
            [this, job, ok, cancelledNow, error, installedOk, ids] {
                finishJob(job, ok, cancelledNow, error);
                if (installedOk)
                    setInstalledIds(ids);
            },
            Qt::QueuedConnection);
    });
    if (!posted) {
        releaseRequest(job);
        job->fail(u"the store is not running"_s);
    }
}

void StoreWorker::uninstall(const QString &id)
{
    if (id.isEmpty() || !m_running)
        return;
    auto *job = new StoreJob(StoreJob::Uninstall, id, this);
    Request request;
    request.cancelled = makeCancelFlag();
    request.cancellable = g_cancellable_new();
    m_requests.insert(job, request);
    setJob(job);
    _GCancellable *cancellable = request.cancellable;
    const CancelFlag cancelled = request.cancelled;
    const bool posted = enqueue([this, id, job, cancellable, cancelled] {
        if (cancelled->load() || g_cancellable_is_cancelled(cancellable)) {
            QMetaObject::invokeMethod(
                this,
                [this, job] {
                    releaseRequest(job);
                    if (job != m_job)
                        job->deleteLater();
                    emit jobFinished(job);
                },
                Qt::QueuedConnection);
            return;
        }
        QMetaObject::invokeMethod(
            this,
            [job] {
                if (job->state() == StoreJob::Queued)
                    job->start();
            },
            Qt::QueuedConnection);
        StoreBackend::Progress progress = [this, job](const StoreProgress &update) {
            QMetaObject::invokeMethod(
                this,
                [job, update] {
                    if (!update.message.isEmpty())
                        job->setMessage(update.message);
                    if (update.installing)
                        job->beginInstall();
                    else
                        job->progress(update.percent);
                },
                Qt::QueuedConnection);
        };
        QString error;
        const bool ok = m_backend->uninstall(id, cancellable, progress, &error);
        const bool cancelledNow = g_cancellable_is_cancelled(cancellable);
        QStringList ids;
        const bool installedOk = installedIds(&ids);
        QMetaObject::invokeMethod(
            this,
            [this, job, ok, cancelledNow, error, installedOk, ids] {
                finishJob(job, ok, cancelledNow, error);
                if (installedOk)
                    setInstalledIds(ids);
            },
            Qt::QueuedConnection);
    });
    if (!posted) {
        releaseRequest(job);
        job->fail(u"the store is not running"_s);
    }
}

void StoreWorker::cancel()
{
    StoreJob *job = m_job;
    if (job == nullptr)
        return;
    const auto it = m_requests.constFind(job);
    if (it != m_requests.constEnd()) {
        it->cancelled->store(true);
        if (it->cancellable != nullptr)
            g_cancellable_cancel(it->cancellable);
    }
    job->cancel();
}
