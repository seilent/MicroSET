#pragma once

#include "storebackend.h"

#include <QHash>
#include <QMutex>
#include <QString>
#include <QStringList>

struct _FlatpakInstallation;
struct _GCancellable;

class FlatpakBackend final : public StoreBackend
{
public:
    explicit FlatpakBackend(const QString &remote = QStringLiteral("flathub"),
                            const QString &arch = QStringLiteral("aarch64"));
    ~FlatpakBackend() override;

    bool syncCatalog(QString *error) override;
    QList<StoreEntry> catalog(QString *error) override;
    QList<StoreEntry> installed(QString *error) override;
    bool install(const QString &id, _GCancellable *cancellable, const Progress &progress, QString *error) override;
    bool uninstall(const QString &id, _GCancellable *cancellable, const Progress &progress, QString *error) override;
    void cancel() override;

private:
    _FlatpakInstallation *installation(QString *error);
    void beginOperation(_GCancellable *cancellable);
    void endOperation(_GCancellable *cancellable);
    QString refFor(const QString &id) const;
    QStringList installedRefs(_FlatpakInstallation *installation, const QString &id, QString *error);

    QString m_remote;
    QString m_arch;
    QHash<QString, QString> m_refs;
    _FlatpakInstallation *m_installation = nullptr;
    mutable QMutex m_lock;
    _GCancellable *m_active = nullptr;
};
