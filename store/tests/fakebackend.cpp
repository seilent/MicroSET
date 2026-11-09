#include "fakebackend.h"

#include <QMutexLocker>

#include <algorithm>
#include <thread>

#pragma push_macro("signals")
#undef signals
#include <gio/gio.h>
#pragma pop_macro("signals")

using namespace Qt::StringLiterals;

namespace {

bool stopped(std::atomic<bool> *cancelled, _GCancellable *cancellable)
{
    return cancelled->load() || (cancellable != nullptr && g_cancellable_is_cancelled(cancellable));
}

}

void FakeBackend::setEntries(const QList<StoreEntry> &entries)
{
    QMutexLocker lock(&m_lock);
    m_entries = entries;
}

void FakeBackend::setInstalled(const QList<StoreEntry> &installed)
{
    QMutexLocker lock(&m_lock);
    m_installed = installed;
}

void FakeBackend::setSyncFails(bool fails, const QString &message)
{
    QMutexLocker lock(&m_lock);
    m_syncFails = fails;
    m_syncError = message;
}

void FakeBackend::setCatalogFails(bool fails, const QString &message)
{
    QMutexLocker lock(&m_lock);
    m_catalogFails = fails;
    m_catalogError = message;
}

void FakeBackend::setInstalledFails(bool fails, const QString &message)
{
    QMutexLocker lock(&m_lock);
    m_installedFails = fails;
    m_installedError = message;
}

void FakeBackend::setInstallFails(const QString &id, const QString &message)
{
    QMutexLocker lock(&m_lock);
    m_installErrors.insert(id, message.isEmpty() ? u"install failed"_s : message);
}

void FakeBackend::setUninstallFails(const QString &id, const QString &message)
{
    QMutexLocker lock(&m_lock);
    m_uninstallErrors.insert(id, message.isEmpty() ? u"uninstall failed"_s : message);
}

void FakeBackend::setUninstallWarning(const QString &id, const QString &message)
{
    QMutexLocker lock(&m_lock);
    m_uninstallWarnings.insert(id, message);
}

void FakeBackend::setInstallSteps(int steps)
{
    QMutexLocker lock(&m_lock);
    m_steps = std::max(1, steps);
}

void FakeBackend::setInstallDelayMs(int ms)
{
    QMutexLocker lock(&m_lock);
    m_delayMs = std::max(0, ms);
}

void FakeBackend::setSyncDelayMs(int ms)
{
    QMutexLocker lock(&m_lock);
    m_syncDelayMs = std::max(0, ms);
}

int FakeBackend::syncCount() const
{
    QMutexLocker lock(&m_lock);
    return m_syncCount;
}

QStringList FakeBackend::uninstalled() const
{
    QMutexLocker lock(&m_lock);
    return m_uninstalled;
}

QStringList FakeBackend::calls() const
{
    QMutexLocker lock(&m_lock);
    return m_calls;
}

bool FakeBackend::syncCatalog(QString *error)
{
    int delay = 0;
    bool fails = false;
    QString message;
    {
        QMutexLocker lock(&m_lock);
        m_syncCount++;
        m_calls << u"sync"_s;
        delay = m_syncDelayMs;
        fails = m_syncFails;
        message = m_syncError;
    }
    if (delay > 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    if (!fails)
        return true;
    if (error != nullptr)
        *error = message.isEmpty() ? u"offline"_s : message;
    return false;
}

QList<StoreEntry> FakeBackend::catalog(QString *error)
{
    QMutexLocker lock(&m_lock);
    if (m_catalogFails) {
        if (error != nullptr)
            *error = m_catalogError.isEmpty() ? u"no catalog"_s : m_catalogError;
        return {};
    }
    if (error != nullptr)
        error->clear();
    return m_entries;
}

QList<StoreEntry> FakeBackend::installed(QString *error)
{
    QMutexLocker lock(&m_lock);
    if (m_installedFails) {
        if (error != nullptr)
            *error = m_installedError.isEmpty() ? u"no database"_s : m_installedError;
        return {};
    }
    if (error != nullptr)
        error->clear();
    return m_installed;
}

bool FakeBackend::install(const QString &id, _GCancellable *cancellable, const Progress &progress, QString *error)
{
    m_cancelled.store(false);
    QString failure;
    int steps = 4;
    int delay = 2;
    {
        QMutexLocker lock(&m_lock);
        steps = m_steps;
        delay = m_delayMs;
        failure = m_installErrors.value(id);
        m_calls << u"install:"_s + id;
    }
    if (!failure.isEmpty()) {
        if (error != nullptr)
            *error = failure;
        return false;
    }
    for (int step = 0; step <= 100; step += 100 / steps) {
        if (stopped(&m_cancelled, cancellable)) {
            if (error != nullptr)
                *error = u"cancelled"_s;
            return false;
        }
        if (progress) {
            StoreProgress update;
            update.id = id;
            update.percent = step;
            update.message = u"Downloading %1"_s.arg(id);
            progress(update);
        }
        if (delay > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    }
    if (stopped(&m_cancelled, cancellable)) {
        if (error != nullptr)
            *error = u"cancelled"_s;
        return false;
    }
    if (progress) {
        StoreProgress update;
        update.id = id;
        update.percent = 100;
        update.message = u"Installing %1"_s.arg(id);
        update.installing = true;
        progress(update);
    }
    {
        QMutexLocker lock(&m_lock);
        StoreEntry entry;
        entry.id = id;
        entry.name = id;
        entry.installed = true;
        m_installed << entry;
    }
    return true;
}

bool FakeBackend::uninstall(const QString &id, _GCancellable *cancellable, const Progress &progress, QString *error)
{
    QString failure;
    QString warning;
    {
        QMutexLocker lock(&m_lock);
        failure = m_uninstallErrors.value(id);
        warning = m_uninstallWarnings.value(id);
        m_calls << u"uninstall:"_s + id;
    }
    if (!failure.isEmpty()) {
        if (error != nullptr)
            *error = failure;
        return false;
    }
    if (stopped(&m_cancelled, cancellable)) {
        if (error != nullptr)
            *error = u"cancelled"_s;
        return false;
    }
    QMutexLocker lock(&m_lock);
    for (int i = 0; i < m_installed.size(); i++) {
        if (m_installed.at(i).id != id)
            continue;
        m_installed.removeAt(i);
        break;
    }
    m_uninstalled << id;
    if (!warning.isEmpty() && progress) {
        StoreProgress update;
        update.id = id;
        update.message = warning;
        progress(update);
    }
    return true;
}

void FakeBackend::cancel()
{
    m_cancelled.store(true);
}
