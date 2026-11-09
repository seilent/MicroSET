#include "appmodel.h"

#include "activator.h"
#include "glibthread.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <QProcessEnvironment>
#include <QPixmap>
#include <QQuickImageProvider>
#include <QVariantMap>

#pragma push_macro("signals")
#undef signals
#include <gio/gdesktopappinfo.h>
#include <gio/gio.h>
#pragma pop_macro("signals")

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

constexpr int IconPx = 128;

QString fromUtf8(const char *text)
{
    return text != nullptr ? QString::fromUtf8(text) : QString();
}

QVariantMap describe(const AppEntry &entry)
{
    return QVariantMap{
        {u"id"_s, entry.id},
        {u"name"_s, entry.name},
        {u"source"_s, AppModel::iconSource(entry.id)},
        {u"startupClass"_s, entry.startupClass},
    };
}

void onMonitorChanged(GAppInfoMonitor *, gpointer data)
{
    QMetaObject::invokeMethod(static_cast<AppModel *>(data), &AppModel::refresh, Qt::QueuedConnection);
}

struct LaunchRequest
{
    AppModel *model = nullptr;
    Activator *activator = nullptr;
    QString appId;
};

void onLaunched(GDesktopAppInfo *, GPid pid, gpointer data)
{
    auto *request = static_cast<LaunchRequest *>(data);
    const qint64 child = static_cast<qint64>(pid);
    request->model->reportLaunched(request->appId, child);
    if (request->activator != nullptr)
        request->activator->reportStarted(request->appId, child);
}

}

AppIconProvider::AppIconProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QImage AppIconProvider::requestImage(const QString &id, QSize *size, const QSize &requested)
{
    Q_UNUSED(requested)
    const QImage image = AppIcons::image(id);
    if (size != nullptr)
        *size = image.size();
    return image;
}

QMutex AppIcons::s_lock;
QHash<QString, IconData> AppIcons::s_icons;
AppModel *AppIcons::s_model = nullptr;

void AppIcons::attach(AppModel *model)
{
    QMutexLocker lock(&s_lock);
    s_model = model;
}

void AppIcons::detach(AppModel *model)
{
    QMutexLocker lock(&s_lock);
    if (s_model != model)
        return;
    s_model = nullptr;
    s_icons.clear();
}

void AppIcons::publish(const QHash<QString, IconData> &icons)
{
    QMutexLocker lock(&s_lock);
    s_icons = icons;
}

bool AppIcons::known(const QString &id)
{
    QMutexLocker lock(&s_lock);
    return s_icons.contains(id);
}

