#include "auth.h"

#include <QDBusArgument>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QFile>

#include <grp.h>
#include <pwd.h>
#include <vector>

using namespace Qt::StringLiterals;

namespace {

QList<uint> groupsOf(uint uid)
{
    const passwd *pw = ::getpwuid(uid);
    if (!pw)
        return {};
    int count = 64;
    std::vector<gid_t> groups(static_cast<size_t>(count));
    if (::getgrouplist(pw->pw_name, pw->pw_gid, groups.data(), &count) < 0) {
        groups.resize(size_t(count));
        if (::getgrouplist(pw->pw_name, pw->pw_gid, groups.data(), &count) < 0)
            return {};
    }
    return QList<uint>(groups.begin(), groups.begin() + count);
}

QList<uint> groupsFrom(const QVariant &value)
{
    QList<uint> out;
    if (value.userType() == qMetaTypeId<QDBusArgument>())
        value.value<QDBusArgument>() >> out;
    else
        out = value.value<QList<uint>>();
    return out;
}

QDBusMessage credentialsCall(const QString &sender)
{
    QDBusMessage call = QDBusMessage::createMethodCall(u"org.freedesktop.DBus"_s, u"/org/freedesktop/DBus"_s,
                                                       u"org.freedesktop.DBus"_s, u"GetConnectionCredentials"_s);
    call << sender;
    return call;
}

}

Auth::Auth(const QDBusConnection &bus, const QString &procRoot)
    : m_bus(bus)
    , m_proc(procRoot)
{
}

QString Auth::refusal(const QString &sender, qint64 *uid) const
{
    return judge(m_bus.call(credentialsCall(sender), QDBus::Block, 500), m_proc, uid);
}

void Auth::check(const QString &sender, std::function<void(const QString &refusal, qint64 uid)> done) const
{
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(credentialsCall(sender), 500));
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, watcher,
                     [proc = m_proc, watcher, done = std::move(done)] {
        watcher->deleteLater();
        qint64 uid = -1;
        const QString refusal = judge(watcher->reply(), proc, &uid);
        done(refusal, uid);
    });
}

QString Auth::judge(const QDBusMessage &reply, const QString &procRoot, qint64 *uid)
{
    *uid = -1;
    if (reply.type() != QDBusMessage::ReplyMessage)
        return u"no credentials: "_s + reply.errorMessage();
    const QVariantMap credentials = qdbus_cast<QVariantMap>(reply.arguments().value(0));
    if (!credentials.contains(u"UnixUserID"_s) || !credentials.contains(u"ProcessID"_s))
        return u"no uid or pid from the bus"_s;
    *uid = credentials.value(u"UnixUserID"_s).toUInt();
    const uint pid = credentials.value(u"ProcessID"_s).toUInt();
    const QList<uint> groups = credentials.contains(u"UnixGroupIDs"_s)
        ? groupsFrom(credentials.value(u"UnixGroupIDs"_s))
        : groupsOf(uint(*uid));
    const group *wheel = ::getgrnam("wheel");
    if (*uid != 0 && !(wheel && groups.contains(uint(wheel->gr_gid))))
        return u"not in wheel"_s;
    QFile cgroup(procRoot + u'/' + QString::number(pid) + u"/cgroup"_s);
    if (!cgroup.open(QIODevice::ReadOnly))
        return u"cannot read the cgroup of pid %1"_s.arg(pid);
    const QByteArray path = cgroup.readAll().trimmed();
    if (path.contains("/app-steam-app") || path.endsWith("/microemu-game.scope") || path.contains("/microemu-game.scope/"))
        return u"called from a game"_s;
    return {};
}
