#pragma once

#include <QDBusConnection>
#include <QDBusServiceWatcher>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

#include <array>

class DbusPad : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool holding READ holding NOTIFY holdingChanged)
    Q_PROPERTY(QString holder READ holder NOTIFY holderChanged)

public:
    enum Action {
        Up,
        Down,
        Left,
        Right,
        A,
        B,
        X,
        Y,
        LB,
        RB,
        LT,
        RT,
        Start,
        Back,
    };
    Q_ENUM(Action)

    static constexpr int ActionCount = Back + 1;
    static constexpr int RepeatInitialMs = 300;
    static constexpr int RepeatIntervalMs = 80;
    static constexpr double PressThreshold = 0.5;
    static constexpr int ReleaseRetryMs = 1000;

    DbusPad(const QDBusConnection &session, const QDBusConnection &system, QObject *parent = nullptr,
            bool wanted = true);

    static int actionFor(const QString &name);
    static QString arbiterService();
    static QString arbiterPath();
    static QString arbiterInterface();

    bool holding() const;
    QString holder() const;
    QString name() const;
    bool wanted() const;
    void setWanted(bool wanted);
    void acquire();
    Q_INVOKABLE QString actionName(DbusPad::Action action) const;

signals:
    void action(DbusPad::Action action, bool repeat);
    void pressed(DbusPad::Action action);
    void released(DbusPad::Action action);
    void holdingChanged();
    void holderChanged();
    void acquired(bool ok);
    void releaseDone();
    void arbiterLost();

private slots:
    void onInputEvent(const QString &name, double value);
    void onArbiterProperties(const QString &interface, const QVariantMap &changed, const QStringList &invalidated);

private:
    void sendRelease();
    void readHolder(bool afterAcquire = false);
    void setHolder(const QString &holder);
    void repeatTick();
    void startRepeat(Action direction);
    void stopRepeat();
    void press(Action a);
    void release(Action a);
    void releaseAll();

    QDBusConnection m_session;
    QDBusConnection m_system;
    QDBusServiceWatcher m_watcher;
    bool m_holding = false;
    bool m_wanted = true;
    QString m_holder;
    QTimer m_retry;
    QTimer m_repeat;
    int m_repeatAction = -1;
    std::array<bool, ActionCount> m_down{};
};
