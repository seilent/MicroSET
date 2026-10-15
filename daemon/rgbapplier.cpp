#include "rgbapplier.h"

#include "sleepinhibitor.h"

#include <QFile>

#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

using namespace Qt::StringLiterals;

namespace {

const QString Enabled = u"rgb.enabled"_s;
const QString Color = u"rgb.color"_s;
const QString Brightness = u"rgb.brightness"_s;

}

RgbApplier::RgbApplier(Settings &settings, SleepInhibitor *sleep, const QString &device, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_sleep(sleep)
    , m_device(device)
{
    m_repeat.setSingleShot(true);
    m_repeat.setTimerType(Qt::PreciseTimer);
    m_repeat.setInterval(repeatMs);
    connect(&m_repeat, &QTimer::timeout, this, &RgbApplier::writeNext);
}

RgbApplier::~RgbApplier()
{
    closeDevice();
}

QByteArray RgbApplier::frame(uint color, uint brightness)
{
    const uint level = qMin(brightness, 255u);
    const auto scaled = [level](uint channel) { return quint8((channel & 0xff) * level / 255); };
    const quint8 r = scaled(color >> 16);
    const quint8 g = scaled(color >> 8);
    const quint8 b = scaled(color);
    const quint8 check = quint8((0x01 + r + g + b) & 0xff);
    const char bytes[] = {char(0xf7), 0x01, char(r), char(g), char(b), 0, 0, 0, 0, char(check), char(0xed)};
    return QByteArray(bytes, sizeof(bytes));
}

QByteArray RgbApplier::wanted() const
{
    if (!m_settings.value(Enabled).toBool())
        return frame(0, 0);
    return frame(m_settings.value(Color).toUInt(), m_settings.value(Brightness).toUInt());
}

void RgbApplier::start()
{
    connect(&m_settings, &Settings::changed, this, [this](const QString &key) {
        if ((key == Enabled || key == Color || key == Brightness) && !m_asleep)
            send(wanted());
    });
    if (m_sleep)
        connect(m_sleep, &SleepInhibitor::prepareForSleep, this, &RgbApplier::onPrepareForSleep);
    send(wanted());
}

void RgbApplier::onPrepareForSleep(bool start)
{
    m_asleep = start;
    if (start) {
        const QByteArray off = frame(0, 0);
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
    if (wanted() != m_frame)
        send(wanted());
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
        finish();
        return;
    }
    const ssize_t written = ::write(m_fd, m_frame.constData(), size_t(m_frame.size()));
    if (written != m_frame.size()) {
        const bool busy = written >= 0 || errno == EAGAIN;
        warn(u"rgb: cannot write %1: %2"_s.arg(m_device, written < 0 ? QString::fromLocal8Bit(std::strerror(errno))
                                                                       : u"short write"_s));
        if (busy && ++m_tries < maxTries) {
            m_repeat.start();
            return;
        }
        closeDevice();
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
        warn(u"rgb: cannot open %1: %2"_s.arg(m_device, QString::fromLocal8Bit(std::strerror(errno))));
        return false;
    }
    termios tty{};
    if (::tcgetattr(fd, &tty) < 0) {
        warn(u"rgb: %1 is not a tty: %2"_s.arg(m_device, QString::fromLocal8Bit(std::strerror(errno))));
        ::close(fd);
        return false;
    }
    ::cfmakeraw(&tty);
    ::cfsetispeed(&tty, B115200);
    ::cfsetospeed(&tty, B115200);
    tty.c_cflag &= ~tcflag_t(CRTSCTS);
    tty.c_cflag |= CLOCAL | CREAD;
    if (::tcsetattr(fd, TCSANOW, &tty) < 0) {
        warn(u"rgb: cannot set up %1: %2"_s.arg(m_device, QString::fromLocal8Bit(std::strerror(errno))));
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

void RgbApplier::warn(const QString &line)
{
    if (m_warned)
        return;
    m_warned = true;
    qWarning("%s", qPrintable(line));
}
