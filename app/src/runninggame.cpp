#include "runninggame.h"

#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusVariant>
#include <QDir>
#include <QFile>
#include <QMap>
#include <QRegularExpression>

#include <functional>
#include <memory>

#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

using namespace Qt::StringLiterals;

namespace {

const QString Service = u"org.freedesktop.systemd1"_s;
const QString Path = u"/org/freedesktop/systemd1"_s;
const QString Manager = u"org.freedesktop.systemd1.Manager"_s;
const QString Properties = u"org.freedesktop.DBus.Properties"_s;
const QRegularExpression Scope(u"^app-steam-app([1-9][0-9]{0,9})-[0-9]+\\.scope$"_s);
const QRegularExpression GameId(u"^[0-9]{1,20}$"_s);

bool protonDir(const QString &dir)
{
    struct stat st;
    return dir.startsWith(u'/') && ::lstat(QFile::encodeName(dir).constData(), &st) == 0 && S_ISDIR(st.st_mode)
        && ::lstat(QFile::encodeName(dir + u"/proton"_s).constData(), &st) == 0 && S_ISREG(st.st_mode);
}

qulonglong monotonicUs()
{
    timespec now{};
    ::clock_gettime(CLOCK_MONOTONIC, &now);
    return qulonglong(now.tv_sec) * 1000000ULL + qulonglong(now.tv_nsec) / 1000ULL;
}

struct Pick
{
    int waiting = 0;
    bool found = false;
    QString unit;
    QString appid;
    QString path;
    qulonglong since = 0;
};

}

QDBusArgument &operator<<(QDBusArgument &argument, const SystemdUnit &unit)
{
    argument.beginStructure();
    argument << unit.name << unit.description << unit.load << unit.active << unit.sub << unit.following << unit.path
             << unit.job << unit.jobType << unit.jobPath;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, SystemdUnit &unit)
{
    argument.beginStructure();
    argument >> unit.name >> unit.description >> unit.load >> unit.active >> unit.sub >> unit.following >> unit.path
        >> unit.job >> unit.jobType >> unit.jobPath;
    argument.endStructure();
    return argument;
}

QDBusArgument &operator<<(QDBusArgument &argument, const UnitProcess &process)
{
    argument.beginStructure();
    argument << process.cgroup << process.pid << process.command;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, UnitProcess &process)
{
    argument.beginStructure();
    argument >> process.cgroup >> process.pid >> process.command;
    argument.endStructure();
    return argument;
}

RunningGame::RunningGame(const QDBusConnection &bus, const QString &procRoot, QObject *parent)
    : QObject(parent)
    , m_bus(bus)
    , m_proc(procRoot)
{
    registerTypes();
    m_later.setSingleShot(true);
    connect(&m_later, &QTimer::timeout, this, &RunningGame::refresh);
}

void RunningGame::registerTypes()
{
    qDBusRegisterMetaType<SystemdUnit>();
    qDBusRegisterMetaType<QList<SystemdUnit>>();
    qDBusRegisterMetaType<UnitProcess>();
    qDBusRegisterMetaType<QList<UnitProcess>>();
}

QVariantMap RunningGame::readEnviron(const QString &procRoot, const QList<uint> &pids)
{
    QVariantMap out{{u"hook"_s, QString()}, {u"profile"_s, QString()}, {u"yielded"_s, QString()}, {u"tool"_s, QString()},
                    {u"proton"_s, false}, {u"steamFex"_s, false}, {u"lsfg"_s, QString()},
                    {u"steamGameId"_s, QString()}};
    bool hooked = false;
    bool framed = false;
    for (const uint pid : pids.mid(0, MaxProcesses)) {
        QFile file(procRoot + u'/' + QString::number(pid) + u"/environ"_s);
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QByteArray data = file.read(MaxEnviron + 1);
        if (data.size() > MaxEnviron)
            continue;
        QMap<QByteArray, QByteArray> env;
        for (const QByteArray &line : data.split('\0')) {
            const qsizetype equals = line.indexOf('=');
            if (equals > 0)
                env.insert(line.left(equals), line.mid(equals + 1));
        }
        if (!hooked && env.contains("MICROOS_PROTON_HOOK")) {
            hooked = true;
            out.insert(u"hook"_s, QString::fromUtf8(env.value("MICROOS_PROTON_HOOK")));
            out.insert(u"profile"_s, QString::fromUtf8(env.value("MICROOS_PROTON_PROFILE")));
            out.insert(u"yielded"_s, QString::fromUtf8(env.value("MICROOS_PROTON_YIELDED")));
        }
        if (!framed && env.contains("MICROOS_LSFG")) {
            framed = true;
            out.insert(u"lsfg"_s, QString::fromUtf8(env.value("MICROOS_LSFG")));
            const QString id = QString::fromUtf8(env.value("SteamGameId"));
            if (GameId.match(id).hasMatch())
                out.insert(u"steamGameId"_s, id);
        }
        const QString tool = QString::fromUtf8(env.value("STEAM_COMPAT_TOOL_PATHS")).section(u':', 0, 0);
        if (!out.value(u"proton"_s).toBool() && protonDir(tool)) {
            out.insert(u"proton"_s, true);
            out.insert(u"tool"_s, tool.section(u'/', -1, -1, QString::SectionSkipEmpty));
        }
        if (!env.value("STEAM_COMPAT_FEX_CONFIG").isEmpty())
            out.insert(u"steamFex"_s, true);
    }
    return out;
}

