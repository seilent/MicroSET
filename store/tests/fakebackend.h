#pragma once

#include "storebackend.h"

#include <QHash>
#include <QList>
#include <QMutex>
#include <QString>
#include <QStringList>

#include <atomic>

class FakeBackend final : public StoreBackend
{
public:
    void setEntries(const QList<StoreEntry> &entries);
    void setInstalled(const QList<StoreEntry> &installed);
    void setSyncFails(bool fails, const QString &message = QString());
    void setCatalogFails(bool fails, const QString &message = QString());
    void setInstalledFails(bool fails, const QString &message = QString());
    void setInstallFails(const QString &id, const QString &message = QString());
    void setUninstallFails(const QString &id, const QString &message = QString());
    void setUninstallWarning(const QString &id, const QString &message);
    void setInstallSteps(int steps);
    void setInstallDelayMs(int ms);
    void setSyncDelayMs(int ms);

    int syncCount() const;
    QStringList uninstalled() const;
    QStringList calls() const;

    bool syncCatalog(QString *error) override;
    QList<StoreEntry> catalog(QString *error) override;
    QList<StoreEntry> installed(QString *error) override;
    bool install(const QString &id, _GCancellable *cancellable, const Progress &progress, QString *error) override;
    bool uninstall(const QString &id, _GCancellable *cancellable, const Progress &progress, QString *error) override;
    void cancel() override;

private:
    mutable QMutex m_lock;
    QList<StoreEntry> m_entries;
    QList<StoreEntry> m_installed;
    QHash<QString, QString> m_installErrors;
    QHash<QString, QString> m_uninstallErrors;
    QHash<QString, QString> m_uninstallWarnings;
    QString m_syncError;
    QString m_catalogError;
    QString m_installedError;
    bool m_syncFails = false;
    bool m_catalogFails = false;
    bool m_installedFails = false;
    int m_steps = 4;
    int m_delayMs = 2;
    int m_syncDelayMs = 0;
    int m_syncCount = 0;
    QStringList m_uninstalled;
    QStringList m_calls;
    std::atomic<bool> m_cancelled{false};
};
