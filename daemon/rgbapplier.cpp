#include "rgbapplier.h"

#include "sleepinhibitor.h"

#include <microset/color.h>

#include <QFile>
#include <QSocketNotifier>

#include <cerrno>
#include <cstring>
#include <optional>

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

using namespace Qt::StringLiterals;

namespace {

const QString Enabled = u"rgb.enabled"_s;
const QString Hue = u"rgb.hue"_s;
const QString Saturation = u"rgb.saturation"_s;
const QString Brightness = u"rgb.brightness"_s;

std::optional<quint64> number(const QByteArray &text)
{
    bool ok = false;
    const quint64 value = text.trimmed().toULongLong(&ok);
    return ok ? std::optional(value) : std::nullopt;
}

std::optional<quint64> readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    return number(file.read(32));
}

}

RgbApplier::RgbApplier(Settings &settings, SleepInhibitor *sleep, const QString &device, const QString &backlight,
                       QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_sleep(sleep)
    , m_device(device)
    , m_backlight(backlight)
{
    m_repeat.setSingleShot(true);
    m_repeat.setTimerType(Qt::PreciseTimer);
    m_repeat.setInterval(repeatMs);
    connect(&m_repeat, &QTimer::timeout, this, &RgbApplier::writeNext);
    m_recompute.setSingleShot(true);
    m_recompute.setInterval(0);
    connect(&m_recompute, &QTimer::timeout, this, &RgbApplier::recompute);
    m_power.setSingleShot(true);
    m_power.setInterval(powerMs);
    connect(&m_power, &QTimer::timeout, this, &RgbApplier::recompute);
}

RgbApplier::~RgbApplier()
{
    closeDevice();
    if (m_screenFd >= 0)
        ::close(m_screenFd);
}

QByteArray RgbApplier::frame(uint rgb)
{
    const quint8 r = quint8(rgb >> 16);
    const quint8 g = quint8(rgb >> 8);
    const quint8 b = quint8(rgb);
    const quint8 check = quint8((0x01 + r + g + b) & 0xff);
    const char bytes[] = {char(0xf7), 0x01, char(r), char(g), char(b), 0, 0, 0, 0, char(check), char(0xed)};
    return QByteArray(bytes, sizeof(bytes));
}

QByteArray RgbApplier::wanted() const
{
    if (!m_settings.value(Enabled).toBool() || m_powerOff)
        return frame(0);
    const uint color = MicroSet::hueColor(m_settings.value(Hue).toInt(), m_settings.value(Saturation).toInt());
    const quint64 cap = qMin(m_settings.value(Brightness).toUInt(), 100u);
    if (m_screenFd < 0)
        return frame(MicroSet::scale(color, cap, 100));
    return frame(MicroSet::scale(color, m_screen * cap, m_max * 100));
}

bool RgbApplier::watchingPower() const
{
    return m_power.isActive();
}

void RgbApplier::start()
{
    connect(&m_settings, &Settings::changed, this, [this](const QString &key) {
        if (key == Enabled || key == Hue || key == Saturation || key == Brightness)
            schedule();
    });
    if (m_sleep)
        connect(m_sleep, &SleepInhibitor::prepareForSleep, this, &RgbApplier::onPrepareForSleep);
    recompute();
}

void RgbApplier::refresh()
{
    if (m_screenFd >= 0)
        readScreen();
    schedule();
}

void RgbApplier::schedule()
{
    if (!m_recompute.isActive())
        m_recompute.start();
}

void RgbApplier::recompute()
{
    m_recompute.stop();
    if (m_screenFd < 0)
        openBacklight();
    readPower();
    if (m_asleep)
        return;
    const QByteArray next = wanted();
    if (next == m_frame && m_left == 0)
        return;
    send(next);
}

void RgbApplier::readPower()
{
    m_powerOff = readFile(m_backlight + u"/bl_power"_s).value_or(0) != 0;
    if (!m_powerOff)
        m_power.stop();
    else if (!m_power.isActive())
        m_power.start();
}

bool RgbApplier::openBacklight()
{
    const QString max = m_backlight + u"/max_brightness"_s;
    const std::optional<quint64> top = readFile(max);
    if (!top || *top == 0) {
        warn(m_lightWarned, u"rgb: cannot read %1, LEDs at full scale"_s.arg(max));
        return false;
    }
    const QString path = m_backlight + u"/actual_brightness"_s;
    const int fd = ::open(QFile::encodeName(path).constData(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        const QString reason = QString::fromLocal8Bit(std::strerror(errno));
        warn(m_lightWarned, u"rgb: cannot open %1: %2, LEDs at full scale"_s.arg(path, reason));
        return false;
    }
    m_screenFd = fd;
    m_max = *top;
    if (!readScreen())
        return false;
    m_notifier = new QSocketNotifier(m_screenFd, QSocketNotifier::Exception, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &RgbApplier::refresh);
    return true;
}

bool RgbApplier::readScreen()
{
    char buffer[32];
    ssize_t got = -1;
    if (::lseek(m_screenFd, 0, SEEK_SET) == 0)
        got = ::read(m_screenFd, buffer, sizeof buffer);
    const std::optional<quint64> value = got > 0 ? number(QByteArray(buffer, got)) : std::nullopt;
    if (!value) {
        closeBacklight(got < 0 ? QString::fromLocal8Bit(std::strerror(errno)) : u"not a number"_s);
        return false;
    }
    m_screen = qMin(*value, m_max);
    m_lightWarned = false;
    return true;
}

void RgbApplier::closeBacklight(const QString &reason)
{
    warn(m_lightWarned, u"rgb: cannot read %1/actual_brightness: %2, LEDs at full scale"_s.arg(m_backlight, reason));
    if (m_notifier) {
        m_notifier->setEnabled(false);
        m_notifier->deleteLater();
        m_notifier = nullptr;
    }
    ::close(m_screenFd);
    m_screenFd = -1;
}

void RgbApplier::onPrepareForSleep(bool start)
{
    m_asleep = start;
    if (start) {
        const QByteArray off = frame(0);
        if (m_frame == off && m_left == 0)
            return;
        send(off);
        if (m_left > 0) {
            m_deferred = true;
            m_sleep->defer();
        }
        return;
    }
    m_deferred = false;
    schedule();
}

void RgbApplier::send(const QByteArray &frame)
{
    m_frame = frame;
    m_left = repeats;
    m_tries = 0;
    if (!m_repeat.isActive())
        writeNext();
}

void RgbApplier::writeNext()
{
    if (m_left == 0)
        return;
    if (m_fd < 0 && !openDevice()) {
        m_frame.clear();
        finish();
        return;
    }
    const ssize_t written = ::write(m_fd, m_frame.constData(), size_t(m_frame.size()));
    if (written != m_frame.size()) {
        const bool busy = written >= 0 || errno == EAGAIN;
        const QString reason = written < 0 ? QString::fromLocal8Bit(std::strerror(errno)) : u"short write"_s;
        warn(m_warned, u"rgb: cannot write %1: %2"_s.arg(m_device, reason));
        if (busy && ++m_tries < maxTries) {
            m_repeat.start();
            return;
        }
        closeDevice();
        m_frame.clear();
        finish();
        return;
    }
    m_tries = 0;
    m_warned = false;
    m_repeat.start();
    if (--m_left == 0)
        finish();
}

void RgbApplier::finish()
{
    m_left = 0;
    if (!m_deferred)
        return;
    m_deferred = false;
    if (m_fd >= 0)
        ::tcdrain(m_fd);
    m_sleep->ready();
}

bool RgbApplier::openDevice()
{
    const int fd = ::open(QFile::encodeName(m_device).constData(), O_WRONLY | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        warn(m_warned, u"rgb: cannot open %1: %2"_s.arg(m_device, QString::fromLocal8Bit(std::strerror(errno))));
        return false;
    }
    termios tty{};
    if (::tcgetattr(fd, &tty) < 0) {
        warn(m_warned, u"rgb: %1 is not a tty: %2"_s.arg(m_device, QString::fromLocal8Bit(std::strerror(errno))));
        ::close(fd);
        return false;
    }
    ::cfmakeraw(&tty);
    ::cfsetispeed(&tty, B115200);
    ::cfsetospeed(&tty, B115200);
    tty.c_cflag &= ~tcflag_t(CRTSCTS);
    tty.c_cflag |= CLOCAL | CREAD;
    if (::tcsetattr(fd, TCSANOW, &tty) < 0) {
        warn(m_warned, u"rgb: cannot set up %1: %2"_s.arg(m_device, QString::fromLocal8Bit(std::strerror(errno))));
        ::close(fd);
        return false;
    }
    m_fd = fd;
    return true;
}

void RgbApplier::closeDevice()
{
    if (m_fd < 0)
        return;
    ::close(m_fd);
    m_fd = -1;
}

void RgbApplier::warn(bool &flag, const QString &line)
{
    if (flag)
        return;
    flag = true;
    qWarning("%s", qPrintable(line));
}
