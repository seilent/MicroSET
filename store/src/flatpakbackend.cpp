#include "flatpakbackend.h"

#include "storecatalog.h"

#include <QMutexLocker>

#include <algorithm>

#pragma push_macro("signals")
#undef signals
#include <flatpak.h>
#pragma pop_macro("signals")

using namespace Qt::StringLiterals;

namespace {

QString text(const gchar *value)
{
    return value != nullptr ? QString::fromUtf8(value) : QString();
}

void takeError(QString *out, GError **error)
{
    if (*error == nullptr)
        return;
    if (out != nullptr)
        *out = QString::fromUtf8((*error)->message);
    g_clear_error(error);
}

struct TransactionState
{
    StoreBackend::Progress progress;
    QString id;
    QString remoteName;
    QString remoteUrl;
    qint64 totalSize = 0;
    qint64 doneSize = 0;
    qint64 currentSize = 0;
    int operationCount = 0;
    int operationsDone = 0;
    int lastPercent = 0;
    QString refusal;
};

void resetOperations(TransactionState *state)
{
    state->totalSize = 0;
    state->doneSize = 0;
    state->currentSize = 0;
    state->operationCount = 0;
    state->operationsDone = 0;
    state->lastPercent = 0;
}

int weightedPercent(const TransactionState *state, int operationPercent)
{
    if (state->totalSize <= 0)
        return operationPercent;
    const qint64 transferred = state->doneSize + state->currentSize * operationPercent / 100;
    return int(transferred * 100 / state->totalSize);
}

void emitProgress(TransactionState *state, int percent, const QString &message, bool installing)
{
    if (percent >= 0) {
        percent = std::clamp(percent, state->lastPercent, 100);
        state->lastPercent = percent;
    }
    if (!state->progress)
        return;
    StoreProgress update;
    update.id = state->id;
    update.percent = percent;
    update.message = message;
    update.installing = installing;
    state->progress(update);
}

void onProgressChanged(FlatpakTransactionProgress *progress, gpointer data)
{
    auto *state = static_cast<TransactionState *>(data);
    gchar *status = flatpak_transaction_progress_get_status(progress);
    emitProgress(state, weightedPercent(state, flatpak_transaction_progress_get_progress(progress)), text(status),
                 false);
    g_free(status);
}

void onNewOperation(FlatpakTransaction *, FlatpakTransactionOperation *operation, FlatpakTransactionProgress *progress,
                    gpointer data)
{
    auto *state = static_cast<TransactionState *>(data);
    const QString ref = text(flatpak_transaction_operation_get_ref(operation));
    state->currentSize = qint64(flatpak_transaction_operation_get_download_size(operation));
    if (state->operationCount == 0)
        state->totalSize += state->currentSize;
    emitProgress(state, weightedPercent(state, flatpak_transaction_progress_get_progress(progress)),
                 u"Downloading %1"_s.arg(ref), false);
    g_signal_connect(progress, "changed", G_CALLBACK(onProgressChanged), state);
}

void onOperationDone(FlatpakTransaction *, FlatpakTransactionOperation *operation, const char *,
                     FlatpakTransactionResult, gpointer data)
{
    auto *state = static_cast<TransactionState *>(data);
    const QString ref = text(flatpak_transaction_operation_get_ref(operation));
    state->doneSize += state->currentSize;
    state->currentSize = 0;
    state->operationsDone++;
    const bool last = state->operationsDone >= state->operationCount;
    const bool uninstall = flatpak_transaction_operation_get_operation_type(operation)
        == FLATPAK_TRANSACTION_OPERATION_UNINSTALL;
    const QString status = uninstall ? u"Removing %1"_s.arg(ref) : u"Installing %1"_s.arg(ref);
    int percent = -1;
    if (last)
        percent = 100;
    else if (state->totalSize > 0)
        percent = int(state->doneSize * 100 / state->totalSize);
    emitProgress(state, percent, status, last);
}

gboolean onOperationError(FlatpakTransaction *, FlatpakTransactionOperation *, const GError *,
                          FlatpakTransactionErrorDetails details, gpointer)
{
    return (details & FLATPAK_TRANSACTION_ERROR_DETAILS_NON_FATAL) != 0;
}

gboolean onReady(FlatpakTransaction *transaction, gpointer data)
{
    auto *state = static_cast<TransactionState *>(data);
    state->operationCount = 0;
    state->totalSize = 0;
    GList *operations = flatpak_transaction_get_operations(transaction);
    for (GList *item = operations; item != nullptr; item = item->next) {
        auto *operation = static_cast<FlatpakTransactionOperation *>(item->data);
        state->operationCount++;
        state->totalSize += qint64(flatpak_transaction_operation_get_download_size(operation));
    }
    g_list_free(operations);
    return TRUE;
}

gboolean onAddRemote(FlatpakTransaction *, FlatpakTransactionRemoteReason, const char *, const char *remote,
                     const char *url, gpointer data)
{
    auto *state = static_cast<TransactionState *>(data);
    if (state->remoteUrl.isEmpty())
        return FALSE;
    if (text(remote) != state->remoteName)
        return FALSE;
    return text(url) == state->remoteUrl ? TRUE : FALSE;
}

void onEndOfLifed(FlatpakTransaction *, const char *ref, const char *reason, const char *rebase, gpointer data)
{
    auto *state = static_cast<TransactionState *>(data);
    QString message = u"%1 is end of life: %2"_s.arg(text(ref), text(reason));
    if (text(rebase).isEmpty())
        message += u"; no rebase target"_s;
    else
        message += u"; rebase to %1"_s.arg(text(rebase));
    emitProgress(state, -1, message, false);
}

gboolean onEndOfLifedWithRebase(FlatpakTransaction *, const char *remote, const char *ref, const char *reason,
                                const char *rebasedToRef, const char **, gpointer data)
{
    auto *state = static_cast<TransactionState *>(data);
    const QString target = text(rebasedToRef);
    const QString message = u"%1 is end of life: %2"_s.arg(text(ref), text(reason));
    if (target.isEmpty() || text(remote) != state->remoteName) {
        state->refusal = target.isEmpty()
            ? u"%1 is end of life: %2 and has no rebase target"_s.arg(text(ref), text(reason))
            : u"%1 is end of life: %2 and cannot rebase to %3 from another remote"_s
                  .arg(text(ref), text(reason), target);
        emitProgress(state, -1, state->refusal, false);
        return FALSE;
    }
    emitProgress(state, -1, u"%1; rebase to %2"_s.arg(message, target), false);
    return TRUE;
}

void connectTransaction(FlatpakTransaction *transaction, TransactionState *state)
{
    resetOperations(state);
    g_signal_connect(transaction, "new-operation", G_CALLBACK(onNewOperation), state);
    g_signal_connect(transaction, "operation-done", G_CALLBACK(onOperationDone), state);
    g_signal_connect(transaction, "operation-error", G_CALLBACK(onOperationError), state);
    g_signal_connect(transaction, "ready", G_CALLBACK(onReady), state);
    g_signal_connect(transaction, "add-new-remote", G_CALLBACK(onAddRemote), state);
    g_signal_connect(transaction, "end-of-lifed", G_CALLBACK(onEndOfLifed), state);
    g_signal_connect(transaction, "end-of-lifed-with-rebase", G_CALLBACK(onEndOfLifedWithRebase), state);
}

bool runUninstalls(FlatpakInstallation *installation, const QStringList &refs, const QString &arch,
                   TransactionState *state, GCancellable *cancellable, QString *error)
{
    if (refs.isEmpty())
        return true;
    GError *gerror = nullptr;
    FlatpakTransaction *transaction = flatpak_transaction_new_for_installation(installation, cancellable, &gerror);
    if (transaction == nullptr) {
        takeError(error, &gerror);
        return false;
    }
    flatpak_transaction_set_no_interaction(transaction, TRUE);
    flatpak_transaction_set_default_arch(transaction, arch.toUtf8().constData());
    connectTransaction(transaction, state);
    bool ok = true;
    for (const QString &ref : refs) {
        if (flatpak_transaction_add_uninstall(transaction, ref.toUtf8().constData(), &gerror))
            continue;
        takeError(error, &gerror);
        ok = false;
        break;
    }
    if (ok)
        ok = flatpak_transaction_run(transaction, cancellable, &gerror);
    if (!ok && gerror != nullptr)
        takeError(error, &gerror);
    g_clear_error(&gerror);
    g_object_unref(transaction);
    return ok;
}

QString remoteUrlFor(FlatpakInstallation *installation, const QString &name)
{
    GError *gerror = nullptr;
    FlatpakRemote *remote =
        flatpak_installation_get_remote_by_name(installation, name.toUtf8().constData(), nullptr, &gerror);
    if (remote == nullptr) {
        g_clear_error(&gerror);
        return {};
    }
    gchar *url = flatpak_remote_get_url(remote);
    const QString result = text(url);
    g_free(url);
    g_object_unref(remote);
    return result;
}

}

