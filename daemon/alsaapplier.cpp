#include "alsaapplier.h"

#include <QFileInfo>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

const QString HpGain = u"audio.hp_gain"_s;
const QString HpAmp = u"audio.hp_amp"_s;
const QString SpeakerGain = u"audio.speaker_gain"_s;

}

const QStringList &AlsaApplier::keys()
{
    static const QStringList list = {HpGain, HpAmp, SpeakerGain};
    return list;
}

const AlsaApplier::Values &AlsaApplier::fixed()
{
    static const Values list = {{u"SpkrLeft PA Volume"_s, u"12"_s}, {u"SpkrRight PA Volume"_s, u"12"_s}};
    return list;
}

AlsaApplier::AlsaApplier(const QDBusConnection &bus, Settings &settings, AlsaControl &control, const QString &devices,
                         int settleMs, QObject *parent)
    : QObject(parent)
    , m_bus(bus)
    , m_settings(settings)
    , m_control(control)
    , m_devices(devices)
{
    m_settle.setSingleShot(true);
    m_settle.setInterval(settleMs);
}

AlsaApplier::Values AlsaApplier::wanted(const QString &key) const
{
    const QVariant value = m_settings.value(key);
    if (key == HpGain) {
        static const QMap<QString, QString> steps = {{u"low"_s, u"8"_s}, {u"normal"_s, u"20"_s}, {u"high"_s, u"24"_s}};
        const QString step = steps.value(value.toString());
        if (step.isEmpty())
            return {};
        return {{u"HPHL Volume"_s, step}, {u"HPHR Volume"_s, step}};
    }
    if (key == HpAmp) {
        static const QMap<QString, std::pair<QString, QString>> modes = {
            {u"lohifi"_s, {u"CLS_H_LOHIFI"_s, u"LOHIFI"_s}},
            {u"ulp"_s, {u"CLS_H_ULP"_s, u"ULP"_s}},
            {u"hifi"_s, {u"CLS_H_HIFI"_s, u"LOHIFI"_s}},
        };
        const auto it = modes.constFind(value.toString());
        if (it == modes.cend())
            return {};
        return {{u"RX HPH Mode"_s, it->first}, {u"RX_HPH PWR Mode"_s, it->second}};
    }
    if (key == SpeakerGain && value.typeId() == QMetaType::UInt) {
        const QString gain = QString::number(value.toUInt());
        return {{u"WSA_RX0 Digital Volume"_s, gain}, {u"WSA_RX1 Digital Volume"_s, gain}};
    }
    return {};
}

AlsaApplier::Values AlsaApplier::all() const
{
    Values out;
    for (const QString &key : keys())
        out += wanted(key);
    return out + fixed();
}

QStringList AlsaApplier::controls() const
{
    QStringList out;
    for (const auto &[name, value] : all())
        out << name;
    return out;
}

QString AlsaApplier::wantedValue(const QString &control) const
{
    for (const auto &[name, value] : all())
        if (name == control)
            return value;
    return {};
}

void AlsaApplier::start()
{
    connect(&m_settings, &Settings::changed, this, [this](const QString &key) {
        if (keys().contains(key) && m_control.isOpen())
            apply(wanted(key), nullptr);
    });
    connect(&m_control, &AlsaControl::changed, this, [this](const QString &control) {
        if (!wantedValue(control).isEmpty())
            check({control}, u"value event"_s);
    });
    connect(&m_control, &AlsaControl::lost, this, [this] {
        m_control.close();
        m_settle.stop();
        qInfo("alsa: card %s gone", qPrintable(m_control.card()));
        rescan();
    });
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &AlsaApplier::rescan);
    connect(&m_settle, &QTimer::timeout, this, [this] {
        if (m_control.isOpen())
            apply(all(), "card appeared");
    });
    m_bus.connect(u"org.freedesktop.login1"_s, u"/org/freedesktop/login1"_s, u"org.freedesktop.login1.Manager"_s,
                  u"PrepareForSleep"_s, this, SLOT(prepareForSleep(bool)));
    watch();
    if (m_control.open())
        apply(all(), "start");
    else
        qInfo("alsa: no card %s, waiting for it", qPrintable(m_control.card()));
}

void AlsaApplier::watch()
{
    const QString parent = QFileInfo(m_devices).absolutePath();
    if (m_watcher.directories().contains(m_devices))
        return;
    if (m_watcher.addPath(m_devices)) {
        if (m_watcher.directories().contains(parent))
            m_watcher.removePath(parent);
    } else if (!m_watcher.directories().contains(parent)) {
        m_watcher.addPath(parent);
    }
}

void AlsaApplier::rescan()
{
    watch();
    if (m_control.isOpen() || !m_control.open())
        return;
    qInfo("alsa: card %s opened", qPrintable(m_control.card()));
    check(controls(), u"card reopen"_s);
    m_settle.start();
}

void AlsaApplier::prepareForSleep(bool start)
{
    if (start || !m_control.isOpen())
        return;
    check(controls(), u"resume"_s);
}

void AlsaApplier::apply(const Values &values, const char *cause)
{
    QStringList written;
    for (const auto &[control, value] : values) {
        if (write(control, value))
            written << control + u'=' + value;
        else if (!m_control.read(control))
            missing(control);
        else
            writeFailed(control, u"alsa: cannot write %1=%2"_s.arg(control, value));
    }
    if (cause && !written.isEmpty())
        qInfo("alsa %s: wrote %s", cause, qPrintable(written.join(u", "_s)));
}

void AlsaApplier::check(const QStringList &controls, const QString &cause)
{
    for (const QString &control : controls) {
        const std::optional<QStringList> current = m_control.read(control);
        if (!current) {
            missing(control);
            continue;
        }
        const QString value = wantedValue(control);
        if (value.isEmpty() || std::all_of(current->cbegin(), current->cend(), [&](const QString &v) { return v == value; }))
            continue;
        if (!write(control, value)) {
            writeFailed(control, u"alsa %1: %2 read %3, cannot write %4"_s.arg(cause, control, current->join(u','), value));
            continue;
        }
        const QString tag = cause + u'\n' + control;
        if (!m_guarded.contains(tag))
            qWarning("alsa %s: %s read %s, wrote %s", qPrintable(cause), qPrintable(control),
                     qPrintable(current->join(u',')), qPrintable(value));
        m_guarded.insert(tag);
    }
}

bool AlsaApplier::write(const QString &control, const QString &value)
{
    if (!m_control.write(control, value))
        return false;
    m_failed.remove(control);
    return true;
}

void AlsaApplier::writeFailed(const QString &control, const QString &line)
{
    if (m_failed.contains(control))
        return;
    m_failed.insert(control);
    qWarning("%s", qPrintable(line));
}

void AlsaApplier::missing(const QString &control)
{
    if (m_missing.contains(control))
        return;
    m_missing.insert(control);
    qWarning("alsa: card %s has no control %s", qPrintable(m_control.card()), qPrintable(control));
}
