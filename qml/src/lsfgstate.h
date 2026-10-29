#pragma once

#include <QFileSystemWatcher>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QTimer>

#include <functional>

class QProcess;

class LsfgState : public QObject
{
    Q_OBJECT

public:
    static constexpr qint64 MaxFile = 64 * 1024;
    static constexpr double SendGap = 20 * 60;

    explicit LsfgState(QObject *parent = nullptr);

    static QString stateFile();
    static QString sessionFile();
    static QString bootId();
    static double monotonic();
    static QString status(const QJsonObject &state, bool known, const QString &boot, double now);

    void watch();
    void setFixture(const QJsonObject &state, bool known, const QString &session);
    void setClock(const QString &boot, std::function<double()> now);
    void install();

    QJsonObject state() const;
    bool known() const;
    QString session() const;
    QString status() const;

signals:
    void changed();

private:
    void read();
    bool watchDirs();
    void expireSend();

    QJsonObject m_state;
    bool m_known = false;
    QString m_session;
    QString m_boot = bootId();
    std::function<double()> m_now = monotonic;
    QFileSystemWatcher m_watch;
    QTimer m_sent;
    QProcess *m_install = nullptr;
};