FlatpakBackend::FlatpakBackend(const QString &remote, const QString &arch)
    : m_remote(remote)
    , m_arch(arch)
{
}

FlatpakBackend::~FlatpakBackend()
{
    if (m_installation != nullptr) {
        g_object_unref(m_installation);
        m_installation = nullptr;
    }
}

FlatpakInstallation *FlatpakBackend::installation(QString *error)
{
    if (m_installation != nullptr)
        return m_installation;
    GError *gerror = nullptr;
    FlatpakInstallation *installation = flatpak_installation_new_user(nullptr, &gerror);
    if (installation == nullptr) {
        takeError(error, &gerror);
        return nullptr;
    }
    m_installation = installation;
    return installation;
}

void FlatpakBackend::beginOperation(_GCancellable *cancellable)
{
    QMutexLocker lock(&m_lock);
    m_active = cancellable;
}

void FlatpakBackend::endOperation(_GCancellable *cancellable)
{
    QMutexLocker lock(&m_lock);
    if (m_active == cancellable)
        m_active = nullptr;
}

void FlatpakBackend::cancel()
{
    QMutexLocker lock(&m_lock);
    if (m_active != nullptr)
        g_cancellable_cancel(m_active);
}

QString FlatpakBackend::refFor(const QString &id) const
{
    const QString ref = m_refs.value(id);
    return ref.isEmpty() ? u"app/%1/%2/stable"_s.arg(id, m_arch) : ref;
}

