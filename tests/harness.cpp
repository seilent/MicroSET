#include "harness.h"

#include <QCoreApplication>
#include <QDBusConnectionInterface>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTest>
#include <QTimer>

#include <pwd.h>
#include <sys/stat.h>
#include <functional>
#include <unistd.h>

using namespace Qt::StringLiterals;

namespace {

const QString ClientName = u"harness-client"_s;

bool waitFor(const std::function<bool()> &ready, int ms = 10000)
{
    QElapsedTimer timer;
    timer.start();
    while (!ready()) {
        if (timer.elapsed() > ms)
            return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QTest::qSleep(10);
    }
    return true;
}

}

uint Harness::user()
{
    const passwd *pw = ::getpwnam("nobody");
    if (!pw)
        qFatal("the tests run callers as the user nobody, which does not exist");
    return pw->pw_uid;
}

QStringList Harness::wheel()
{
    const QString uid = QString::number(user());
    return {QStringLiteral(SETPRIV), u"--reuid"_s, uid, u"--regid"_s, uid, u"--groups"_s, u"10"_s};
}

QStringList Harness::noWheel()
{
    const QString uid = QString::number(user());
    return {QStringLiteral(SETPRIV), u"--reuid"_s, uid, u"--regid"_s, uid, u"--clear-groups"_s};
}

Harness::Harness()
{
    ::chmod(QFile::encodeName(m_dir.path()).constData(), 0755);
    QDir(m_dir.path()).mkpath(u"state"_s);
    QDir(m_dir.path()).mkpath(u"proc"_s);
    ::chmod(QFile::encodeName(path(u"state"_s)).constData(), 0700);
}

Harness::~Harness()
{
    stopDaemon();
    QDBusConnection::disconnectFromBus(ClientName);
    if (m_bus.state() != QProcess::NotRunning) {
        m_bus.kill();
        m_bus.waitForFinished();
    }
}

QString Harness::path(const QString &relative) const
{
    return m_dir.filePath(relative);
}

void Harness::writeFile(const QString &relative, const QByteArray &data) const
{
    const QString target = path(relative);
    QDir().mkpath(QFileInfo(target).path());
    QFile file(target);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(data) != data.size())
        qFatal("cannot write %s", qPrintable(target));
}

void Harness::writeSessions() const
{
    writeFile(u"root/usr/lib/microos/sessions.d/steam.conf"_s,
              "LABEL=Steam\nDESKTOP=gamescope-session-steam.desktop\nUNIT=gamescope-session-plus@steam.service\n"
              "STOP=steam\nDEFAULT=yes\n");
    writeFile(u"root/usr/lib/microos/sessions.d/microdap.conf"_s,
              "LABEL=MicroDAP\nDESKTOP=\"microdap.desktop\"\nUNIT=gamescope-session-plus@microdap.service\n"
              "STOP=unit\nDEFAULT=yes\n");
    writeFile(u"root/usr/lib/microos/sessions.d/recovery.conf"_s,
              "LABEL=Recovery\nDESKTOP=microos-recovery.desktop\nUNIT=gamescope-session-plus@recovery.service\n"
              "STOP=unit\nDEFAULT=no\n");
    writeFile(u"root/usr/share/wayland-sessions/gamescope-session-steam.desktop"_s, "[Desktop Entry]\n");
    writeFile(u"root/usr/share/wayland-sessions/microdap.desktop"_s, "[Desktop Entry]\n");
    writeFile(u"root/usr/share/wayland-sessions/microos-recovery.desktop"_s, "[Desktop Entry]\n");
    writeFile(u"root/usr/lib/systemd/user/gamescope-session-plus@.service"_s, "[Service]\n");
}

void Harness::writeState(const QByteArray &values) const
{
    writeFile(u"state/settings.json"_s, "{\"version\": 1, \"values\": " + values + "}");
}

QByteArray Harness::readState() const
{
    QFile file(path(u"state/settings.json"_s));
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool Harness::startBus()
{
    const QString socket = path(u"bus/socket"_s);
    QDir().mkpath(path(u"bus"_s));
    ::chmod(QFile::encodeName(path(u"bus"_s)).constData(), 0755);
    const QByteArray config = R"(<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-BUS Bus Configuration 1.0//EN"
 "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<busconfig>
  <type>system</type>
  <listen>unix:path=)" + QFile::encodeName(socket) + R"(</listen>
  <auth>EXTERNAL</auth>
  <policy context="default">
    <allow user="*"/>
    <allow own="*"/>
    <allow send_type="method_call"/>
    <allow send_type="signal"/>
    <allow send_requested_reply="true" send_type="method_return"/>
    <allow send_requested_reply="true" send_type="error"/>
    <allow receive_type="method_call"/>
    <allow receive_type="method_return"/>
    <allow receive_type="error"/>
    <allow receive_type="signal"/>
  </policy>
  <include>)" + QFile::encodeName(QStringLiteral(MICROSET_SOURCE_DIR) + u"/data/net.seilent.MicroSet.conf"_s) + R"(</include>
</busconfig>
)";
    writeFile(u"bus.conf"_s, config);
    m_bus.setProcessChannelMode(QProcess::ForwardedChannels);
    m_bus.start(QStringLiteral(DBUS_DAEMON), {u"--config-file="_s + path(u"bus.conf"_s), u"--nofork"_s, u"--nopidfile"_s});
    if (!waitFor([&] { return QFileInfo::exists(socket); }))
        return false;
    m_address = u"unix:path="_s + socket;
    writeCgroup(QCoreApplication::applicationPid(), RootCgroup);
    return QDBusConnection::connectToBus(m_address, ClientName).isConnected();
}

