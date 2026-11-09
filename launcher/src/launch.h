#pragma once

#include "activator.h"
#include "appmodel.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusVariant>
#include <QList>
#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>

struct UnitProperty
{
    QString name;
    QDBusVariant value;
};
Q_DECLARE_METATYPE(UnitProperty)
Q_DECLARE_METATYPE(QList<UnitProperty>)

QDBusArgument &operator<<(QDBusArgument &argument, const UnitProperty &property);
const QDBusArgument &operator>>(const QDBusArgument &argument, UnitProperty &property);

struct UnitAux
{
    QString name;
    QList<UnitProperty> value;
};
Q_DECLARE_METATYPE(UnitAux)
Q_DECLARE_METATYPE(QList<UnitAux>)

QDBusArgument &operator<<(QDBusArgument &argument, const UnitAux &aux);
const QDBusArgument &operator>>(const QDBusArgument &argument, UnitAux &aux);

class ManagerEnv : public QObject
{
    Q_OBJECT

public:
    static QString service();
    static QString path();
    static QString managerInterface();
    static QString propertiesInterface();

    explicit ManagerEnv(const QDBusConnection &bus, QObject *parent = nullptr);

    QMap<QString, QString> environment() const;
    bool stale() const;
    void read();
    void watch();
    void subscribe();
    static QMap<QString, QString> parse(const QStringList &entries);

public slots:
    void refresh();

signals:
    void readDone();

private:
    QDBusConnection m_bus;
    QMap<QString, QString> m_values;
    quint64 m_signals = 0;
    quint64 m_reads = 0;
    bool m_watching = false;
};

class LauncherLaunch : public Activator
{
    Q_OBJECT

public:
    static constexpr int MaxIdLength = 64;

    LauncherLaunch(const QDBusConnection &bus, AppModel *apps, QObject *parent = nullptr);

    static QString scopeName(const QString &appId, qint64 pid);
    static QString escapeId(const QString &appId);
    static QStringList requiredNames();

    void setEnvironment(const QMap<QString, QString> &values);
    void setManager(ManagerEnv *manager);
    QMap<QString, QString> environment() const override;

    void start(const QString &id) override;
    void reportStarted(const QString &appId, qint64 pid) override;
    void reportFailed(const QString &appId, const QString &message) override;

private:
    void moveToScope(const QString &appId, qint64 pid);

    QDBusConnection m_bus;
    AppModel *m_apps;
    ManagerEnv *m_manager = nullptr;
    QMap<QString, QString> m_environment;
};
