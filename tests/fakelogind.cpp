#include "fakelogind.h"

#include <QDBusMessage>
#include <QDBusUnixFileDescriptor>

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

using namespace Qt::StringLiterals;

namespace {

const QString Connection = u"fake-logind"_s;
const QString Path = u"/org/freedesktop/login1"_s;
const QString Manager = u"org.freedesktop.login1.Manager"_s;

}

FakeLogind::FakeLogind(const QString &address)
    : m_address(address)
{
}

FakeLogind::~FakeLogind()
{
    QDBusConnection(Connection).unregisterObject(Path);
    QDBusConnection::disconnectFromBus(Connection);
    for (const int fd : std::as_const(m_reads))
        ::close(fd);
}

bool FakeLogind::start()
{
    QDBusConnection bus = QDBusConnection::connectToBus(m_address, Connection);
    return bus.isConnected() && bus.registerVirtualObject(Path, this)
        && bus.registerService(u"org.freedesktop.login1"_s);
}

void FakeLogind::prepareForSleep(bool start)
{
    QDBusMessage signal = QDBusMessage::createSignal(Path, Manager, u"PrepareForSleep"_s);
    signal << start;
    QDBusConnection(Connection).send(signal);
}

int FakeLogind::open()
{
    for (qsizetype i = m_reads.size() - 1; i >= 0; --i) {
        pollfd entry{m_reads.at(i), POLLIN, 0};
        if (::poll(&entry, 1, 0) > 0 && (entry.revents & POLLHUP)) {
            ::close(m_reads.at(i));
            m_reads.removeAt(i);
        }
    }
    return int(m_reads.size());
}

QString FakeLogind::introspect(const QString &) const
{
    return {};
}

bool FakeLogind::handleMessage(const QDBusMessage &message, const QDBusConnection &connection)
{
    if (message.interface() != Manager || message.member() != "Inhibit"_L1) {
        connection.send(message.createErrorReply(u"org.freedesktop.DBus.Error.UnknownMethod"_s, message.member()));
        return true;
    }
    QStringList words;
    for (const QVariant &argument : message.arguments())
        words << argument.toString();
    inhibits << words.join(u'|');
    if (refuse) {
        connection.send(message.createErrorReply(u"org.freedesktop.DBus.Error.AccessDenied"_s, u"refused"_s));
        return true;
    }
    int fds[2];
    if (::pipe2(fds, O_CLOEXEC) < 0) {
        connection.send(message.createErrorReply(u"org.freedesktop.DBus.Error.Failed"_s, u"pipe"_s));
        return true;
    }
    m_reads << fds[0];
    connection.send(message.createReply(QVariant::fromValue(QDBusUnixFileDescriptor(fds[1]))));
    ::close(fds[1]);
    return true;
}
