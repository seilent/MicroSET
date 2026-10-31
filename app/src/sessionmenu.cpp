#include "sessionmenu.h"

#include "auth.h"

#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QPointer>
#include <QRegularExpression>

#include <utility>

using namespace Qt::StringLiterals;

namespace {

const QString SystemdService = u"org.freedesktop.systemd1"_s;
const QString SystemdPath = u"/org/freedesktop/systemd1"_s;
const QString SystemdManager = u"org.freedesktop.systemd1.Manager"_s;

QString validate(const QList<SessionMenuRow> &sessions, const QString &current)
{
    static const QRegularExpression name(QRegularExpression::anchoredPattern(u"[a-z0-9][a-z0-9-]*"_s));
    if (sessions.size() > SessionMenu::MaxRows)
        return u"too many sessions"_s;
    for (const SessionMenuRow &row : sessions) {
        if (!name.match(row.name).hasMatch())
            return u"bad session name: "_s + row.name;
        if (row.label.isEmpty() || row.label.size() > SessionMenu::MaxLabel)
            return u"bad label for: "_s + row.name;
    }
    if (!current.isEmpty()) {
        bool listed = false;
        for (const SessionMenuRow &row : sessions)
            listed = listed || row.name == current;
        if (!listed)
            return u"current is not listed: "_s + current;
    }
    return {};
}

QString stateOf(const SessionMenuRow &row)
{
    const QVariant value = row.entry.value(u"state"_s);
    if (!value.isValid())
        return u"available"_s;
    const QString state = value.toString();
    if (state == u"available"_s || state == u"not-installed"_s || state == u"refused"_s)
        return state;
    qInfo("microset overlay: session %s state %s is unknown", qPrintable(row.name), qPrintable(state));
    return u"available"_s;
}

}

QDBusArgument &operator<<(QDBusArgument &argument, const SessionMenuRow &row)
{
    argument.beginStructure();
    argument << row.name << row.label << row.entry;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, SessionMenuRow &row)
{
    argument.beginStructure();
    argument >> row.name >> row.label >> row.entry;
    argument.endStructure();
    return argument;
}

SessionMenu::SessionMenu(const Auth &auth, const QDBusConnection &session, const QDBusConnection &system,
                         QObject *parent)
    : QObject(parent)
    , m_auth(auth)
    , m_session(session)
    , m_system(system)
{
    registerTypes();
    m_system.connect(SystemdService, SystemdPath, SystemdManager, u"JobRemoved"_s, this,
                     SLOT(onJobRemoved(uint,QDBusObjectPath,QString,QString)));
}

QString SessionMenu::service()
{
    return u"net.seilent.MicroOs.SessionMenu"_s;
}

QString SessionMenu::path()
{
    return u"/net/seilent/MicroOs/SessionMenu"_s;
}

QString SessionMenu::interface()
{
    return u"net.seilent.MicroOs.SessionMenu1"_s;
}

void SessionMenu::registerTypes()
{
    qDBusRegisterMetaType<SessionMenuRow>();
    qDBusRegisterMetaType<QList<SessionMenuRow>>();
    qRegisterMetaType<QList<SessionMenuEntry>>();
}

bool SessionMenu::publish(QDBusConnection bus)
{
    if (!bus.registerObject(path(), this, QDBusConnection::ExportScriptableSlots))
        return false;
    if (bus.interface()->registerService(service(), QDBusConnectionInterface::QueueService,
                                         QDBusConnectionInterface::DontAllowReplacement)
        == QDBusConnectionInterface::ServiceQueued)
        qInfo("microset overlay: session menu name queued");
    return true;
}

void SessionMenu::Show(const QList<SessionMenuRow> &sessions, const QString &current, const QVariantMap &hints)
{
    setDelayedReply(true);
    const QDBusMessage request = message();
    QDBusConnection bus = connection();
    QPointer<SessionMenu> self(this);
    auto done = [self, request, bus, sessions, current, hints](const QString &refusal, qint64 uid) {
        if (!refusal.isEmpty()) {
            qInfo("microset overlay: Show by uid %lld refused: %s", uid, qPrintable(refusal));
            bus.send(request.createErrorReply(QDBusError::AccessDenied, refusal));
            return;
        }
        const QString invalid = validate(sessions, current);
        if (!invalid.isEmpty()) {
            bus.send(request.createErrorReply(QDBusError::InvalidArgs, invalid));
            return;
        }
        QList<SessionMenuEntry> entries;
        for (const SessionMenuRow &row : sessions)
            entries << SessionMenuEntry{row.name, row.label, stateOf(row), row.entry.value(u"install"_s).toBool()};
        bus.send(request.createReply());
        if (self)
            emit self.data()->requested(entries, current, hints.value(u"in-game"_s).toBool(),
                                        hints.value(u"switching"_s).toBool());
    };
    m_auth.check(request.service(), std::move(done));
}

void SessionMenu::startSwitch(const QString &name)
{
    m_job.clear();
    m_removed.clear();
    m_waiting = true;
    m_system.asyncCall(
        QDBusMessage::createMethodCall(SystemdService, SystemdPath, SystemdManager, u"Subscribe"_s));
    QDBusMessage call = QDBusMessage::createMethodCall(SystemdService, SystemdPath, SystemdManager, u"StartUnit"_s);
    call << u"microos-session-switch@"_s + name + u".service"_s << u"replace"_s;
    auto *watcher = new QDBusPendingCallWatcher(m_system.asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, name] {
        watcher->deleteLater();
        m_waiting = false;
        const QDBusMessage reply = watcher->reply();
        if (reply.type() != QDBusMessage::ReplyMessage) {
            qInfo("microset overlay: switch to %s failed: %s", qPrintable(name), qPrintable(reply.errorMessage()));
            emit switchReplied(false);
            return;
        }
        emit switchReplied(true);
        const QString job = reply.arguments().value(0).value<QDBusObjectPath>().path();
        for (qsizetype i = 0; i < m_removed.size(); ++i) {
            if (m_removed.at(i).first != job)
                continue;
            const QString result = m_removed.at(i).second;
            m_removed.removeAt(i);
            m_job.clear();
            emit switchJobRemoved(result);
            return;
        }
        m_job = job;
    });
}

void SessionMenu::startInstall(const QString &name)
{
    QDBusMessage call = QDBusMessage::createMethodCall(SystemdService, SystemdPath, SystemdManager, u"StartUnit"_s);
    call << u"microos-session-install@"_s + name + u".service"_s << u"replace"_s;
    auto *watcher = new QDBusPendingCallWatcher(m_session.asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, name] {
        watcher->deleteLater();
        const QDBusMessage reply = watcher->reply();
        if (reply.type() != QDBusMessage::ReplyMessage) {
            qInfo("microset overlay: install of %s failed: %s", qPrintable(name), qPrintable(reply.errorMessage()));
            emit installReplied(false);
            return;
        }
        emit installReplied(true);
    });
}

void SessionMenu::onJobRemoved(uint, const QDBusObjectPath &job, const QString &, const QString &result)
{
    const QString path = job.path();
    if (!m_job.isEmpty()) {
        if (path == m_job) {
            m_job.clear();
            emit switchJobRemoved(result);
        }
        return;
    }
    if (!m_waiting)
        return;
    m_removed.append({path, result});
    if (m_removed.size() > MaxRemembered)
        m_removed.removeFirst();
}
