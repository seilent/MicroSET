#pragma once

#include "homeclient.h"

#include <QDBusConnection>
#include <QDBusServiceWatcher>
#include <QObject>
#include <QString>
#include <QVariantList>

class HomeClient : public DesktopHome
{
    Q_OBJECT

public:
    explicit HomeClient(const QDBusConnection &bus, QObject *parent = nullptr);

    void enter() override;
    void toggle() override;
    void leave() override;
    void focusWindow(const QString &address) override;

private slots:
    void onProperties(const QString &interface, const QVariantMap &changed, const QStringList &invalidated);
    void onOwner(const QString &service, const QString &oldOwner, const QString &newOwner);

private:
    void read();
    void subscribe();
    void call(const QString &method, const QVariantList &arguments);

    QDBusConnection m_bus;
    QDBusServiceWatcher m_watcher;
    bool m_subscribed = false;
};