bool RunningGame::signalHeld(const QString &dir, const QString &id)
{
    const int dirFd = ::open(QFile::encodeName(dir).constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (dirFd < 0)
        return false;
    DIR *entries = ::fdopendir(dirFd);
    if (!entries) {
        ::close(dirFd);
        return false;
    }
    const QRegularExpression name(u"^"_s + QRegularExpression::escape(id) + u"-[0-9]+$"_s);
    bool held = false;
    int seen = 0;
    while (!held && seen < MaxSignals) {
        const dirent *entry = ::readdir(entries);
        if (!entry)
            break;
        if (!name.match(QString::fromUtf8(entry->d_name)).hasMatch())
            continue;
        ++seen;
        const int fd = ::openat(::dirfd(entries), entry->d_name, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0)
            continue;
        struct stat st;
        if (::fstat(fd, &st) == 0 && S_ISREG(st.st_mode)) {
            if (::flock(fd, LOCK_SH | LOCK_NB) == 0)
                ::flock(fd, LOCK_UN);
            else
                held = errno == EWOULDBLOCK;
        }
        ::close(fd);
    }
    ::closedir(entries);
    return held;
}

QString RunningGame::signalDir()
{
    return QDir::homePath() + u"/.cache/lsfg-vk/active"_s;
}

void RunningGame::setSignalDir(const QString &dir)
{
    m_signals = dir;
}

QVariantMap RunningGame::info() const
{
    return m_info;
}

void RunningGame::skip(const QStringList &appids)
{
    m_skip = appids;
}

void RunningGame::refresh()
{
    const quint64 generation = ++m_generation;
    if (!m_subscribed) {
        m_subscribed = m_bus.connect(Service, Path, Manager, u"UnitRemoved"_s, this, SLOT(onRemoved(QString,QDBusObjectPath)));
        if (m_subscribed)
            m_bus.asyncCall(QDBusMessage::createMethodCall(Service, Path, Manager, u"Subscribe"_s));
    }
    QDBusMessage list = QDBusMessage::createMethodCall(Service, Path, Manager, u"ListUnitsByPatterns"_s);
    list << QStringList{u"active"_s} << QStringList{u"app-steam-app*.scope"_s};
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(list), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, generation] {
        watcher->deleteLater();
        if (generation != m_generation)
            return;
        const QDBusMessage reply = watcher->reply();
        if (reply.type() != QDBusMessage::ReplyMessage) {
            watch({});
            setInfo({});
            return;
        }
        choose(qdbus_cast<QList<SystemdUnit>>(reply.arguments().value(0)), generation);
    });
}

