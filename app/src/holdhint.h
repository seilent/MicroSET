#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QString>

class HoldHint : public QObject
{
    Q_OBJECT

public:
    static inline const QString Script = QStringLiteral("/usr/libexec/microos/microos-storage");

    HoldHint(const QString &dir, const QString &procRoot, QObject *parent = nullptr);

    bool active() const;
    QString device() const;
    void check();

signals:
    void activeChanged();

private:
    bool formatRunning(qint64 pid) const;

    QFileSystemWatcher m_watcher;
    QString m_file;
    QString m_proc;
    QString m_device;
    bool m_active = false;
};
