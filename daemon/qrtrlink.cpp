#include "qrtrlink.h"

#include <QSocketNotifier>

#include <cerrno>
#include <cstring>

#include <linux/qrtr.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

static_assert(sizeof(sockaddr_qrtr) == 12);

constexpr qsizetype bufferSize = 65536;

}

QrtrLink::QrtrLink(QObject *parent)
    : SscLink(parent)
    , m_buffer(bufferSize, Qt::Uninitialized)
{
}

QrtrLink::~QrtrLink()
{
    closeSocket();
}

bool QrtrLink::open() const
{
    return m_fd >= 0;
}

quint32 QrtrLink::localNode() const
{
    return m_node;
}

bool QrtrLink::send(quint32 node, quint32 port, const QByteArray &bytes)
{
    if (m_fd < 0)
        return false;
    sockaddr_qrtr addr{};
    addr.sq_family = AF_QIPCRTR;
    addr.sq_node = node;
    addr.sq_port = port;
    const ssize_t sent = ::sendto(m_fd, bytes.constData(), size_t(bytes.size()), 0,
                                  reinterpret_cast<const sockaddr *>(&addr), sizeof(addr));
    if (sent < 0) {
        qWarning("ssc: sendto failed: %s", std::strerror(errno));
        return false;
    }
    return true;
}

bool QrtrLink::reopen()
{
    closeSocket();
    return openSocket();
}

bool QrtrLink::openSocket()
{
    const int fd = ::socket(AF_QIPCRTR, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        qWarning("ssc: socket unavailable: %s", std::strerror(errno));
        return false;
    }
    sockaddr_qrtr addr{};
    socklen_t len = sizeof(addr);
    if (::getsockname(fd, reinterpret_cast<sockaddr *>(&addr), &len) < 0) {
        qWarning("ssc: getsockname failed: %s", std::strerror(errno));
        ::close(fd);
        return false;
    }
    m_fd = fd;
    m_node = addr.sq_node;
    m_notifier = new QSocketNotifier(m_fd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &QrtrLink::drain);
    return true;
}

void QrtrLink::closeSocket()
{
    if (m_notifier) {
        m_notifier->setEnabled(false);
        m_notifier->deleteLater();
        m_notifier = nullptr;
    }
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
}

void QrtrLink::drain()
{
    while (m_fd >= 0) {
        sockaddr_qrtr addr{};
        socklen_t len = sizeof(addr);
        const ssize_t got = ::recvfrom(m_fd, m_buffer.data(), size_t(m_buffer.size()), 0,
                                       reinterpret_cast<sockaddr *>(&addr), &len);
        if (got < 0) {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return;
            qWarning("ssc: recvfrom failed: %s", std::strerror(errno));
            closeSocket();
            emit failed();
            return;
        }
        emit received(addr.sq_node, addr.sq_port, QByteArray(m_buffer.constData(), got));
    }
}
