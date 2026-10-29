#include "lsfgstate.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>

#include <cerrno>
#include <cmath>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

using namespace Qt::StringLiterals;

namespace {

QByteArray readFile(const QString &path)
{
    const int fd = ::open(QFile::encodeName(path).constData(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0)
        return {};
    struct stat st;
    QByteArray data;
    if (::fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_size <= LsfgState::MaxFile) {
        data.resize(qsizetype(LsfgState::MaxFile) + 1);
        qsizetype done = 0;
        while (done < data.size()) {
            const ssize_t n = ::read(fd, data.data() + done, size_t(data.size() - done));
            if (n < 0 && errno == EINTR)
                continue;
            if (n <= 0)
                break;
            done += n;
        }
        data.resize(done > LsfgState::MaxFile ? 0 : done);
    }
    ::close(fd);
    return data;
}

QString existing(const QString &path)
{
    QString dir = QFileInfo(path).absolutePath();
    while (!QFileInfo(dir).isDir() && dir != QFileInfo(dir).absolutePath())
        dir = QFileInfo(dir).absolutePath();
    return dir;
}

double sendLeft(const QJsonObject &state, const QString &boot, double now)
{
    const QJsonObject send = state.value("send"_L1).toObject();
    const QJsonValue mono = send.value("last_mono"_L1);
    if (boot.isEmpty() || send.value("last_boot"_L1).toString() != boot || !mono.isDouble() || mono.toDouble() < 0
        || now < mono.toDouble())
        return 0;
    return LsfgState::SendGap - (now - mono.toDouble());
}

}

LsfgState::LsfgState(QObject *parent)
    : QObject(parent)
{
    connect(&m_watch, &QFileSystemWatcher::directoryChanged, this, &LsfgState::watch);
    connect(&m_watch, &QFileSystemWatcher::fileChanged, this, &LsfgState::watch);
    m_sent.setSingleShot(true);
    m_sent.setTimerType(Qt::PreciseTimer);
    connect(&m_sent, &QTimer::timeout, this, [this] {
        expireSend();
        emit changed();
    });
}

QString LsfgState::stateFile()
{
    const QString set = qEnvironmentVariable("MICROSET_LSFG_STATE");
    if (!set.isEmpty())
        return set;
    const QString xdg = qEnvironmentVariable("XDG_STATE_HOME");
    const QString base = xdg.startsWith(u'/') ? xdg : QDir::homePath() + u"/.local/state"_s;
    return base + u"/microos-lsfg/state.json"_s;
}

QString LsfgState::sessionFile()
{
    return qEnvironmentVariable("MICROSET_SESSION_CURRENT", u"/run/microos/session-current"_s);
}

QString LsfgState::bootId()
{
    static const QString id = QString::fromLatin1(readFile(u"/proc/sys/kernel/random/boot_id"_s)).trimmed();
    return id;
}

double LsfgState::monotonic()
{
    timespec now{};
    ::clock_gettime(CLOCK_MONOTONIC, &now);
    return double(now.tv_sec) + double(now.tv_nsec) / 1e9;
}

QString LsfgState::status(const QJsonObject &state, bool known, const QString &boot, double now)
{
    if (!known)
        return u"ownership unknown"_s;
    if (state.value("user_layer"_L1).toBool())
        return u"old layer in home"_s;
    const QString install = state.value("install"_L1).toString();
    if (install == "installed"_L1) {
        if (state.value("branch_ok"_L1) == QJsonValue(false))
            return u"switch to the lsfg-vk beta"_s;
        return state.value("library"_L1).toString().startsWith("/run/media/"_L1) ? u"on microSD"_s : u"installed"_s;
    }
    if (install == "installing"_L1)
        return u"installing"_s;
    const QString owned = state.value("owned"_L1).toString();
    if (owned != "yes"_L1)
        return owned == "no"_L1 ? u"not installed"_s : u"ownership unknown"_s;
    const QString result = state.value("send"_L1).toObject().value("result"_L1).toString();
    if (result == "sent"_L1 && sendLeft(state, boot, now) > 0)
        return u"install sent"_s;
    if (result == "no-client"_L1)
        return u"no Steam client"_s;
    if (result == "game-running"_L1)
        return u"game running"_s;
    if (result == "ownership-unknown"_L1)
        return u"ownership unknown"_s;
    return u"not installed"_s;
}

void LsfgState::watch()
{
    read();
    if (watchDirs())
        read();
}

bool LsfgState::watchDirs()
{
    QStringList wanted{existing(stateFile()), existing(sessionFile())};
    for (const QString &file : {stateFile(), sessionFile()})
        if (QFileInfo(file).isFile())
            wanted << file;
    wanted.removeDuplicates();
    wanted.sort();
    QStringList watched = m_watch.directories() + m_watch.files();
    watched.sort();
    if (watched == wanted)
        return false;
    if (!watched.isEmpty())
        m_watch.removePaths(watched);
    m_watch.addPaths(wanted);
    return true;
}

void LsfgState::read()
{
    const QByteArray data = readFile(stateFile());
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(data, &error);
    const bool known = error.error == QJsonParseError::NoError && document.isObject();
    const QJsonObject state = known ? document.object() : QJsonObject();
    const QString session = QString::fromUtf8(readFile(sessionFile())).simplified().section(u' ', 0, 0);
    if (known == m_known && state == m_state && session == m_session)
        return;
    m_known = known;
    m_state = state;
    m_session = session;
    expireSend();
    emit changed();
}

void LsfgState::expireSend()
{
    const double left = sendLeft(m_state, bootId(), monotonic());
    if (left > 0)
        m_sent.start(int(std::ceil(left * 1000)) + 1);
    else
        m_sent.stop();
}

void LsfgState::setFixture(const QJsonObject &state, bool known, const QString &session)
{
    m_state = state;
    m_known = known;
    m_session = session;
    expireSend();
    emit changed();
}

void LsfgState::install()
{
    if (m_install)
        return;
    m_install = new QProcess(this);
    m_install->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_install, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        if (status != QProcess::NormalExit || code != 0)
            qWarning("microset settings: microos-lsfg-send@manual.service did not start: %s",
                     qPrintable(QString::fromUtf8(m_install->readAll()).trimmed().left(256)));
        m_install->deleteLater();
        m_install = nullptr;
    });
    connect(m_install, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        qWarning("microset settings: cannot run systemctl: %s", qPrintable(m_install->errorString()));
        m_install->deleteLater();
        m_install = nullptr;
    });
    m_install->start(u"systemctl"_s,
                     {u"--user"_s, u"start"_s, u"--job-mode=fail"_s, u"microos-lsfg-send@manual.service"_s});
}

QJsonObject LsfgState::state() const
{
    return m_state;
}

bool LsfgState::known() const
{
    return m_known;
}

QString LsfgState::session() const
{
    return m_session;
}

QString LsfgState::status() const
{
    return status(m_state, m_known, bootId(), monotonic());
}
