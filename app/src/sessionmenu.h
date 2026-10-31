#pragma once

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusObjectPath>
#include <QList>
#include <QObject>
#include <QPair>
#include <QString>
#include <QVariantMap>

class Auth;

struct SessionMenuRow
{
    QString name;
    QString label;
    QVariantMap entry;
};
Q_DECLARE_METATYPE(SessionMenuRow)

QDBusArgument &operator<<(QDBusArgument &argument, const SessionMenuRow &row);
const QDBusArgument &operator>>(const QDBusArgument &argument, SessionMenuRow &row);

struct SessionMenuEntry
{
    QString name;
    QString label;
    QString state;
    bool install = false;
};
Q_DECLARE_METATYPE(SessionMenuEntry)
Q_DECLARE_METATYPE(QList<SessionMenuEntry>)

class SessionMenu : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "net.seilent.MicroOs.SessionMenu1")

public:
    static constexpr int MaxRows = 64;
    static constexpr int MaxLabel = 256;
    static constexpr int MaxRemembered = 8;

    SessionMenu(const Auth &auth, const QDBusConnection &session, const QDBusConnection &system,
                QObject *parent = nullptr);

    static QString service();
    static QString path();
    static QString interface();
    static void registerTypes();

    bool publish(QDBusConnection bus);
    void startSwitch(const QString &name);
    void startInstall(const QString &name);

signals:
    void requested(const QList<SessionMenuEntry> &entries, const QString &current, bool inGame, bool switching);
    void switchReplied(bool ok);
    void switchJobRemoved(const QString &result);
    void installReplied(bool ok);

public slots:
    Q_SCRIPTABLE void Show(const QList<SessionMenuRow> &sessions, const QString &current, const QVariantMap &hints);

private slots:
    void onJobRemoved(uint id, const QDBusObjectPath &job, const QString &unit, const QString &result);

private:
    const Auth &m_auth;
    QDBusConnection m_session;
    QDBusConnection m_system;
    QString m_job;
    QList<QPair<QString, QString>> m_removed;
    bool m_waiting = false;
};
