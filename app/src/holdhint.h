#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QString>

class HoldHint : public QObject
{
    Q_OBJECT

public:
    static inline const QString Script = QStringLiteral("/usr/libexec/microos/microos-storage");
    static inline const QString Powerd = QStringLiteral("/usr/libexec/microos/microos-powerd");

    HoldHint(const QString &dir, const QString &procRoot, QObject *parent = nullptr);

    bool active() const;
    QString device() const;
    QString kind() const;
    void check();

signals:
    void activeChanged();

private:
    QList<QByteArray> lines(const QString &path);
    bool running(const QList<QByteArray> &hint, const QString &script, const QByteArray &verb) const;

    QFileSystemWatcher m_watcher;
    QString m_file;
    QString m_undervolt;
    QString m_proc;
    QString m_device;
    QString m_kind;
    bool m_active = false;
};
