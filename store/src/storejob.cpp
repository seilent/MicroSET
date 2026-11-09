#include "storejob.h"

#include <algorithm>

StoreJob::StoreJob(Kind kind, const QString &id, QObject *parent)
    : QObject(parent)
    , m_kind(kind)
    , m_id(id)
{
}

StoreJob::Kind StoreJob::kind() const
{
    return m_kind;
}

QString StoreJob::id() const
{
    return m_id;
}

StoreJob::State StoreJob::state() const
{
    return m_state;
}

int StoreJob::percent() const
{
    return m_percent;
}

QString StoreJob::message() const
{
    return m_message;
}

bool StoreJob::running() const
{
    return m_state == Queued || m_state == Downloading || m_state == Installing;
}

void StoreJob::start()
{
    if (m_state != Queued)
        return;
    setState(m_kind == Install ? Downloading : Installing);
}

void StoreJob::progress(int percent)
{
    if (m_state != Downloading || percent < 0)
        return;
    setPercent(std::clamp(percent, m_percent, 100));
}

void StoreJob::beginInstall()
{
    if (m_state != Downloading)
        return;
    setPercent(100);
    setState(Installing);
}

void StoreJob::finish()
{
    if (m_state != Downloading && m_state != Installing)
        return;
    setPercent(100);
    setState(Done);
}

void StoreJob::fail(const QString &message)
{
    if (!running())
        return;
    setMessage(message);
    setState(Failed);
}

void StoreJob::cancel()
{
    if (!running())
        return;
    setState(Cancelled);
}

void StoreJob::setMessage(const QString &message)
{
    if (m_message == message)
        return;
    m_message = message;
    emit messageChanged();
}

void StoreJob::setState(State state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged();
}

void StoreJob::setPercent(int percent)
{
    if (m_percent == percent)
        return;
    m_percent = percent;
    emit percentChanged();
}