void RunningGame::choose(const QList<SystemdUnit> &units, quint64 generation)
{
    auto pick = std::make_shared<Pick>();
    const auto get = [this](const QDBusObjectPath &path, const QString &interface, const QString &name,
                            std::function<void(const QVariant &)> done) {
        QDBusMessage call = QDBusMessage::createMethodCall(Service, path.path(), Properties, u"Get"_s);
        call << interface << name;
        auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [watcher, done = std::move(done)] {
            watcher->deleteLater();
            const QDBusMessage reply = watcher->reply();
            done(reply.type() == QDBusMessage::ReplyMessage ? reply.arguments().value(0).value<QDBusVariant>().variant()
                                                            : QVariant());
        });
    };
    for (const SystemdUnit &unit : units) {
        const QString appid = Scope.match(unit.name).captured(1);
        if (appid.isEmpty() || m_skip.contains(appid) || pick->waiting >= MaxUnits)
            continue;
        ++pick->waiting;
        auto slice = std::make_shared<QString>();
        auto since = std::make_shared<qulonglong>(0);
        auto left = std::make_shared<int>(2);
        const auto got = [this, pick, slice, since, left, name = unit.name, appid, path = unit.path.path(), generation] {
            if (--*left > 0)
                return;
            if (*slice == Slice && (!pick->found || *since > pick->since)) {
                pick->found = true;
                pick->unit = name;
                pick->appid = appid;
                pick->path = path;
                pick->since = *since;
            }
            if (--pick->waiting > 0 || generation != m_generation)
                return;
            if (pick->found) {
                inspect(pick->unit, pick->appid, pick->path, pick->since, generation);
                return;
            }
            watch({});
            setInfo({});
        };
        get(unit.path, u"org.freedesktop.systemd1.Scope"_s, u"Slice"_s, [slice, got](const QVariant &value) {
            *slice = value.toString();
            got();
        });
        get(unit.path, u"org.freedesktop.systemd1.Unit"_s, u"ActiveEnterTimestampMonotonic"_s,
            [since, got](const QVariant &value) {
                *since = value.toULongLong();
                got();
            });
    }
    if (pick->waiting > 0)
        return;
    watch({});
    setInfo({});
}

void RunningGame::inspect(const QString &unit, const QString &appid, const QString &path, qulonglong since,
                          quint64 generation)
{
    QDBusMessage call = QDBusMessage::createMethodCall(Service, Path, Manager, u"GetUnitProcesses"_s);
    call << unit;
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, unit, appid, path, since, generation] {
        watcher->deleteLater();
        if (generation != m_generation)
            return;
        QList<uint> pids;
        const QDBusMessage reply = watcher->reply();
        if (reply.type() == QDBusMessage::ReplyMessage)
            for (const UnitProcess &process : qdbus_cast<QList<UnitProcess>>(reply.arguments().value(0)))
                pids << process.pid;
        QVariantMap info = readEnviron(m_proc, pids);
        info.insert(u"appid"_s, appid);
        info.insert(u"unit"_s, unit);
        const QString lsfg = info.value(u"lsfg"_s).toString();
        if (lsfg == "on"_L1 || lsfg == "cap-overridden"_L1) {
            const qulonglong now = monotonicUs();
            const bool young = since > 0 && now >= since && now - since < qulonglong(StartWindowUs);
            const QString id = info.value(u"steamGameId"_s).toString();
            const bool held = signalHeld(m_signals, id.isEmpty() ? appid : id);
            info.insert({{u"lsfgHeld"_s, held}, {u"lsfgYoung"_s, young}});
            if (young && !held)
                m_later.start(int((qulonglong(StartWindowUs) - (now - since)) / 1000) + 1);
        }
        watch(path);
        setInfo(info);
    });
}

void RunningGame::watch(const QString &path)
{
    if (path == m_path)
        return;
    if (!m_path.isEmpty())
        m_bus.disconnect(Service, m_path, Properties, u"PropertiesChanged"_s, this,
                         SLOT(onProperties(QString,QVariantMap,QStringList)));
    m_path = path;
    if (!m_path.isEmpty())
        m_bus.connect(Service, m_path, Properties, u"PropertiesChanged"_s, this,
                      SLOT(onProperties(QString,QVariantMap,QStringList)));
}

void RunningGame::onRemoved(const QString &name, const QDBusObjectPath &)
{
    if (!name.isEmpty() && name == m_info.value(u"unit"_s).toString())
        markGone();
}

void RunningGame::onProperties(const QString &interface, const QVariantMap &changed, const QStringList &)
{
    if (interface == "org.freedesktop.systemd1.Unit"_L1 && changed.contains(u"ActiveState"_s)
        && changed.value(u"ActiveState"_s).toString() != "active"_L1)
        markGone();
}

void RunningGame::markGone()
{
    if (m_info.isEmpty() || m_info.value(u"gone"_s).toBool())
        return;
    QVariantMap info = m_info;
    info.insert(u"gone"_s, true);
    setInfo(info);
}

void RunningGame::setInfo(const QVariantMap &info)
{
    if (info == m_info)
        return;
    m_info = info;
    emit changed();
}
