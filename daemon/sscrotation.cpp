#include "sscrotation.h"

#include "sleepinhibitor.h"
#include "ssclink.h"

#include <algorithm>

namespace {

QByteArray dataType()
{
    return QByteArrayLiteral("device_orient");
}

QString suidHex(const ssc::Suid &suid)
{
    return QStringLiteral("%1%2").arg(suid.high, 16, 16, QLatin1Char('0')).arg(suid.low, 16, 16, QLatin1Char('0'));
}

}

SscRotation::SscRotation(SscLink *link, SleepInhibitor *sleep, int retry, int hold, QObject *parent)
    : QObject(parent)
    , m_link(link)
    , m_sleep(sleep)
    , m_retryBase(retry)
    , m_retryDelay(retry)
    , m_enableDelay(retry)
    , m_reopenDelay(retry)
{
    m_link->setParent(this);
    m_settle.setSingleShot(true);
    m_settle.setInterval(settleMs);
    connect(&m_settle, &QTimer::timeout, this, &SscRotation::settle);
    m_hold.setSingleShot(true);
    m_hold.setInterval(hold);
    connect(&m_hold, &QTimer::timeout, this, &SscRotation::lose);
    m_retry.setSingleShot(true);
    connect(&m_retry, &QTimer::timeout, this, &SscRotation::requestSuid);
    m_enableRetry.setSingleShot(true);
    connect(&m_enableRetry, &QTimer::timeout, this, &SscRotation::maybeEnable);
    m_reopen.setSingleShot(true);
    connect(&m_reopen, &QTimer::timeout, this, &SscRotation::onReopen);
    m_sleepRelease.setSingleShot(true);
    m_sleepRelease.setInterval(sleepReleaseMs);
    connect(&m_sleepRelease, &QTimer::timeout, this, &SscRotation::releaseSleep);
    connect(m_link, &SscLink::received, this, &SscRotation::onReceived);
    connect(m_link, &SscLink::failed, this, &SscRotation::onLinkFailed);
    if (m_sleep)
        connect(m_sleep, &SleepInhibitor::prepareForSleep, this, &SscRotation::onPrepareForSleep);
}

SscRotation::~SscRotation()
{
    if (m_enableTxn)
        disable();
}

bool SscRotation::supported() const
{
    return m_supported;
}

int SscRotation::turn() const
{
    return m_turn;
}

int SscRotation::raw() const
{
    return m_raw;
}

bool SscRotation::wanted() const
{
    return m_wanted;
}

bool SscRotation::claimed() const
{
    return m_claimed;
}

void SscRotation::setWanted(bool wanted)
{
    if (!wanted)
        m_last = -1;
    if (m_wanted == wanted)
        return;
    m_wanted = wanted;
    if (wanted) {
        if (!m_begun)
            begin();
        maybeEnable();
        return;
    }
    m_settle.stop();
    m_enableRetry.stop();
    m_enableDelay = m_retryBase;
    if (m_holding) {
        m_hold.stop();
        m_holding = false;
        updateSupported();
    }
    if (m_enableTxn)
        disable();
    m_claimed = false;
    setTurn(-1);
}

void SscRotation::begin()
{
    if (!m_link->open() && !m_link->reopen()) {
        backoff(m_reopen, m_reopenDelay);
        return;
    }
    m_begun = true;
    m_reopen.stop();
    m_reopenDelay = m_retryBase;
    sendLookup();
}

void SscRotation::onReceived(quint32 node, quint32 port, const QByteArray &bytes)
{
    if (port == ssc::controlPort) {
        handleControl(bytes);
        return;
    }
    if (!m_server || node != m_server->node || port != m_server->port)
        return;
    const std::optional<ssc::Message> message = ssc::parse(bytes);
    if (!message)
        return;
    if (message->type == ssc::typeResponse && message->msgId == ssc::msgControl) {
        handleResponse(*message);
        return;
    }
    if (message->type != ssc::typeIndication
        || (message->msgId != ssc::msgReportSmall && message->msgId != ssc::msgReportLarge))
        return;
    const std::optional<QByteArrayView> data = message->data();
    if (!data)
        return;
    const std::optional<ssc::Report> report = ssc::parseReport(*data);
    if (report)
        handleReport(*report);
}

