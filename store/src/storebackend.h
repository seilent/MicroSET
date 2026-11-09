#pragma once

#include "storeentry.h"

#include <QList>
#include <QString>

#include <functional>

struct _GCancellable;

class StoreBackend
{
public:
    using Progress = std::function<void(const StoreProgress &)>;

    virtual ~StoreBackend() = default;

    virtual bool syncCatalog(QString *error) = 0;
    virtual QList<StoreEntry> catalog(QString *error) = 0;
    virtual QList<StoreEntry> installed(QString *error) = 0;
    virtual bool install(const QString &id, _GCancellable *cancellable, const Progress &progress, QString *error) = 0;
    virtual bool uninstall(const QString &id, _GCancellable *cancellable, const Progress &progress,
                           QString *error) = 0;
    virtual void cancel() = 0;
};
