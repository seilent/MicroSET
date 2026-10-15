#pragma once

#include <QDBusConnection>
#include <QDBusMessage>
#include <QProcess>
#include <QTemporaryDir>

class Harness
{
public:
    static inline const QByteArray RootCgroup = "0::/system.slice/sshd.service";
    static inline const QByteArray UserCgroup =
        "0::/user.slice/user-1000.slice/user@1000.service/app.slice/steamos-manager.service";
    static inline const QByteArray GameCgroup =
        "0::/user.slice/user-1000.slice/user@1000.service/app.slice/app-steam-app1145360-1.scope";
    static uint user();
    static QStringList wheel();
    static QStringList noWheel();

    Harness();
    ~Harness();

    QString path(const QString &relative) const;
    void writeFile(const QString &relative, const QByteArray &data) const;
    void writeSessions() const;
    void writeState(const QByteArray &values) const;
    QByteArray readState() const;

    bool startBus();
    QString address() const;
    QDBusConnection client() const;
    void writeCgroup(qint64 pid, const QByteArray &cgroup) const;
    QProcessEnvironment environment() const;

    bool startDaemon();
    void stopDaemon();
    QString daemonLog();

    QDBusMessage call(const QString &method, const QVariantList &arguments) const;
    int cli(const QStringList &arguments, const QStringList &as = {}, const QByteArray &cgroup = RootCgroup,
            QString *output = nullptr) const;
    void startCli(QProcess &process, const QStringList &arguments, const QStringList &as = {},
                  const QByteArray &cgroup = RootCgroup) const;

private:
    QTemporaryDir m_dir;
    QProcess m_bus;
    QProcess m_daemon;
    QByteArray m_log;
    QString m_address;
};