QImage AppIcons::image(const QString &id)
{
    IconData data;
    {
        QMutexLocker lock(&s_lock);
        const auto it = s_icons.constFind(id);
        if (it == s_icons.constEnd())
            return {};
        data = it.value();
    }
    if (!data.file.isEmpty()) {
        QImageReader reader(data.file);
        reader.setAutoTransform(true);
        const QImage image = reader.read();
        if (!image.isNull()) {
            QImage canvas(IconPx, IconPx, QImage::Format_ARGB32_Premultiplied);
            canvas.fill(Qt::transparent);
            QPainter painter(&canvas);
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            const QImage scaled = image.scaled(IconPx, IconPx, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            painter.drawImage((IconPx - scaled.width()) / 2, (IconPx - scaled.height()) / 2, scaled);
            return canvas;
        }
    }
    QIcon icon;
    for (const QString &name : data.names) {
        if (!QIcon::hasThemeIcon(name))
            continue;
        icon = QIcon::fromTheme(name);
        if (!icon.isNull())
            break;
    }
    if (icon.isNull())
        return {};
    const QPixmap pixmap = icon.pixmap(IconPx, IconPx);
    return pixmap.isNull() ? QImage() : pixmap.toImage();
}

AppModel::AppModel(QObject *parent)
    : QObject(parent)
{
    m_collator.setCaseSensitivity(Qt::CaseInsensitive);
    m_collator.setNumericMode(true);
}

AppModel::~AppModel()
{
    stopMonitor();
    AppIcons::detach(this);
}

void AppModel::shutdown()
{
    stopMonitor();
}

QString AppModel::steamPrefix()
{
    return u"steam steam://"_s;
}

QString AppModel::steamLoaderId()
{
    return u"steam_http_loader.desktop"_s;
}

bool AppModel::hidden(const QString &id, const QString &command)
{
    if (id == steamLoaderId())
        return true;
    return command.startsWith(steamPrefix());
}

QString AppModel::iconSource(const QString &id)
{
    return AppIcons::known(id) ? u"image://microset-appicon/"_s + id : QString();
}

QVariantList AppModel::entries() const
{
    return m_variants;
}

int AppModel::count() const
{
    return int(m_entries.size());
}

const AppEntry *AppModel::find(const QString &id) const
{
    for (const AppEntry &entry : m_entries) {
        if (entry.id == id)
            return &entry;
    }
    return nullptr;
}

QString AppModel::nameOf(const QString &id) const
{
    const AppEntry *entry = find(id);
    return entry != nullptr ? entry->name : QString();
}

QString AppModel::classOf(const QString &class_) const
{
    for (const AppEntry &entry : m_entries) {
        if (!entry.startupClass.isEmpty() && entry.startupClass == class_)
            return entry.id;
    }
    for (const AppEntry &entry : m_entries) {
        QString plain = entry.id;
        if (plain.endsWith(u".desktop"_s))
            plain.chop(8);
        if (plain == class_)
            return entry.id;
    }
    return {};
}

void AppModel::startMonitor(GlibThread *glib)
{
    if (m_monitor != nullptr)
        return;
    m_glib = glib;
    if (m_glib == nullptr) {
        createMonitor();
        return;
    }
    m_glib->runBlocking([this] { createMonitor(); });
}

void AppModel::createMonitor()
{
    GAppInfoMonitor *monitor = g_app_info_monitor_get();
    m_monitor = monitor;
    g_signal_connect(monitor, "changed", G_CALLBACK(onMonitorChanged), this);
}

void AppModel::stopMonitor()
{
    if (m_monitor == nullptr)
        return;
    auto *monitor = static_cast<GAppInfoMonitor *>(m_monitor);
    m_monitor = nullptr;
    const auto drop = [this, monitor] {
        g_signal_handlers_disconnect_by_data(monitor, this);
        g_object_unref(monitor);
    };
    if (m_glib != nullptr)
        m_glib->runBlocking(drop);
    else
        drop();
}

void AppModel::refresh()
{
    QList<AppEntry> found;
    GList *all = g_app_info_get_all();
    for (GList *node = all; node != nullptr; node = node->next) {
        GAppInfo *info = G_APP_INFO(node->data);
        if (!G_IS_DESKTOP_APP_INFO(info) || !g_app_info_should_show(info))
            continue;
        auto *desktop = G_DESKTOP_APP_INFO(info);
        AppEntry entry;
        entry.id = fromUtf8(g_app_info_get_id(info));
        entry.name = fromUtf8(g_app_info_get_display_name(info));
        entry.command = fromUtf8(g_app_info_get_commandline(info));
        entry.startupClass = fromUtf8(g_desktop_app_info_get_startup_wm_class(desktop));
        GIcon *gicon = g_app_info_get_icon(info);
        if (gicon != nullptr && G_IS_FILE_ICON(gicon)) {
            GFile *file = g_file_icon_get_file(G_FILE_ICON(gicon));
            gchar *path = g_file_get_path(file);
            entry.iconFile = fromUtf8(path);
            g_free(path);
        } else if (gicon != nullptr && G_IS_THEMED_ICON(gicon)) {
            const gchar *const *names = g_themed_icon_get_names(G_THEMED_ICON(gicon));
            for (qsizetype i = 0; names != nullptr && names[i] != nullptr; ++i)
                entry.iconNames << QString::fromUtf8(names[i]);
        }
        if (entry.id.isEmpty() || entry.name.isEmpty())
            continue;
        if (hidden(entry.id, entry.command))
            continue;
        entry.sortKey = entry.name;
        found << entry;
    }
    g_list_free_full(all, g_object_unref);
    setEntries(found);
}

void AppModel::setEntries(const QList<AppEntry> &entries)
{
    QList<AppEntry> sorted = entries;
    const QCollator collator = m_collator;
    std::stable_sort(sorted.begin(), sorted.end(), [&collator](const AppEntry &a, const AppEntry &b) {
        const int order = collator.compare(a.sortKey, b.sortKey);
        return order == 0 ? a.id < b.id : order < 0;
    });
    m_entries = sorted;
    m_icons.clear();
    m_variants.clear();
    for (const AppEntry &entry : std::as_const(m_entries))
        if (!entry.iconFile.isEmpty() || !entry.iconNames.isEmpty())
            m_icons.insert(entry.id, IconData{entry.iconFile, entry.iconNames});
    AppIcons::publish(m_icons);
    for (const AppEntry &entry : std::as_const(m_entries))
        m_variants << describe(entry);
    emit entriesChanged();
}

void AppModel::setEntriesForTest(const QVariantList &entries)
{
    QList<AppEntry> rows;
    for (const QVariant &row : entries) {
        const QVariantMap map = row.toMap();
        AppEntry entry;
        entry.id = map.value(u"id"_s).toString();
        entry.name = map.value(u"name"_s).toString();
        entry.sortKey = entry.name;
        entry.iconFile = map.value(u"iconFile"_s).toString();
        rows << entry;
    }
    setEntries(rows);
}

void AppModel::launch(const QString &id, Activator *activator)
{
    const AppEntry *entry = find(id);
    if (entry == nullptr) {
        if (activator != nullptr)
            activator->reportFailed(id, u"no entry"_s);
        return;
    }
    const QByteArray desktopId = entry->id.toUtf8();
    GDesktopAppInfo *info = g_desktop_app_info_new(desktopId.constData());
    if (info == nullptr) {
        if (activator != nullptr)
            activator->reportFailed(id, u"cannot read "_s + entry->id);
        return;
    }
    GAppLaunchContext *launch = g_app_launch_context_new();
    QMap<QString, QString> environment = activator != nullptr ? activator->environment() : QMap<QString, QString>();
    if (environment.isEmpty()) {
        const QProcessEnvironment own = QProcessEnvironment::systemEnvironment();
        for (const QString &name : own.keys())
            environment.insert(name, own.value(name));
    }
    gchar **own = g_get_environ();
    for (qsizetype i = 0; own != nullptr && own[i] != nullptr; ++i) {
        const QByteArray name(own[i]);
        const qsizetype at = name.indexOf('=');
        if (at <= 0)
            continue;
        if (!environment.contains(QString::fromUtf8(name.left(at))))
            g_app_launch_context_unsetenv(launch, name.left(at).constData());
    }
    g_strfreev(own);
    for (auto it = environment.cbegin(); it != environment.cend(); ++it)
        g_app_launch_context_setenv(launch, it.key().toUtf8().constData(), it.value().toUtf8().constData());
    LaunchRequest request{this, activator, id};
    GError *error = nullptr;
    const gboolean ok = g_desktop_app_info_launch_uris_as_manager(info, nullptr, launch, G_SPAWN_SEARCH_PATH, nullptr,
                                                                 nullptr, onLaunched, &request, &error);
    if (!ok) {
        const QString message = error != nullptr ? fromUtf8(error->message) : u"launch failed"_s;
        if (error != nullptr)
            g_error_free(error);
        if (activator != nullptr)
            activator->reportFailed(id, message);
    }
    g_object_unref(launch);
    g_object_unref(info);
}

void AppModel::reportLaunched(const QString &appId, qint64 pid)
{
    emit launched(appId, pid);
}