bool FlatpakBackend::syncCatalog(QString *error)
{
    FlatpakInstallation *installation = this->installation(error);
    if (installation == nullptr)
        return false;
    GCancellable *cancellable = g_cancellable_new();
    beginOperation(cancellable);
    GError *gerror = nullptr;
    gboolean changed = FALSE;
    const bool ok = flatpak_installation_update_appstream_full_sync(
        installation, m_remote.toUtf8().constData(), m_arch.toUtf8().constData(), nullptr, nullptr, &changed,
        cancellable, &gerror);
    endOperation(cancellable);
    g_object_unref(cancellable);
    if (!ok)
        takeError(error, &gerror);
    return ok;
}

QList<StoreEntry> FlatpakBackend::catalog(QString *error)
{
    QList<StoreEntry> entries;
    FlatpakInstallation *installation = this->installation(error);
    if (installation == nullptr)
        return entries;
    GError *gerror = nullptr;
    FlatpakRemote *remote =
        flatpak_installation_get_remote_by_name(installation, m_remote.toUtf8().constData(), nullptr, &gerror);
    if (remote == nullptr) {
        takeError(error, &gerror);
        return entries;
    }
    GFile *dir = flatpak_remote_get_appstream_dir(remote, m_arch.toUtf8().constData());
    g_object_unref(remote);
    gchar *path = dir != nullptr ? g_file_get_path(dir) : nullptr;
    const QString base = text(path);
    g_free(path);
    g_clear_object(&dir);
    if (base.isEmpty()) {
        if (error != nullptr)
            *error = u"no appstream cache for %1"_s.arg(m_remote);
        return entries;
    }
    entries = parseAppStreamCatalog(base + u"/appstream.xml.gz"_s, base + u"/icons/64x64"_s,
                                    base + u"/icons/128x128"_s, error);
    for (const StoreEntry &entry : entries)
        m_refs.insert(entry.id, entry.ref);
    return entries;
}

QStringList FlatpakBackend::installedRefs(FlatpakInstallation *installation, const QString &id, QString *error)
{
    QStringList refs;
    GError *gerror = nullptr;
    GPtrArray *installed =
        flatpak_installation_list_installed_refs_by_kind(installation, FLATPAK_REF_KIND_APP, nullptr, &gerror);
    if (installed == nullptr) {
        takeError(error, &gerror);
        return refs;
    }
    for (guint i = 0; i < installed->len; i++) {
        auto *ref = FLATPAK_REF(g_ptr_array_index(installed, i));
        if (id != text(flatpak_ref_get_name(ref)))
            continue;
        if (m_arch != text(flatpak_ref_get_arch(ref)))
            continue;
        gchar *formatted = flatpak_ref_format_ref(ref);
        refs << text(formatted);
        g_free(formatted);
    }
    g_ptr_array_unref(installed);
    if (refs.isEmpty() && error != nullptr)
        *error = u"%1 is not installed"_s.arg(id);
    return refs;
}

