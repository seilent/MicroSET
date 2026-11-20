#pragma once

#include <QDBusConnection>
#include <QDBusServiceWatcher>
#include <QObject>
#include <QString>

class QuickAccess : public QObject
{
    Q_OBJECT

public:
    static inline const QString Service = QStringLiteral("net.seilent.MicroOS.PowerKey");
    static inline const QString Path = QStringLiteral("/net/seilent/MicroOS/PowerKey");
    static inline const QString Interface = QStringLiteral("net.seilent.MicroOS.PowerKey1");
    static constexpr int CallMs = 2000;

    QuickAccess(const QDBusConnection &bus, const QString &session, QObject *parent = nullptr);

    bool available() const;
    bool owned() const;
    QString session() const;
    void open();

signals:
    void ownedChanged();
    void opened();
    void failed();

private:
    void setOwned(bool owned);

    QDBusConnection m_bus;
    QDBusServiceWatcher m_watcher;
    QString m_session;
    bool m_owned = false;
    bool m_known = false;
    bool m_calling = false;
};
