#pragma once

#include "storeentry.h"
#include "storejob.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>

#include <atomic>
#include <functional>
#include <memory>

struct _GCancellable;

class StoreGlibThread;
class StoreBackend;

class StoreWorker : public QObject
{
    Q_OBJECT
    Q_PROPERTY(StoreJob *job READ job NOTIFY jobChanged)

public:
    explicit StoreWorker(std::unique_ptr<StoreBackend> backend, QObject *parent = nullptr);
    ~StoreWorker() override;

    void start();
    void stop();

    StoreJob *job() const;
    StoreJob *jobFor(const QString &id) const;
    bool idle() const;

    Q_INVOKABLE void load();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void install(const QString &id);
    Q_INVOKABLE void uninstall(const QString &id);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void cancelFor(const QString &id);

signals:
    void catalogReady(const QList<StoreEntry> &entries);
    void installedReady(const QList<StoreEntry> &installed);
    void syncFinished(bool ok, const QString &error);
    void jobChanged();
    void jobFinished(StoreJob *job);

private:
    struct Request
    {
        std::shared_ptr<std::atomic<bool>> cancelled;
        _GCancellable *cancellable = nullptr;
    };

    bool enqueue(std::function<void()> work);
    void setJob(StoreJob *job);
    void releaseRequest(StoreJob *job);
    void finishJob(StoreJob *job, bool ok, bool cancelled, const QString &error);
    void setInstalled(const QList<StoreEntry> &installed, bool force = false);
    bool installedEntries(QList<StoreEntry> *entries, QString *error = nullptr);

    std::unique_ptr<StoreBackend> m_backend;
    std::unique_ptr<StoreGlibThread> m_thread;
    StoreJob *m_job = nullptr;
    QHash<StoreJob *, Request> m_requests;
    QList<StoreEntry> m_installed;
    bool m_installedValid = false;
    bool m_running = false;
    bool m_syncing = false;
};
