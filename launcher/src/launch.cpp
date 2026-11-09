#include "launch.h"

#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QDebug>
#include <QEventLoop>

using namespace Qt::StringLiterals;

namespace {

constexpr const char *ScopePrefix = "app-microset\\x2dlauncher-";

UnitProperty properties(const QString &name, const QVariant &value)
{
    return UnitProperty{name, QDBusVariant(value)};
}

QList<UnitProperty> transientProperties(const QString &description, qint64 pid)
{
    return {
        properties(u"Description"_s, description),
        properties(u"Slice"_s, u"app.slice"_s),
        properties(u"PIDs"_s, QVariant::fromValue(QList<uint>{static_cast<uint>(pid)})),
    };
}

}

QDBusArgument &operator<<(QDBusArgument &argument, const UnitProperty &property)
{
    argument.beginStructure();
    argument << property.name << property.value;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, UnitProperty &property)
{
    argument.beginStructure();
    argument >> property.name >> property.value;
    argument.endStructure();
    return argument;
}

QDBusArgument &operator<<(QDBusArgument &argument, const UnitAux &aux)
{
    argument.beginStructure();
    argument << aux.name << aux.value;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, UnitAux &aux)
{
    argument.beginStructure();
    argument >> aux.name >> aux.value;
    argument.endStructure();
    return argument;
}

ManagerEnv::ManagerEnv(const QDBusConnection &bus, QObject *parent)
    : QObject(parent)
    , m_bus(bus)
{
}

QString ManagerEnv::service()
{
    return u"org.freedesktop.systemd1"_s;
}

QString ManagerEnv::path()
{
    return u"/org/freedesktop/systemd1"_s;
}

QString ManagerEnv::managerInterface()
{
    return u"org.freedesktop.systemd1.Manager"_s;
}

QString ManagerEnv::propertiesInterface()
{
    return u"org.freedesktop.DBus.Properties"_s;
}

QMap<QString, QString> ManagerEnv::parse(const QStringList &entries)
{
    QMap<QString, QString> out;
    for (const QString &entry : entries) {
        const qsizetype at = entry.indexOf(u'=');
        if (at <= 0)
            continue;
        out.insert(entry.left(at), entry.mid(at + 1));
    }
    return out;
}

QMap<QString, QString> ManagerEnv::environment() const
{
    return m_values;
}

bool ManagerEnv::stale() const
{
    return m_signals != m_reads;
}

void ManagerEnv::subscribe()
{
    QDBusMessage call = QDBusMessage::createMethodCall(service(), path(), managerInterface(), u"Subscribe"_s);
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call, 2000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        const QDBusPendingReply<> reply = *w;
        if (reply.isError())
            qWarning("launcher: cannot subscribe to the user manager, unit changes may be missed: %s",
                     qPrintable(reply.error().message()));
    });
}

void ManagerEnv::refresh()
{
    ++m_signals;
    read();
}

void ManagerEnv::read()
{
    if (!m_bus.isConnected()) {
        emit readDone();
        return;
    }
    QDBusMessage call = QDBusMessage::createMethodCall(service(), path(), propertiesInterface(), u"Get"_s);
    call << managerInterface() << u"Environment"_s;
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call, 2000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        const QDBusPendingReply<QDBusVariant> reply = *w;
        if (reply.isError()) {
            qWarning("launcher: cannot read the user manager environment: %s",
                     qPrintable(reply.error().message()));
            emit readDone();
            return;
        }
        m_values = parse(reply.value().variant().toStringList());
        m_reads = m_signals;
        for (const QString &name : LauncherLaunch::requiredNames())
            if (!m_values.contains(name))
                qWarning("launcher: the user manager environment lacks %s, the app does not get it",
                         qPrintable(name));
        emit readDone();
    });
}

void ManagerEnv::watch()
{
    if (m_watching || !m_bus.isConnected())
        return;
    m_watching = true;
    m_bus.connect(service(), path(), propertiesInterface(), u"PropertiesChanged"_s, this, SLOT(refresh()));
    m_bus.connect(service(), path(), managerInterface(), u"Reloading"_s, this, SLOT(refresh()));
    subscribe();
    read();
}

LauncherLaunch::LauncherLaunch(const QDBusConnection &bus, AppModel *apps, QObject *parent)
    : Activator(parent)
    , m_bus(bus)
    , m_apps(apps)
{
}

QStringList LauncherLaunch::requiredNames()
{
    return {u"HOME"_s, u"PATH"_s, u"XDG_RUNTIME_DIR"_s, u"DBUS_SESSION_BUS_ADDRESS"_s};
}

QString LauncherLaunch::escapeId(const QString &appId)
{
    QString out;
    for (const char c : appId.toUtf8()) {
        const uint byte = static_cast<unsigned char>(c);
        if ((byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') || (byte >= '0' && byte <= '9')
            || byte == '-' || byte == '_' || byte == '.')
            out += QLatin1Char(c);
        else
            out += QStringLiteral("\\x%1").arg(byte, 2, 16, QLatin1Char('0'));
    }
    if (out.size() > MaxIdLength)
        out = out.left(MaxIdLength);
    return out;
}

QString LauncherLaunch::scopeName(const QString &appId, qint64 pid)
{
    return QString::fromLatin1(ScopePrefix) + escapeId(appId) + u'-' + QString::number(pid) + u".scope"_s;
}

QMap<QString, QString> LauncherLaunch::environment() const
{
    return m_environment;
}

void LauncherLaunch::setEnvironment(const QMap<QString, QString> &values)
{
    m_environment = values;
}

void LauncherLaunch::setManager(ManagerEnv *manager)
{
    m_manager = manager;
}

void LauncherLaunch::start(const QString &id)
{
    if (m_apps == nullptr)
        return;
    if (m_manager != nullptr) {
        if (m_manager->stale()) {
            QEventLoop loop;
            const QMetaObject::Connection link =
                connect(m_manager, &ManagerEnv::readDone, &loop, &QEventLoop::quit);
            m_manager->refresh();
            loop.exec(QEventLoop::ExcludeUserInputEvents);
            QObject::disconnect(link);
        }
        const QMap<QString, QString> current = m_manager->environment();
        if (!current.isEmpty())
            m_environment = current;
    }
    m_apps->launch(id, this);
}

void LauncherLaunch::reportStarted(const QString &appId, qint64 pid)
{
    moveToScope(appId, pid);
    emit started(appId, pid);
}

void LauncherLaunch::reportFailed(const QString &appId, const QString &message)
{
    emit failed(appId, message);
}

void LauncherLaunch::moveToScope(const QString &appId, qint64 pid)
{
    if (!m_bus.isConnected())
        return;
    qDBusRegisterMetaType<UnitProperty>();
    qDBusRegisterMetaType<QList<UnitProperty>>();
    qDBusRegisterMetaType<UnitAux>();
    qDBusRegisterMetaType<QList<UnitAux>>();
    const QString name = scopeName(appId, pid);
    const QString description = u"MicroSET launcher: "_s + appId;
    QDBusMessage call = QDBusMessage::createMethodCall(ManagerEnv::service(), ManagerEnv::path(),
                                                       ManagerEnv::managerInterface(), u"StartTransientUnit"_s);
    const QList<UnitProperty> values = transientProperties(description, pid);
    call << name << u"fail"_s << QVariant::fromValue(values) << QVariant::fromValue(QList<UnitAux>{});
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [name](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        const QDBusPendingReply<QDBusObjectPath> reply = *w;
        if (reply.isError())
            qWarning("launcher: cannot put the app in %s: %s", qPrintable(name), qPrintable(reply.error().message()));
    });
}
