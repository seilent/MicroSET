#pragma once

#include <QDBusConnection>
#include <QDBusServiceWatcher>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

class AndroidStatus : public QObject
{
    Q_OBJECT

public:
    static inline const QString Service = QStringLiteral("net.seilent.MicroOs.Android1");
    static inline const QString Path = QStringLiteral("/net/seilent/MicroOs/Android");
    static inline const QString Interface = QStringLiteral("net.seilent.MicroOs.Android1");
    static inline const QString GooglePlayUnit = QStringLiteral("microos-android-gapps.service");
    static constexpr int CallMs = 2000;

    explicit AndroidStatus(const QDBusConnection &bus, QObject *parent = nullptr);

    QVariantMap status() const;
    void installGooglePlay();

signals:
    void changed();
    void failed();

private slots:
    void onPropertiesChanged(const QString &interface, const QVariantMap &changed, const QStringList &invalidated);

private:
    void fetch();
    void setStatus(const QVariantMap &status);

    QDBusConnection m_bus;
    QDBusServiceWatcher m_watcher;
    QVariantMap m_status;
    QVariantMap m_since;
    quint64 m_ask = 0;
    bool m_starting = false;
};
