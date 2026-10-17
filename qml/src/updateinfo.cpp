#include "updateinfo.h"

#include <QFile>
#include <QFileInfo>
#include <QProcess>

using namespace Qt::StringLiterals;

UpdateInfo::UpdateInfo(QObject *parent)
    : QObject(parent)
{
    connect(&m_watch, &QFileSystemWatcher::directoryChanged, this, [this] {
        const QString before = m_status;
        if (!readUpdateFile() && before == "available"_L1)
            setUpdate(u"unchecked"_s, {});
        watchDir();
    });
}

QString UpdateInfo::osReleasePath()
{
    return qEnvironmentVariable("MICROSET_OS_RELEASE", u"/usr/lib/os-release"_s);
}

QString UpdateInfo::updateFile()
{
    return qEnvironmentVariable("MICROSET_UPDATE_FILE", u"/run/microos/update-available"_s);
}

QString UpdateInfo::readKey(const QString &path, const QString &key)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    const QString prefix = key + u'=';
    QString value;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.startsWith(prefix))
            value = line.mid(prefix.size());
    }
    if (value.size() >= 2 && (value.startsWith(u'"') || value.startsWith(u'\'')) && value.endsWith(value.front()))
        value = value.mid(1, value.size() - 2);
    return value;
}

QString UpdateInfo::checkStatus(const QString &show)
{
    QString status;
    QString started;
    for (const QString &line : show.split(u'\n')) {
        if (line.startsWith("ExecMainStatus="_L1))
            status = line.mid(15).trimmed();
        else if (line.startsWith("ExecMainStartTimestampMonotonic="_L1))
            started = line.mid(32).trimmed();
    }
    if (started.isEmpty() || started == "0"_L1)
        return u"unchecked"_s;
    if (status == "0"_L1 || status == "7"_L1)
        return u"none"_s;
    if (status == "4"_L1)
        return u"offline"_s;
    return u"unchecked"_s;
}

QString UpdateInfo::text(const QString &status, const QString &version)
{
    if (status == "available"_L1)
        return version.isEmpty() ? u"available"_s : version + u" available"_s;
    if (status == "none"_L1)
        return u"up to date"_s;
    if (status == "offline"_L1)
        return u"offline"_s;
    return u"not checked"_s;
}

void UpdateInfo::watch()
{
    m_imageVersion = readKey(osReleasePath(), u"IMAGE_VERSION"_s);
    readUpdateFile();
    watchDir();
    emit changed();
}

void UpdateInfo::watchDir()
{
    QString dir = QFileInfo(updateFile()).absolutePath();
    while (!QFileInfo(dir).isDir() && dir != QFileInfo(dir).absolutePath())
        dir = QFileInfo(dir).absolutePath();
    const QStringList watched = m_watch.directories();
    if (watched == QStringList{dir})
        return;
    if (!watched.isEmpty())
        m_watch.removePaths(watched);
    m_watch.addPath(dir);
}

bool UpdateInfo::readUpdateFile()
{
    const QString path = updateFile();
    const QString state = readKey(path, u"state"_s);
    const QString checked = readKey(path, u"checked"_s);
    const bool available = !state.isEmpty() && state != "withdrawn"_L1;
    const bool moved = m_state != state || m_checked != checked;
    m_state = state;
    m_checked = checked;
    if (available) {
        const QString version = readKey(path, u"version"_s);
        if (moved || m_status != "available"_L1 || m_version != version) {
            m_status = u"available"_s;
            m_version = version;
            emit changed();
        }
        return true;
    }
    if (moved)
        emit changed();
    return false;
}

void UpdateInfo::setUpdate(const QString &status, const QString &version)
{
    if (m_status == status && m_version == version)
        return;
    m_status = status;
    m_version = version;
    emit changed();
}

void UpdateInfo::reload()
{
    if (readUpdateFile() || m_check)
        return;
    m_check = new QProcess(this);
    connect(m_check, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        const QString out = QString::fromUtf8(m_check->readAllStandardOutput());
        m_check->deleteLater();
        m_check = nullptr;
        setUpdate(status == QProcess::NormalExit && code == 0 ? checkStatus(out) : u"unchecked"_s, {});
    });
    connect(m_check, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        m_check->deleteLater();
        m_check = nullptr;
        setUpdate(u"unchecked"_s, {});
    });
    m_check->start(u"systemctl"_s, {u"show"_s, u"-p"_s, u"ExecMainStatus"_s, u"-p"_s,
                                    u"ExecMainStartTimestampMonotonic"_s, u"microos-update-check.service"_s});
}

void UpdateInfo::setFixture(const QString &imageVersion, const QString &status, const QString &version)
{
    m_imageVersion = imageVersion;
    m_status = status;
    m_version = version;
    emit changed();
}

QString UpdateInfo::imageVersion() const
{
    return m_imageVersion;
}

QString UpdateInfo::status() const
{
    return m_status;
}

QString UpdateInfo::version() const
{
    return m_version;
}