void SscRotation::onLinkFailed()
{
    dropServer();
    backoff(m_reopen, m_reopenDelay);
}

void SscRotation::onReopen()
{
    if (!m_begun) {
        if (m_wanted)
            begin();
        return;
    }
    if (!m_link->reopen()) {
        backoff(m_reopen, m_reopenDelay);
        return;
    }
    m_reopenDelay = m_retryBase;
    sendLookup();
}

void SscRotation::onPrepareForSleep(bool start)
{
    if (start) {
        m_asleep = true;
        m_enableRetry.stop();
        if (m_enableTxn) {
            m_settle.stop();
            m_sleepTxn = disable();
            if (m_sleepTxn) {
                m_deferred = true;
                m_sleep->defer();
                m_sleepRelease.start();
            }
        }
        return;
    }
    m_asleep = false;
    m_deferred = false;
    m_sleepRelease.stop();
    m_sleepTxn = 0;
    maybeEnable();
}

void SscRotation::handleControl(const QByteArray &bytes)
{
    const std::optional<ssc::Control> control = ssc::parseControl(bytes);
    if (!control || control->service != ssc::service)
        return;
    if (control->cmd == ssc::newServer && !m_server) {
        m_server = control;
        m_retryDelay = m_retryBase;
        m_enableDelay = m_retryBase;
        qInfo("ssc: service on node %u port %u", control->node, control->port);
        requestSuid();
        return;
    }
    if (control->cmd == ssc::delServer && m_server && control->node == m_server->node
        && control->port == m_server->port && control->instance == m_server->instance)
        dropServer();
}

void SscRotation::handleResponse(const ssc::Message &message)
{
    const auto entry = m_requests.constFind(message.txn);
    if (entry == m_requests.constEnd())
        return;
    const Request kind = entry.value();
    m_requests.erase(entry);
    const std::optional<ssc::Result> result = message.result();
    const bool ok = result && result->result == 0;
    if (kind == Request::Enable) {
        if (message.txn != m_enableTxn)
            return;
        if (!ok) {
            qWarning("ssc: enable failed %u", result ? result->error : 0u);
            m_enableTxn = 0;
            enableFailed();
            return;
        }
        m_claimed = true;
        m_enableDelay = m_retryBase;
        qInfo("ssc: enabled");
        setTurn(m_last);
        send(Request::Flush, ssc::request(*m_suid, ssc::msgFlush, std::nullopt));
        return;
    }
    if (!ok)
        qWarning("ssc: request failed %u", result ? result->error : 0u);
    if (kind == Request::Suid && !ok && !m_suid)
        scheduleRetry();
    if (kind == Request::Disable && m_sleepTxn && message.txn == m_sleepTxn)
        releaseSleep();
}

void SscRotation::handleReport(const ssc::Report &report)
{
    const bool lookup = report.suid == ssc::lookupSuid;
    if (!lookup && !(m_suid && report.suid == *m_suid))
        return;
    for (const ssc::Event &event : report.events) {
        if (event.msgId == ssc::msgError) {
            qWarning("ssc: error %llu", ssc::uintField(event.payload, 1).value_or(0));
            continue;
        }
        if (lookup && event.msgId == ssc::msgSuidEvent)
            handleSuid(event.payload);
        else if (!lookup && event.msgId == ssc::msgOrientEvent)
            handleValue(event.payload);
    }
}

void SscRotation::handleSuid(const QByteArray &payload)
{
    if (m_suid)
        return;
    QByteArray type;
    std::optional<ssc::Suid> suid;
    for (const ssc::Field &field : ssc::fields(payload)) {
        if (field.type != ssc::WireType::Bytes)
            continue;
        if (field.id == 1)
            type = field.bytes.toByteArray();
        else if (field.id == 2 && !suid)
            suid = ssc::parseSuid(field.bytes);
    }
    if (type != dataType())
        return;
    if (!suid) {
        scheduleRetry();
        return;
    }
    m_suid = suid;
    m_retry.stop();
    m_retryDelay = m_retryBase;
    qInfo("ssc: device_orient %s", qPrintable(suidHex(*suid)));
    m_hold.stop();
    m_holding = false;
    updateSupported();
    maybeEnable();
}

