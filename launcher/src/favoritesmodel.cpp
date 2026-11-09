#include "favoritesmodel.h"

#include "appmodel.h"

#include <microset/client.h>

using namespace Qt::StringLiterals;

FavoritesModel::FavoritesModel(const QDBusConnection &bus, AppModel *apps, QObject *parent)
    : QObject(parent)
    , m_client(new MicroSet::Client(bus, this))
    , m_apps(apps)
{
    connect(m_client, &MicroSet::Client::changed, this, [this](const QString &changed, const QVariant &value) {
        if (changed != key() || m_inflight > 0)
            return;
        settle(value.toStringList());
    });
    m_client->watch(key());
    connect(m_apps, &AppModel::entriesChanged, this, &FavoritesModel::favoritesChanged);
    load();
}

QString FavoritesModel::key()
{
    return u"launcher.favorites"_s;
}

QStringList FavoritesModel::ids() const
{
    return m_ids;
}

QVariantList FavoritesModel::favorites() const
{
    return m_apps != nullptr ? m_apps->entriesFor(m_ids) : QVariantList();
}

bool FavoritesModel::starred(const QString &id) const
{
    return m_ids.contains(id);
}

void FavoritesModel::toggle(const QString &id)
{
    if (id.isEmpty())
        return;
    QStringList ids = m_ids;
    if (!ids.removeOne(id))
        ids << id;
    settle(ids);
    push();
}

void FavoritesModel::unstar(const QString &id)
{
    if (!m_ids.contains(id))
        return;
    QStringList ids = m_ids;
    ids.removeAll(id);
    settle(ids);
    push();
}

void FavoritesModel::load()
{
    MicroSet::Error error;
    const QVariant value = m_client->get(key(), &error);
    if (!error.ok()) {
        qWarning("launcher: cannot read %s: %s", qPrintable(key()), qPrintable(error.message));
        return;
    }
    settle(value.toStringList());
}

void FavoritesModel::settle(const QStringList &ids)
{
    if (ids == m_ids)
        return;
    m_ids = ids;
    emit idsChanged();
    emit favoritesChanged();
}

void FavoritesModel::push()
{
    const QStringList ids = m_ids;
    ++m_inflight;
    m_client->set(key(), ids, [this](const MicroSet::Error &error) {
        --m_inflight;
        if (error.ok())
            return;
        qWarning("launcher: cannot store %s: %s", qPrintable(key()), qPrintable(error.message));
        load();
    });
}
