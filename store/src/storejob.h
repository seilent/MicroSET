#pragma once

#include <QObject>
#include <QString>

class StoreJob : public QObject
{
    Q_OBJECT
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(Kind kind READ kind CONSTANT)
    Q_PROPERTY(QString id READ id CONSTANT)
    Q_PROPERTY(int percent READ percent NOTIFY percentChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)

public:
    enum State {
        Queued,
        Downloading,
        Installing,
        Done,
        Failed,
        Cancelled,
    };
    Q_ENUM(State)
    enum Kind {
        Install,
        Uninstall,
    };
    Q_ENUM(Kind)

    StoreJob(Kind kind, const QString &id, QObject *parent = nullptr);

    Kind kind() const;
    QString id() const;
    State state() const;
    int percent() const;
    QString message() const;
    bool running() const;

    void start();
    void progress(int percent);
    void beginInstall();
    void finish();
    void fail(const QString &message);
    void cancel();
    void setMessage(const QString &message);

signals:
    void stateChanged();
    void percentChanged();
    void messageChanged();

private:
    void setState(State state);
    void setPercent(int percent);

    Kind m_kind;
    QString m_id;
    State m_state = Queued;
    int m_percent = 0;
    QString m_message;
};
