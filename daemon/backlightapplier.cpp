#include "backlightapplier.h"

#include <QFile>
#include <QFileInfo>
#include <QSocketNotifier>

#include <cerrno>
#include <cstring>
#include <optional>

#include <fcntl.h>
#include <unistd.h>

using namespace Qt::StringLiterals;

namespace {

std::optional<quint64> number(const QByteArray &text)
{
    bool ok = false;
    const quint64 value = text.trimmed().toULongLong(&ok);
    return ok ? std::optional(value) : std::nullopt;
}

std::optional<quint64> readFd(int fd)
{
    char buffer[32];
    if (::lseek(fd, 0, SEEK_SET) != 0)
        return std::nullopt;
    const ssize_t got = ::read(fd, buffer, sizeof buffer);
    return got > 0 ? number(QByteArray(buffer, got)) : std::nullopt;
}

QString reason()
{
    return QString::fromLocal8Bit(std::strerror(errno));
}

}

BacklightApplier::BacklightApplier(Settings &settings, const QString &backlight, const QString &runtime,
                                   QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_backlight(backlight)
    , m_marker(runtime + u"/backlight"_s)
{
}

BacklightApplier::~BacklightApplier()
{
    if (m_watchFd >= 0)
        ::close(m_watchFd);
}

quint64 BacklightApplier::raw(uint basisPoints, quint64 max)
{
    return qMax<quint64>(1, (quint64(basisPoints) * max + 5000) / 10000);
}

uint BacklightApplier::basisPoints(quint64 raw, quint64 max)
{
    if (max == 0)
        return 1;
    return uint(qBound<quint64>(1, (raw * 10000 + max / 2) / max, 10000));
}

uint BacklightApplier::floor() const
{
    const Key *key = m_settings.schema().key(Name);
    return key && key->floorValue ? uint(*key->floorValue) : 1;
}

void BacklightApplier::start()
{
    if (!m_settings.schema().key(Name))
        return;
    QFile maxFile(m_backlight + u"/max_brightness"_s);
    const std::optional<quint64> max = maxFile.open(QIODevice::ReadOnly) ? number(maxFile.read(32)) : std::nullopt;
    if (!max || *max == 0) {
        qWarning("backlight: cannot read %s, brightness not applied", qPrintable(maxFile.fileName()));
        return;
    }
    m_max = *max;
    connect(&m_settings, &Settings::changed, this, [this](const QString &key, const QVariant &value) {
        if (key == Name)
            write(raw(value.toUInt(), m_max), false);
    });
    openWatch();
    const bool boot = !QFileInfo::exists(m_marker);
    const QVariant stored = m_settings.stored(Name);
    if (stored.isValid()) {
        const uint level = stored.toUInt();
        if (boot && level < floor()) {
            qInfo("backlight: stored %u is under the floor, boot at %u", level, floor());
            m_settings.store(Name, floor());
        } else {
            write(raw(level, m_max), true);
        }
    } else if (m_watchFd >= 0) {
        const std::optional<quint64> now = readFd(m_watchFd);
        if (now && *now > 0) {
            m_last = *now;
            m_ok = true;
            m_settings.store(Name, basisPoints(*now, m_max));
        }
    }
    if (!boot)
        return;
    QFile marker(m_marker);
    if (!marker.open(QIODevice::WriteOnly))
        qWarning("backlight: cannot write %s, the next start counts as a boot", qPrintable(m_marker));
}

bool BacklightApplier::apply(uint basisPoints)
{
    return write(raw(basisPoints, m_max), false);
}

bool BacklightApplier::write(quint64 level, bool force)
{
    if (m_max == 0)
        return false;
    if (!force && m_ok && level == m_last)
        return true;
    m_last = level;
    const QString path = m_backlight + u"/brightness"_s;
    const QByteArray text = QByteArray::number(level) + '\n';
    const int fd = ::open(QFile::encodeName(path).constData(), O_WRONLY | O_TRUNC | O_CLOEXEC);
    m_ok = fd >= 0 && ::write(fd, text.constData(), size_t(text.size())) == text.size();
    const QString why = m_ok ? QString() : reason();
    if (fd >= 0)
        ::close(fd);
    if (m_ok)
        m_writeWarned = false;
    else
        warn(m_writeWarned, u"backlight: cannot write %1 to %2: %3"_s.arg(level).arg(path, why));
    return m_ok;
}

bool BacklightApplier::openWatch()
{
    const QString path = m_backlight + u"/actual_brightness"_s;
    const int fd = ::open(QFile::encodeName(path).constData(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        warn(m_readWarned, u"backlight: cannot open %1: %2, other writers are not recorded"_s.arg(path, reason()));
        return false;
    }
    m_watchFd = fd;
    m_notifier = new QSocketNotifier(m_watchFd, QSocketNotifier::Exception, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &BacklightApplier::refresh);
    return true;
}

void BacklightApplier::refresh()
{
    if (m_max == 0 || (m_watchFd < 0 && !openWatch()))
        return;
    const std::optional<quint64> now = readFd(m_watchFd);
    if (!now) {
        warn(m_readWarned, u"backlight: cannot read %1/actual_brightness"_s.arg(m_backlight));
        return;
    }
    m_readWarned = false;
    if (*now == m_last)
        return;
    const quint64 effective = raw(m_settings.value(Name).toUInt(), m_max);
    if (!m_ok && write(effective, true))
        return;
    if (*now == 0) {
        if (m_ok) {
            qInfo("backlight: 0 written by another process, %llu written back", effective);
            write(effective, true);
        }
        return;
    }
    qInfo("backlight: %llu written by another process, recorded", *now);
    m_last = *now;
    m_ok = true;
    m_settings.store(Name, basisPoints(*now, m_max));
}

void BacklightApplier::warn(bool &flag, const QString &line)
{
    if (flag)
        return;
    flag = true;
    qWarning("%s", qPrintable(line));
}
