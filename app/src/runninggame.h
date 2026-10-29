#pragma once

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QList>
#include <QObject>
#include <QTimer>
#include <QVariantMap>

struct SystemdUnit
{
    QString name;
    QString description;
    QString load;
    QString active;
    QString sub;
    QString following;
    QDBusObjectPath path;
    uint job = 0;
    QString jobType;
    QDBusObjectPath jobPath;
};
Q_DECLARE_METATYPE(SystemdUnit)

struct UnitProcess
{
    QString cgroup;
    uint pid = 0;
    QString command;
};
Q_DECLARE_METATYPE(UnitProcess)

QDBusArgument &operator<<(QDBusArgument &argument, const SystemdUnit &unit);
const QDBusArgument &operator>>(const QDBusArgument &argument, SystemdUnit &unit);
QDBusArgument &operator<<(QDBusArgument &argument, const UnitProcess &process);
const QDBusArgument &operator>>(const QDBusArgument &argument, UnitProcess &process);

class RunningGame : public QObject
{
    Q_OBJECT

public:
    static constexpr int MaxUnits = 16;
    static constexpr int MaxProcesses = 256;
    static constexpr qint64 MaxEnviron = 256 * 1024;
    static constexpr int MaxSignals = 256;
    static constexpr qint64 StartWindowUs = 10 * 1000 * 1000;
    static inline const QString Slice = QStringLiteral("game.slice");

    RunningGame(const QDBusConnection &bus, const QString &procRoot, QObject *parent = nullptr);

    static void registerTypes();
    static QVariantMap readEnviron(const QString &procRoot, const QList<uint> &pids);
    static bool signalHeld(const QString &dir, const QString &id);
    static QString signalDir();

    QVariantMap info() const;
    void skip(const QStringList &appids);
    void setSignalDir(const QString &dir);
    void refresh();

signals:
    void changed();

private slots:
    void onRemoved(const QString &name, const QDBusObjectPath &path);
    void onProperties(const QString &interface, const QVariantMap &changed, const QStringList &invalidated);

private:
    void choose(const QList<SystemdUnit> &units, quint64 generation);
    void inspect(const QString &unit, const QString &appid, const QString &path, qulonglong since, quint64 generation);
    void watch(const QString &path);
    void markGone();
    void setInfo(const QVariantMap &info);

    QDBusConnection m_bus;
    QString m_proc;
    QStringList m_skip;
    QString m_signals = signalDir();
    QTimer m_later;
    QString m_path;
    QVariantMap m_info;
    quint64 m_generation = 0;
    bool m_subscribed = false;
};
