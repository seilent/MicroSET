#include "fakealsa.h"

#include <QMetaObject>

using namespace Qt::StringLiterals;

FakeAlsa::FakeAlsa()
{
    const QStringList hphModes = {u"CLS_H_INVALID"_s, u"CLS_H_HIFI"_s, u"CLS_H_LP"_s,     u"CLS_AB"_s,       u"CLS_H_LOHIFI"_s,
                                  u"CLS_H_ULP"_s,     u"CLS_AB_HIFI"_s, u"CLS_AB_LP"_s, u"CLS_AB_LOHIFI"_s};
    controls = {
        {u"HPHL Volume"_s, {{u"20"_s}, {}, true}},
        {u"HPHR Volume"_s, {{u"20"_s}, {}, true}},
        {u"RX HPH Mode"_s, {{u"CLS_H_LOHIFI"_s}, hphModes, true}},
        {u"RX_HPH PWR Mode"_s, {{u"LOHIFI"_s}, {u"ULP"_s, u"LOHIFI"_s}, false}},
        {u"WSA_RX0 Digital Volume"_s, {{u"84"_s}, {}, true}},
        {u"WSA_RX1 Digital Volume"_s, {{u"84"_s}, {}, true}},
        {u"SpkrLeft PA Volume"_s, {{u"8"_s}, {}, true}},
        {u"SpkrRight PA Volume"_s, {{u"8"_s}, {}, true}},
    };
}

QString FakeAlsa::card() const
{
    return u"PocketMICRO2"_s;
}

bool FakeAlsa::open()
{
    if (!present)
        return false;
    m_open = true;
    ++opens;
    return true;
}

void FakeAlsa::close()
{
    m_open = false;
}

bool FakeAlsa::isOpen() const
{
    return m_open;
}

std::optional<QStringList> FakeAlsa::read(const QString &control)
{
    if (!m_open || !controls.contains(control))
        return std::nullopt;
    return controls.value(control).values;
}

bool FakeAlsa::write(const QString &control, const QString &value)
{
    if (!m_open || broken || !set(control, value))
        return false;
    writes << control + u'=' + value;
    return true;
}

void FakeAlsa::foreign(const QString &control, const QString &value)
{
    set(control, value);
}

void FakeAlsa::unplug()
{
    present = false;
    QMetaObject::invokeMethod(this, [this] { emit lost(); }, Qt::QueuedConnection);
}

QString FakeAlsa::value(const QString &control) const
{
    return controls.value(control).values.join(u',');
}

bool FakeAlsa::set(const QString &control, const QString &value)
{
    const auto it = controls.find(control);
    if (it == controls.end())
        return false;
    bool number = false;
    value.toLong(&number);
    if (it->items.isEmpty() ? !number : !it->items.contains(value))
        return false;
    const bool changed = it->values != QStringList{value};
    it->values = {value};
    if (changed && it->events && m_open)
        QMetaObject::invokeMethod(this, [this, control] {
            if (m_open)
                emit this->changed(control);
        }, Qt::QueuedConnection);
    return true;
}
