#include "store.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace Qt::StringLiterals;

namespace {

const QString FileName = u"settings.json"_s;
constexpr qint64 MaxSize = 1 << 20;

bool writeAll(int fd, const QByteArray &data)
{
    qsizetype done = 0;
    while (done < data.size()) {
        const ssize_t n = ::write(fd, data.constData() + done, size_t(data.size() - done));
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return false;
        done += n;
    }
    return true;
}

bool syncDir(const QString &dir)
{
    const int fd = ::open(QFile::encodeName(dir).constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0)
        return false;
    const bool ok = ::fsync(fd) == 0;
    ::close(fd);
    return ok;
}

}

Store::Store(const QString &dir)
    : m_dir(dir)
{
}

QString Store::path() const
{
    return m_dir + u'/' + FileName;
}

void Store::load()
{
    m_values = {};
    const int fd = ::open(QFile::encodeName(path()).constData(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        if (errno == ELOOP)
            moveAside(u"a symlink"_s);
        else if (errno != ENOENT)
            qWarning("%s: %s, starting on defaults", qPrintable(path()), strerror(errno));
        return;
    }
    struct stat st;
    QByteArray data;
    QString why;
    if (::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        why = u"not a regular file"_s;
    } else if (st.st_size > MaxSize) {
        why = u"too large"_s;
    } else {
        char buffer[65536];
        for (;;) {
            const ssize_t n = ::read(fd, buffer, sizeof buffer);
            if (n < 0 && errno == EINTR)
                continue;
            if (n < 0)
                why = QString::fromLocal8Bit(strerror(errno));
            if (n <= 0 || data.size() + n > MaxSize) {
                if (n > 0)
                    why = u"too large"_s;
                break;
            }
            data.append(buffer, n);
        }
    }
    ::close(fd);
    if (why.isEmpty()) {
        QJsonParseError error;
        const QJsonObject root = QJsonDocument::fromJson(data, &error).object();
        if (error.error != QJsonParseError::NoError)
            why = error.errorString();
        else if (root.value("version"_L1).toInteger() != 1 || !root.value("values"_L1).isObject())
            why = u"not a version 1 state file"_s;
        else
            m_values = root.value("values"_L1).toObject();
    }
    if (!why.isEmpty())
        moveAside(why);
}

void Store::moveAside(const QString &why)
{
    const QString bad = FileName + u".bad-"_s + QDateTime::currentDateTimeUtc().toString(u"yyyyMMddTHHmmssZ"_s);
    const QString target = m_dir + u'/' + bad;
    if (::rename(QFile::encodeName(path()).constData(), QFile::encodeName(target).constData()) != 0) {
        qWarning("%s: %s, cannot move it aside: %s, starting on defaults", qPrintable(path()), qPrintable(why),
                 strerror(errno));
        return;
    }
    const QStringList old = QDir(m_dir).entryList({FileName + u".bad-*"_s}, QDir::Files | QDir::System | QDir::Hidden);
    for (const QString &name : old)
        if (name != bad)
            ::unlink(QFile::encodeName(m_dir + u'/' + name).constData());
    syncDir(m_dir);
    qWarning("%s: %s, moved to %s, starting on defaults", qPrintable(path()), qPrintable(why), qPrintable(bad));
}

QJsonValue Store::value(const QString &key) const
{
    return m_values.value(key);
}

bool Store::contains(const QString &key) const
{
    return m_values.contains(key);
}

bool Store::set(const QString &key, const QJsonValue &value)
{
    return set(QJsonObject{{key, value}});
}

bool Store::set(const QJsonObject &changes)
{
    QJsonObject next = m_values;
    for (auto it = changes.begin(); it != changes.end(); ++it)
        next.insert(it.key(), it.value());
    if (!write(next))
        return false;
    m_values = next;
    return true;
}

bool Store::write(const QJsonObject &values)
{
    const QByteArray data = QJsonDocument(QJsonObject{{u"version"_s, 1}, {u"values"_s, values}}).toJson();
    if (data.size() > MaxSize) {
        qWarning("%s: state would exceed %lld bytes, not written", qPrintable(path()), MaxSize);
        return false;
    }
    QByteArray temp = QFile::encodeName(m_dir + u"/."_s + FileName + u".XXXXXX"_s);
    const int fd = ::mkostemp(temp.data(), O_CLOEXEC);
    if (fd < 0) {
        qWarning("%s: cannot create a temp file: %s", qPrintable(path()), strerror(errno));
        return false;
    }
    bool ok = ::fchmod(fd, 0600) == 0 && writeAll(fd, data) && ::fsync(fd) == 0;
    ok = ::close(fd) == 0 && ok;
    ok = ok && ::rename(temp.constData(), QFile::encodeName(path()).constData()) == 0;
    if (!ok) {
        qWarning("%s: write failed: %s", qPrintable(path()), strerror(errno));
        ::unlink(temp.constData());
        return false;
    }
    if (!syncDir(m_dir))
        qWarning("%s: fsync failed: %s", qPrintable(m_dir), strerror(errno));
    return true;
}
