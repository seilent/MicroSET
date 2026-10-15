#include "sleepinhibitor.h"

#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusUnixFileDescriptor>

#include <unistd.h>

using namespace Qt::StringLiterals;

SleepInhibitor::SleepInhibitor(const QDBusConnection &bus, const QString &why, QObject *parent)
    : QObject(parent)
    , m_bus(bus)
    , m_why(why)
{
    if (!m_bus.isConnected()) {
        qWarning("sleep: login1 bus not connected");
        return;
    }
    m_subscribed = m_bus.connect(service(), path(), interface(), u"PrepareForSleep"_s, this,
                                 SLOT(onPrepareForSleep(bool)));
    if (!m_subscribed)
        qWarning("sleep: cannot subscribe to PrepareForSleep: %s", qPrintable(m_bus.lastError().message()));
}

SleepInhibitor::~SleepInhibitor()
{
    release();
}

QString SleepInhibitor::service()
{
    return u"org.freedesktop.login1"_s;
}

QString SleepInhibitor::path()
{
    return u"/org/freedesktop/login1"_s;
}

QString SleepInhibitor::interface()
{
    return u"org.freedesktop.login1.Manager"_s;
}

bool SleepInhibitor::subscribed() const
{
    return m_subscribed;
}

bool SleepInhibitor::held() const
{
    return m_fd >= 0;
}

int SleepInhibitor::takeCount() const
{
    return m_takeCount;
}

void SleepInhibitor::take()
{
    if (m_fd >= 0 || m_taking || !m_bus.isConnected())
        return;
    QDBusMessage call = QDBusMessage::createMethodCall(service(), path(), interface(), u"Inhibit"_s);
    call << u"sleep"_s << u"microset"_s << m_why << u"delay"_s;
    m_taking = true;
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call, 2000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        watcher->deleteLater();
        m_taking = false;
        const QDBusMessage reply = watcher->reply();
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
            qWarning("sleep: Inhibit failed: %s", qPrintable(reply.errorMessage()));
            return;
        }
        if (m_sleeping) {
            qInfo("sleep: Inhibit answered after PrepareForSleep, dropped");
            return;
        }
        const auto descriptor = qvariant_cast<QDBusUnixFileDescriptor>(reply.arguments().constFirst());
        if (!descriptor.isValid()) {
            qWarning("sleep: Inhibit returned no descriptor");
            return;
        }
        m_fd = ::dup(descriptor.fileDescriptor());
        if (m_fd < 0) {
            qWarning("sleep: cannot keep the inhibitor descriptor");
            return;
        }
        ++m_takeCount;
        qInfo("sleep: delay inhibitor taken, %s", qPrintable(m_why));
        emit heldChanged();
    });
}

void SleepInhibitor::release()
{
    if (m_fd < 0)
        return;
    ::close(m_fd);
    m_fd = -1;
    qInfo("sleep: delay inhibitor released");
    emit heldChanged();
}

void SleepInhibitor::defer()
{
    if (m_sleeping)
        ++m_pending;
}

void SleepInhibitor::ready()
{
    if (!m_sleeping || m_pending == 0)
        return;
    if (--m_pending == 0)
        release();
}

void SleepInhibitor::onPrepareForSleep(bool start)
{
    qInfo("sleep: PrepareForSleep %s", start ? "true" : "false");
    m_sleeping = start;
    m_pending = 0;
    if (start) {
        emit prepareForSleep(true);
        if (m_pending == 0)
            release();
        return;
    }
    take();
    emit prepareForSleep(false);
}
