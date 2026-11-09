#pragma once

#include "storeentry.h"
#include "storejob.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <atomic>
#include <functional>
#include <memory>

struct _GCancellable;

class GlibThread;
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

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void install(const QString &id);
    Q_INVOKABLE void uninstall(const QString &id);
    Q_INVOKABLE void cancel();

signals:
    void catalogReady(const QList<StoreEntry> &entries);
    void installedReady(const QStringList &ids);
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
    void setInstalledIds(const QStringList &ids);
    bool installedIds(QStringList *ids);

    std::unique_ptr<StoreBackend> m_backend;
    std::unique_ptr<GlibThread> m_thread;
    StoreJob *m_job = nullptr;
    QHash<StoreJob *, Request> m_requests;
    QStringList m_installedIds;
    bool m_installedValid = false;
    bool m_running = false;
    bool m_syncing = false;
};
