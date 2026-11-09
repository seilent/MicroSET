#pragma once

#include <QDBusConnection>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

class AppModel;

namespace MicroSet {
class Client;
}

class FavoritesModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList favorites READ favorites NOTIFY favoritesChanged)
    Q_PROPERTY(QStringList ids READ ids NOTIFY idsChanged)

public:
    FavoritesModel(const QDBusConnection &bus, AppModel *apps, QObject *parent = nullptr);

    static QString key();

    QStringList ids() const;
    QVariantList favorites() const;
    bool starred(const QString &id) const;

    Q_INVOKABLE void toggle(const QString &id);
    Q_INVOKABLE void unstar(const QString &id);

    void setIdsForTest(const QStringList &ids);

signals:
    void idsChanged();
    void favoritesChanged();

private:
    void load();
    void settle(const QStringList &ids);
    void push();

    MicroSet::Client *m_client = nullptr;
    AppModel *m_apps = nullptr;
    QStringList m_ids;
    int m_inflight = 0;
};
