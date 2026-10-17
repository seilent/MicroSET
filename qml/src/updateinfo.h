#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QString>

class QProcess;

class UpdateInfo : public QObject
{
    Q_OBJECT

public:
    explicit UpdateInfo(QObject *parent = nullptr);

    static QString osReleasePath();
    static QString updateFile();
    static QString readKey(const QString &path, const QString &key);
    static QString checkStatus(const QString &show);
    static QString text(const QString &status, const QString &version);

    void watch();
    void reload();
    void setFixture(const QString &imageVersion, const QString &status, const QString &version);

    QString imageVersion() const;
    QString status() const;
    QString version() const;

signals:
    void changed();

private:
    bool readUpdateFile();
    void watchDir();
    void setUpdate(const QString &status, const QString &version);

    QString m_imageVersion;
    QString m_status = QStringLiteral("unchecked");
    QString m_version;
    QString m_state;
    QString m_checked;
    QFileSystemWatcher m_watch;
    QProcess *m_check = nullptr;
};
