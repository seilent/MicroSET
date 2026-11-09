#include "idlepulse.h"

IdlePulse::IdlePulse(QObject *parent)
    : QObject(parent)
{
}

void IdlePulse::pulse()
{
}

int IdlePulse::pulseCount() const
{
    return m_count;
}

void IdlePulse::reset()
{
    m_count = 0;
}

void IdlePulse::counted()
{
    ++m_count;
}
