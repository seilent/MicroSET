#include "storecatalog.h"

#include <QFileInfo>
#include <QSet>
#include <QStringList>

#pragma push_macro("signals")
#undef signals
#include <appstream.h>
#pragma pop_macro("signals")

using namespace Qt::StringLiterals;

namespace {

QString text(const gchar *value)
{
    return value != nullptr ? QString::fromUtf8(value) : QString();
}

QString cachedIconFile(const QString &dir, const QString &name)
{
    const QStringList candidates{dir + u'/' + name, dir + u'/' + name + u".png"_s};
    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate))
            return candidate;
    }
    return {};
}

QString localIconFile(AsIcon *icon)
{
    const QString filename = text(as_icon_get_filename(icon));
    if (!filename.isEmpty() && QFileInfo::exists(filename))
        return filename;
    const QString name = text(as_icon_get_name(icon));
    if (name.isEmpty() || name.contains(u'/'))
        return {};
    return QFileInfo::exists(name) ? name : QString();
}

QString iconFile(AsComponent *component, const QString &dir, guint size)
{
    if (dir.isEmpty())
        return {};
    GPtrArray *icons = as_component_get_icons(component);
    if (icons == nullptr)
        return {};
    QString fallback;
    for (guint i = 0; i < icons->len; i++) {
        auto *icon = AS_ICON(g_ptr_array_index(icons, i));
        const AsIconKind kind = as_icon_get_kind(icon);
        if (kind == AS_ICON_KIND_LOCAL) {
            const QString file = localIconFile(icon);
            if (!file.isEmpty())
                return file;
            continue;
        }
        if (kind != AS_ICON_KIND_CACHED)
            continue;
        const QString name = text(as_icon_get_name(icon));
        if (name.isEmpty() || name.contains(u'/'))
            continue;
        const QString candidate = cachedIconFile(dir, name);
        if (candidate.isEmpty())
            continue;
        if (as_icon_get_width(icon) == size && as_icon_get_height(icon) == size)
            return candidate;
        if (fallback.isEmpty())
            fallback = candidate;
    }
    return fallback;
}

QString developerName(AsComponent *component)
{
    AsDeveloper *developer = as_component_get_developer(component);
    return developer != nullptr ? text(as_developer_get_name(developer)) : QString();
}

QString bundleRef(AsComponent *component)
{
    AsBundle *bundle = as_component_get_bundle(component, AS_BUNDLE_KIND_FLATPAK);
    return bundle != nullptr ? text(as_bundle_get_id(bundle)) : QString();
}

void setError(QString *out, const GError *error)
{
    if (out != nullptr)
        *out = error != nullptr ? QString::fromUtf8(error->message) : QString();
}

}

QList<StoreEntry> parseAppStreamCatalog(const QString &file, const QString &icons64, const QString &icons128,
                                        QString *error)
{
    QList<StoreEntry> entries;
    if (!QFileInfo::exists(file)) {
        if (error != nullptr)
            *error = u"no appstream cache at %1"_s.arg(file);
        return entries;
    }

    AsMetadata *metadata = as_metadata_new();
    as_metadata_set_format_style(metadata, AS_FORMAT_STYLE_CATALOG);
    GError *gerror = nullptr;
    GFile *source = g_file_new_for_path(file.toUtf8().constData());
    const gboolean parsed = as_metadata_parse_file(metadata, source, AS_FORMAT_KIND_XML, &gerror);
    g_object_unref(source);
    if (!parsed) {
        setError(error, gerror);
        g_clear_error(&gerror);
        g_object_unref(metadata);
        return entries;
    }

    QSet<QString> seen;
    AsComponentBox *box = as_metadata_get_components(metadata);
    for (guint i = 0; i < as_component_box_len(box); i++) {
        AsComponent *component = as_component_box_index(box, i);
        if (component == nullptr || as_component_get_kind(component) != AS_COMPONENT_KIND_DESKTOP_APP)
            continue;
        if (as_component_get_merge_kind(component) != AS_MERGE_KIND_NONE)
            continue;
        StoreEntry entry;
        entry.id = text(as_component_get_id(component));
        if (entry.id.isEmpty() || seen.contains(entry.id))
            continue;
        seen.insert(entry.id);
        entry.ref = bundleRef(component);
        entry.name = text(as_component_get_name(component));
        if (entry.name.isEmpty())
            entry.name = entry.id;
        entry.summary = text(as_component_get_summary(component));
        entry.developer = developerName(component);
        entry.icon = iconFile(component, icons64, 64);
        entry.iconLarge = iconFile(component, icons128, 128);
        entries << entry;
    }
    g_object_unref(metadata);
    return entries;
}