void SscRotation::handleValue(const QByteArray &payload)
{
    if (!m_claimed)
        return;
    const std::optional<quint64> value = ssc::uintField(payload, 1);
    if (!value)
        return;
    qInfo("ssc: value %llu", *value);
    if (*value <= 3)
        take(int(*value));
}

void SscRotation::sendLookup()
{
    if (!m_link->send(m_link->localNode(), ssc::controlPort, ssc::lookup(ssc::service)))
        qWarning("ssc: lookup failed");
}

quint16 SscRotation::send(Request kind, const QByteArray &request)
{
    if (!m_server)
        return 0;
    m_txn = m_txn == 0xffff ? 1 : quint16(m_txn + 1);
    if (!m_link->send(m_server->node, m_server->port, ssc::control(m_txn, request)))
        return 0;
    m_requests.insert(m_txn, kind);
    return m_txn;
}

void SscRotation::requestSuid()
{
    if (!m_wanted || !m_server || m_suid)
        return;
    if (!send(Request::Suid, ssc::suidRequest(dataType())))
        scheduleRetry();
}

bool SscRotation::suidPending() const
{
    return m_retry.isActive() || std::find(m_requests.cbegin(), m_requests.cend(), Request::Suid) != m_requests.cend();
}

void SscRotation::scheduleRetry()
{
    backoff(m_retry, m_retryDelay);
}

void SscRotation::backoff(QTimer &timer, int &delay)
{
    timer.start(delay);
    delay = qMin(delay * 2, maxRetryMs);
}

void SscRotation::enableFailed()
{
    setTurn(-1);
    backoff(m_enableRetry, m_enableDelay);
}

void SscRotation::maybeEnable()
{
    if (!m_wanted || !m_server || m_asleep || m_enableTxn)
        return;
    if (!m_suid) {
        if (!suidPending())
            requestSuid();
        return;
    }
    m_enableTxn = send(Request::Enable, ssc::request(*m_suid, ssc::msgOnChange, QByteArray()));
    if (!m_enableTxn)
        enableFailed();
}

quint16 SscRotation::disable()
{
    quint16 txn = 0;
    if (m_suid)
        txn = send(Request::Disable, ssc::request(*m_suid, ssc::msgDisable, std::nullopt));
    m_enableTxn = 0;
    m_claimed = false;
    qInfo("ssc: released");
    return txn;
}

void SscRotation::dropServer()
{
    const bool held = m_supported || m_claimed;
    m_server.reset();
    m_suid.reset();
    m_enableTxn = 0;
    m_claimed = false;
    m_requests.clear();
    m_settle.stop();
    m_retry.stop();
    m_enableRetry.stop();
    if (m_wanted && held) {
        m_holding = true;
        m_hold.start();
        updateSupported();
        return;
    }
    lose();
}

void SscRotation::releaseSleep()
{
    m_sleepRelease.stop();
    m_sleepTxn = 0;
    if (!m_deferred)
        return;
    m_deferred = false;
    m_sleep->ready();
}

void SscRotation::lose()
{
    m_hold.stop();
    m_holding = false;
    m_settle.stop();
    m_last = -1;
    setTurn(-1);
    updateSupported();
}

void SscRotation::take(int value)
{
    m_raw = value;
    if (value == m_turn) {
        m_settle.stop();
        return;
    }
    if (value == m_pending && m_settle.isActive())
        return;
    m_pending = value;
    m_settle.start();
}

void SscRotation::settle()
{
    if (!m_claimed)
        return;
    m_last = m_pending;
    setTurn(m_pending);
}

void SscRotation::updateSupported()
{
    const bool supported = m_suid.has_value() || m_holding;
    if (m_supported == supported)
        return;
    m_supported = supported;
    emit supportedChanged();
}

void SscRotation::setTurn(int turn)
{
    if (m_turn == turn)
        return;
    m_turn = turn;
    emit turnChanged();
}
