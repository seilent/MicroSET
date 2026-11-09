#include "clock.h"

#include <QDebug>
#include <QSocketNotifier>

#include <cerrno>
#include <cstring>
#include <ctime>

#include <sys/timerfd.h>
#include <unistd.h>

Clock::Clock(QObject *parent)
    : QObject(parent)
{
}

Clock::~Clock()
{
    if (m_fd >= 0)
        ::close(m_fd);
}

QString Clock::format(const QDateTime &now)
{
    const QTime time = now.time();
    return QStringLiteral("%1:%2").arg(time.hour()).arg(time.minute(), 2, 10, QLatin1Char('0'));
}

QString Clock::text() const
{
    return m_text;
}

int Clock::armCount() const
{
    return m_arms;
}

int Clock::cancelCount() const
{
    return m_cancels;
}

void Clock::start()
{
    if (m_fd >= 0)
        return;
    m_fd = ::timerfd_create(CLOCK_REALTIME, TFD_CLOEXEC | TFD_NONBLOCK);
    if (m_fd < 0) {
        qWarning("launcher: cannot create the clock timer: %s", std::strerror(errno));
        publish();
        return;
    }
    m_notifier = new QSocketNotifier(m_fd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &Clock::fire);
    publish();
    arm();
}

void Clock::arm()
{
    if (m_fd < 0)
        return;
    timespec now{};
    if (::clock_gettime(CLOCK_REALTIME, &now) != 0) {
        qWarning("launcher: cannot read the wall clock: %s", std::strerror(errno));
        return;
    }
    const qint64 seconds = qint64(now.tv_sec);
    const qint64 next = (seconds - seconds % 60) + 60;
    itimerspec spec{};
    spec.it_value.tv_sec = static_cast<time_t>(next);
    spec.it_value.tv_nsec = 0;
    if (::timerfd_settime(m_fd, TFD_TIMER_ABSTIME | TFD_TIMER_CANCEL_ON_SET, &spec, nullptr) != 0) {
        qWarning("launcher: cannot arm the clock timer: %s", std::strerror(errno));
        return;
    }
    ++m_arms;
}

void Clock::fire()
{
    quint64 expirations = 0;
    const ssize_t got = ::read(m_fd, &expirations, sizeof(expirations));
    if (got < 0 && errno == ECANCELED)
        ++m_cancels;
    else if (got < 0 && errno != EAGAIN)
        qWarning("launcher: clock timer read failed: %s", std::strerror(errno));
    publish();
    arm();
}

void Clock::publish()
{
    const QString now = Clock::format(QDateTime::currentDateTime());
    if (now == m_text)
        return;
    m_text = now;
    emit textChanged();
}