QList<StoreEntry> FlatpakBackend::installed(QString *error)
{
    QList<StoreEntry> entries;
    FlatpakInstallation *installation = this->installation(error);
    if (installation == nullptr)
        return entries;
    GError *gerror = nullptr;
    GPtrArray *refs =
        flatpak_installation_list_installed_refs_by_kind(installation, FLATPAK_REF_KIND_APP, nullptr, &gerror);
    if (refs == nullptr) {
        takeError(error, &gerror);
        return entries;
    }
    for (guint i = 0; i < refs->len; i++) {
        auto *ref = FLATPAK_INSTALLED_REF(g_ptr_array_index(refs, i));
        StoreEntry entry;
        entry.id = text(flatpak_ref_get_name(FLATPAK_REF(ref)));
        gchar *formatted = flatpak_ref_format_ref(FLATPAK_REF(ref));
        entry.ref = text(formatted);
        g_free(formatted);
        entry.name = text(flatpak_installed_ref_get_appdata_name(ref));
        if (entry.name.isEmpty())
            entry.name = entry.id;
        entry.summary = text(flatpak_installed_ref_get_appdata_summary(ref));
        entry.installedSize = qint64(flatpak_installed_ref_get_installed_size(ref));
        entry.installed = true;
        entries << entry;
    }
    g_ptr_array_unref(refs);
    return entries;
}

bool FlatpakBackend::install(const QString &id, _GCancellable *cancellable, const Progress &progress, QString *error)
{
    FlatpakInstallation *installation = this->installation(error);
    if (installation == nullptr)
        return false;
    if (cancellable != nullptr && g_cancellable_is_cancelled(cancellable)) {
        if (error != nullptr)
            *error = u"cancelled"_s;
        return false;
    }
    if (!installedRefs(installation, id, nullptr).isEmpty()) {
        if (progress) {
            StoreProgress update;
            update.id = id;
            update.message = u"%1 is already installed"_s.arg(id);
            progress(update);
        }
        return true;
    }
    const QString ref = refFor(id);
    beginOperation(cancellable);
    TransactionState state{progress, id};
    state.remoteName = m_remote;
    state.remoteUrl = remoteUrlFor(installation, m_remote);
    GError *gerror = nullptr;
    FlatpakTransaction *transaction = flatpak_transaction_new_for_installation(installation, cancellable, &gerror);
    bool ok = false;
    if (transaction == nullptr) {
        takeError(error, &gerror);
    } else {
        flatpak_transaction_set_no_interaction(transaction, TRUE);
        flatpak_transaction_set_disable_prune(transaction, TRUE);
        flatpak_transaction_set_default_arch(transaction, m_arch.toUtf8().constData());
        connectTransaction(transaction, &state);
        if (!flatpak_transaction_add_install(transaction, m_remote.toUtf8().constData(), ref.toUtf8().constData(),
                                             nullptr, &gerror)) {
            takeError(error, &gerror);
        } else {
            ok = flatpak_transaction_run(transaction, cancellable, &gerror);
            if (!ok)
                takeError(error, &gerror);
        }
        g_object_unref(transaction);
    }
    if (!ok && !state.refusal.isEmpty() && error != nullptr)
        *error = state.refusal;
    endOperation(cancellable);
    return ok;
}

bool FlatpakBackend::uninstall(const QString &id, _GCancellable *cancellable, const Progress &progress,
                               QString *error)
{
    FlatpakInstallation *installation = this->installation(error);
    if (installation == nullptr)
        return false;
    if (cancellable != nullptr && g_cancellable_is_cancelled(cancellable)) {
        if (error != nullptr)
            *error = u"cancelled"_s;
        return false;
    }
    const QStringList refs = installedRefs(installation, id, error);
    if (refs.isEmpty())
        return false;
    beginOperation(cancellable);
    TransactionState state{progress, id};
    state.remoteName = m_remote;
    const bool ok = runUninstalls(installation, refs, m_arch, &state, cancellable, error);
    if (ok) {
        GError *gerror = nullptr;
        GPtrArray *unused =
            flatpak_installation_list_unused_refs(installation, m_arch.toUtf8().constData(), cancellable, &gerror);
        if (unused == nullptr) {
            QString warning;
            takeError(&warning, &gerror);
            emitProgress(&state, -1, u"Runtime cleanup failed: %1"_s.arg(warning), false);
        } else {
            QStringList runtimeRefs;
            for (guint i = 0; i < unused->len; i++) {
                gchar *formatted = flatpak_ref_format_ref(FLATPAK_REF(g_ptr_array_index(unused, i)));
                runtimeRefs << text(formatted);
                g_free(formatted);
            }
            g_ptr_array_unref(unused);
            QString cleanupError;
            if (!runUninstalls(installation, runtimeRefs, m_arch, &state, cancellable, &cleanupError))
                emitProgress(&state, -1, u"Runtime cleanup failed: %1"_s.arg(cleanupError), false);
        }
    }
    endOperation(cancellable);
    return ok;
}