QString Harness::address() const
{
    return m_address;
}

QDBusConnection Harness::client() const
{
    return QDBusConnection(ClientName);
}

void Harness::writeCgroup(qint64 pid, const QByteArray &cgroup) const
{
    writeFile(u"proc/%1/cgroup"_s.arg(pid), cgroup + '\n');
}

QProcessEnvironment Harness::environment() const
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(u"DBUS_SYSTEM_BUS_ADDRESS"_s, m_address);
    env.insert(u"QT_FORCE_STDERR_LOGGING"_s, u"1"_s);
    env.insert(u"LC_ALL"_s, u"C.UTF-8"_s);
    env.insert(u"MICROSET_SCHEMA_DIR"_s, QStringLiteral(MICROSET_SOURCE_DIR) + u"/schema"_s);
    env.insert(u"STATE_DIRECTORY"_s, path(u"state"_s));
    env.insert(u"MICROSET_POWER_CONF"_s, path(u"power.conf"_s));
    env.insert(u"MICROSET_SESSIONS_ROOT"_s, path(u"root"_s));
    env.insert(u"MICROOS_PROC"_s, path(u"proc"_s));
    env.insert(u"MICROSET_ALSA_CARD"_s, u"MicroSetTest"_s);
    env.insert(u"MICROSET_ALSA_DEVICES"_s, path(u"dev/snd"_s));
    return env;
}

bool Harness::startDaemon()
{
    m_log.clear();
    m_daemon.setProcessEnvironment(environment());
    m_daemon.setProcessChannelMode(QProcess::MergedChannels);
    m_daemon.start(QStringLiteral(MICROSET_DAEMON), {});
    if (!m_daemon.waitForStarted())
        return false;
    QDBusConnectionInterface *bus = client().interface();
    return waitFor([&] {
        return bus->isServiceRegistered(u"net.seilent.MicroSet"_s).value() || m_daemon.state() == QProcess::NotRunning;
    }) && m_daemon.state() == QProcess::Running;
}

void Harness::stopDaemon()
{
    if (m_daemon.state() == QProcess::NotRunning)
        return;
    m_daemon.terminate();
    if (!m_daemon.waitForFinished(5000)) {
        m_daemon.kill();
        m_daemon.waitForFinished();
    }
    m_log += m_daemon.readAll();
    QDBusConnectionInterface *bus = client().interface();
    waitFor([&] { return !bus->isServiceRegistered(u"net.seilent.MicroSet"_s).value(); });
}

QString Harness::daemonLog()
{
    m_log += m_daemon.readAll();
    return QString::fromUtf8(m_log);
}

QDBusMessage Harness::call(const QString &method, const QVariantList &arguments) const
{
    QDBusMessage message = QDBusMessage::createMethodCall(u"net.seilent.MicroSet"_s, u"/net/seilent/MicroSet"_s,
                                                          u"net.seilent.MicroSet1"_s, method);
    message.setArguments(arguments);
    return client().call(message, QDBus::BlockWithGui, 10000);
}

int Harness::cli(const QStringList &arguments, const QStringList &as, const QByteArray &cgroup, QString *output) const
{
    QProcess process;
    process.setProcessEnvironment(environment());
    const QString script = u"mkdir -p \"$MICROOS_PROC/$$\" && printf '%s\\n' \"$0\" > \"$MICROOS_PROC/$$/cgroup\" && exec \"$@\""_s;
    process.start(u"sh"_s, QStringList{u"-c"_s, script, QString::fromUtf8(cgroup)} + as
                               + QStringList{QStringLiteral(MICROSET_CLI)} + arguments);
    if (!waitFor([&] { return process.state() == QProcess::NotRunning; }))
        process.kill();
    process.waitForFinished();
    if (output)
        *output = QString::fromUtf8(process.readAllStandardOutput());
    const QByteArray errors = process.readAllStandardError();
    if (!errors.isEmpty())
        qInfo("cli %s: %s", qPrintable(arguments.join(u' ')), errors.trimmed().constData());
    return process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
}
