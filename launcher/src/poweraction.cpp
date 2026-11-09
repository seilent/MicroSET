#include "poweraction.h"

#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDebug>

using namespace Qt::StringLiterals;

PowerAction::PowerAction(QObject *parent)
    : QObject(parent)
{
}

LogindPower::LogindPower(const QDBusConnection &bus, QObject *parent)
    : PowerAction(parent)
    , m_bus(bus)
{
}

QString LogindPower::service()
{
    return u"org.freedesktop.login1"_s;
}

QString LogindPower::path()
{
    return u"/org/freedesktop/login1"_s;
}

QString LogindPower::interface()
{
    return u"org.freedesktop.login1.Manager"_s;
}

void LogindPower::run(const QString &verb)
{
    QString method;
    if (verb == u"suspend"_s)
        method = u"Suspend"_s;
    else if (verb == u"reboot"_s)
        method = u"Reboot"_s;
    else if (verb == u"power off"_s)
        method = u"PowerOff"_s;
    if (method.isEmpty()) {
        qWarning("launcher: unknown power verb %s", qPrintable(verb));
        return;
    }
    if (!m_bus.isConnected()) {
        qWarning("launcher: no system bus, %s dropped", qPrintable(method));
        return;
    }
    QDBusMessage call = QDBusMessage::createMethodCall(service(), path(), interface(), method);
    call << false;
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call, 5000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [method](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        const QDBusPendingReply<> reply = *w;
        if (reply.isError())
            qWarning("launcher: login1 %s refused: %s", qPrintable(method), qPrintable(reply.error().message()));
    });
}
