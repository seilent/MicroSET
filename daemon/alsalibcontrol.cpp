#include "alsalibcontrol.h"

#include <alsa/asoundlib.h>

#include <cerrno>

AlsaLibControl::AlsaLibControl(const QString &card, QObject *parent)
    : AlsaControl(parent)
    , m_card(card)
{
}

AlsaLibControl::~AlsaLibControl()
{
    close();
}

QString AlsaLibControl::card() const
{
    return m_card;
}

bool AlsaLibControl::open()
{
    if (m_ctl)
        return true;
    snd_ctl_card_info_t *info;
    snd_ctl_card_info_alloca(&info);
    int card = -1;
    while (!m_ctl && snd_card_next(&card) == 0 && card >= 0) {
        snd_ctl_t *ctl = nullptr;
        if (snd_ctl_open(&ctl, ("hw:" + QByteArray::number(card)).constData(), SND_CTL_NONBLOCK) < 0)
            continue;
        if (snd_ctl_card_info(ctl, info) == 0 && m_card == QLatin1StringView(snd_ctl_card_info_get_id(info)))
            m_ctl = ctl;
        else
            snd_ctl_close(ctl);
    }
    if (!m_ctl)
        return false;
    pollfd descriptor;
    const int error = snd_ctl_subscribe_events(m_ctl, 1);
    if (error < 0 || snd_ctl_poll_descriptors(m_ctl, &descriptor, 1) != 1) {
        qWarning("alsa: no events from card %s: %s", qPrintable(m_card), error < 0 ? snd_strerror(error) : "no descriptor");
        close();
        return false;
    }
    m_notifier = new QSocketNotifier(descriptor.fd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &AlsaLibControl::readEvents);
    return true;
}

void AlsaLibControl::close()
{
    if (m_notifier) {
        m_notifier->setEnabled(false);
        m_notifier->deleteLater();
        m_notifier = nullptr;
    }
    if (m_ctl) {
        snd_ctl_close(m_ctl);
        m_ctl = nullptr;
    }
    m_elements.clear();
}

bool AlsaLibControl::isOpen() const
{
    return m_ctl;
}

void AlsaLibControl::readEvents()
{
    snd_ctl_event_t *event;
    snd_ctl_event_alloca(&event);
    QStringList names;
    for (int i = 0; i < 64; ++i) {
        const int n = snd_ctl_read(m_ctl, event);
        if (n == 0 || n == -EAGAIN)
            break;
        if (n < 0) {
            emit lost();
            return;
        }
        if (snd_ctl_event_get_type(event) != SND_CTL_EVENT_ELEM)
            continue;
        const unsigned mask = snd_ctl_event_elem_get_mask(event);
        if (mask == SND_CTL_EVENT_MASK_REMOVE || !(mask & SND_CTL_EVENT_MASK_VALUE))
            continue;
        const QString name = QString::fromUtf8(snd_ctl_event_elem_get_name(event));
        if (!names.contains(name))
            names << name;
    }
    for (const QString &name : std::as_const(names))
        emit changed(name);
}

const AlsaLibControl::Element *AlsaLibControl::element(const QString &control)
{
    const auto it = m_elements.constFind(control);
    if (it != m_elements.cend())
        return &*it;
    snd_ctl_elem_info_t *info;
    snd_ctl_elem_info_alloca(&info);
    snd_ctl_elem_info_set_interface(info, SND_CTL_ELEM_IFACE_MIXER);
    snd_ctl_elem_info_set_name(info, control.toUtf8().constData());
    if (snd_ctl_elem_info(m_ctl, info) < 0)
        return nullptr;
    Element element;
    element.count = snd_ctl_elem_info_get_count(info);
    const snd_ctl_elem_type_t type = snd_ctl_elem_info_get_type(info);
    if (type == SND_CTL_ELEM_TYPE_ENUMERATED) {
        element.enumerated = true;
        const unsigned items = snd_ctl_elem_info_get_items(info);
        for (unsigned i = 0; i < items; ++i) {
            snd_ctl_elem_info_set_item(info, i);
            if (snd_ctl_elem_info(m_ctl, info) < 0)
                return nullptr;
            element.items << QString::fromUtf8(snd_ctl_elem_info_get_item_name(info));
        }
    } else if (type != SND_CTL_ELEM_TYPE_INTEGER) {
        return nullptr;
    }
    return &*m_elements.insert(control, element);
}

std::optional<QStringList> AlsaLibControl::read(const QString &control)
{
    const Element *element = m_ctl ? this->element(control) : nullptr;
    if (!element)
        return std::nullopt;
    snd_ctl_elem_value_t *value;
    snd_ctl_elem_value_alloca(&value);
    snd_ctl_elem_value_set_interface(value, SND_CTL_ELEM_IFACE_MIXER);
    snd_ctl_elem_value_set_name(value, control.toUtf8().constData());
    if (snd_ctl_elem_read(m_ctl, value) < 0)
        return std::nullopt;
    QStringList out;
    for (unsigned i = 0; i < element->count; ++i) {
        if (element->enumerated) {
            const unsigned index = snd_ctl_elem_value_get_enumerated(value, i);
            out << element->items.value(index, QString::number(index));
        } else {
            out << QString::number(snd_ctl_elem_value_get_integer(value, i));
        }
    }
    return out;
}

bool AlsaLibControl::write(const QString &control, const QString &text)
{
    const Element *element = m_ctl ? this->element(control) : nullptr;
    if (!element)
        return false;
    snd_ctl_elem_value_t *value;
    snd_ctl_elem_value_alloca(&value);
    snd_ctl_elem_value_set_interface(value, SND_CTL_ELEM_IFACE_MIXER);
    snd_ctl_elem_value_set_name(value, control.toUtf8().constData());
    bool ok = true;
    const long number = text.toLong(&ok);
    const qsizetype index = element->items.indexOf(text);
    if (element->enumerated ? index < 0 : !ok)
        return false;
    for (unsigned i = 0; i < element->count; ++i) {
        if (element->enumerated)
            snd_ctl_elem_value_set_enumerated(value, i, unsigned(index));
        else
            snd_ctl_elem_value_set_integer(value, i, number);
    }
    return snd_ctl_elem_write(m_ctl, value) >= 0;
}
