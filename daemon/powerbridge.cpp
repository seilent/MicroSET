#include "powerbridge.h"

#include "errors.h"

#include <QDBusConnectionInterface>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusReply>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QFile>

#include <algorithm>
#include <limits>

using namespace Qt::StringLiterals;

namespace {

const QString Powerd = u"net.seilent.MicroOs.Power"_s;
const QString PowerPath = u"/net/seilent/MicroOs/Power"_s;
const QString Properties = u"org.freedesktop.DBus.Properties"_s;
const QString Power1 = u"net.seilent.MicroOs.Power1"_s;
const QString Tdp1 = u"com.steampowered.SteamOSManager1.TdpLimit1"_s;
const QString ImportedKey = u"meta.imported_power"_s;
const QString UndervoltKey = u"power.gpu_undervolt"_s;
const QString UndervoltLevelsKey = u"power.gpu_undervolt_levels"_s;
const QString TuningFallbackKey = u"power.gpu_tuning_fallback"_s;
const QString TuningTripKey = u"power.gpu_tuning_trip"_s;
const QString UndervoltActiveKey = u"power.gpu_undervolt_active"_s;
const QString UndervoltSerialKey = u"meta.gpu_undervolt_serial"_s;
const QString Off = u"off"_s;
const QStringList Trips = {u"hang"_s, u"gmu"_s, u"hfi"_s, u"oob"_s, u"start"_s};
const QStringList Reasons = Trips + QStringList{u"unclean"_s, u"changed"_s, u"refused"_s};

}

const QList<PowerBridge::Property> &PowerBridge::properties()
{
    static const QList<Property> list = {
        {u"power.profile"_s, Power1, u"Profile"_s},
        {u"power.gpu_level"_s, Power1, u"GpuLevel"_s},
        {u"power.gpu_clock"_s, Power1, u"ManualGpuClock"_s},
        {u"power.governor"_s, Power1, u"CpuGovernor"_s},
        {u"power.game_floor"_s, Power1, u"GameCpuFloor"_s},
        {u"power.tdp"_s, Tdp1, u"TdpLimit"_s},
    };
    return list;
}

PowerBridge::PowerBridge(const QDBusConnection &bus, Settings &settings, const UndervoltsProvider &undervolts,
                         QObject *parent)
    : QObject(parent)
    , m_bus(bus)
    , m_settings(settings)
    , m_undervolts(undervolts)
{
}

void PowerBridge::importState(const QString &path, Settings &settings)
{
    if (settings.value(ImportedKey).toBool())
        return;
    for (const Property &property : properties())
        if (settings.isStored(property.key))
            return;
    QFile file(path);
    if (!file.exists())
        return;
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning("%s: %s, not imported", qPrintable(path), qPrintable(file.errorString()));
        return;
    }
    const QMap<QString, QString> names = {
        {u"profile"_s, u"power.profile"_s},     {u"gpu_level"_s, u"power.gpu_level"_s},
        {u"gpu_clock"_s, u"power.gpu_clock"_s}, {u"governor"_s, u"power.governor"_s},
        {u"tdp"_s, u"power.tdp"_s},
    };
    const Schema &schema = settings.schema();
    QList<std::pair<QString, QVariant>> values;
    int number = 0;
    while (!file.atEnd()) {
        ++number;
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.isEmpty())
            continue;
        const qsizetype eq = line.indexOf(u'=');
        const QString name = names.value(line.left(eq).trimmed());
        const Key *key = schema.key(name);
        const QString text = line.mid(eq + 1).trimmed();
        QVariant value;
        if (key && eq > 0) {
            bool ok = false;
            const qlonglong n = text.toLongLong(&ok);
            if (key->type == "u"_L1 && text.isEmpty() && name == "power.tdp"_L1 && key->max)
                value = uint(*key->max);
            else if (key->type == "u"_L1 && ok && n >= 0)
                value = schema.clamp(*key, uint(std::min<qlonglong>(n, std::numeric_limits<uint>::max())));
            else if (key->type == "s"_L1 && schema.accepts(*key, text))
                value = text;
        }
        if (!value.isValid()) {
            qWarning("%s: line %d is not a valid power setting, not imported", qPrintable(path), number);
            return;
        }
        values.append({name, value});
    }
    QVariantMap batch = {{ImportedKey, true}};
    QStringList imported;
    for (const auto &[name, value] : values) {
        batch.insert(name, value);
        imported << name + u'=' + Schema::text(value);
    }
    if (settings.store(batch) == Settings::Failed) {
        qWarning("%s: cannot write the state file, not imported", qPrintable(path));
        return;
    }
    qInfo("imported %s: %s", qPrintable(path), qPrintable(imported.join(u' ')));
}

void PowerBridge::start()
{
    auto *watcher = new QDBusServiceWatcher(Powerd, m_bus, QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, this, &PowerBridge::ownerChanged);
    m_bus.connect(QString(), PowerPath, Properties, u"PropertiesChanged"_s, this,
                  SLOT(propertiesChanged(QString, QVariantMap, QStringList, QDBusMessage)));
    m_bus.connect(QString(), PowerPath, Power1, u"UndervoltRequestDone"_s, this,
                  SLOT(undervoltDone(uint, QString, QString, QString, QDBusMessage)));
    const QDBusReply<QString> owner = m_bus.interface()->serviceOwner(Powerd);
    if (owner.isValid())
        ownerChanged(Powerd, QString(), owner.value());
}

void PowerBridge::ownerChanged(const QString &, const QString &, const QString &owner)
{
    if (owner == m_owner)
        return;
    m_owner = owner;
    ++m_generation;
    m_recording = false;
    m_pending = 0;
    m_settings.setLive(TuningTripKey, QString());
    if (owner.isEmpty())
        qInfo("powerd left the bus");
    else
        push();
}

void PowerBridge::call(const QString &method, const QVariantList &arguments,
                       std::function<void(const QDBusMessage &)> done)
{
    call(Properties, method, arguments, std::move(done));
}

void PowerBridge::call(const QString &interface, const QString &method, const QVariantList &arguments,
                       std::function<void(const QDBusMessage &)> done)
{
    QDBusMessage message = QDBusMessage::createMethodCall(m_owner, PowerPath, interface, method);
    message.setArguments(arguments);
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(message, 5000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [watcher, done = std::move(done)] {
        watcher->deleteLater();
        done(watcher->reply());
    });
}

void PowerBridge::push()
{
    const quint64 generation = m_generation;
    call(u"GetAll"_s, {Power1}, [this, generation](const QDBusMessage &power) {
        if (generation != m_generation)
            return;
        call(u"GetAll"_s, {Tdp1}, [this, generation, power](const QDBusMessage &tdp) {
            if (generation != m_generation)
                return;
            if (power.type() != QDBusMessage::ReplyMessage) {
                qWarning("push to %s: %s", qPrintable(m_owner), qPrintable(power.errorMessage()));
                m_recording = true;
                return;
            }
            if (tdp.type() != QDBusMessage::ReplyMessage)
                qWarning("push to %s: no TdpLimit1: %s", qPrintable(m_owner), qPrintable(tdp.errorMessage()));
            const QVariantMap current = qdbus_cast<QVariantMap>(power.arguments().value(0));
            recordUndervolt(current);
            const QVariantMap limits = tdp.type() == QDBusMessage::ReplyMessage
                ? qdbus_cast<QVariantMap>(tdp.arguments().value(0))
                : QVariantMap();
            QList<Property> queue;
            for (const Property &property : properties()) {
                const QVariantMap &values = property.interface == Power1 ? current : limits;
                if (values.contains(property.name) && values.value(property.name) != m_settings.value(property.key))
                    queue << property;
            }
            pushNext(generation, queue, {});
        });
    });
}

void PowerBridge::pushNext(quint64 generation, QList<Property> queue, QStringList pushed)
{
    if (queue.isEmpty()) {
        pushUndervolt(generation, pushed);
        return;
    }
    const Property property = queue.takeFirst();
    const QVariant value = m_settings.value(property.key);
    const QVariantList arguments = {property.interface, property.name, QVariant::fromValue(QDBusVariant(value))};
    call(u"Set"_s, arguments, [=, this](const QDBusMessage &reply) mutable {
        if (generation != m_generation)
            return;
        QString entry = property.key + u'=' + Schema::text(value);
        if (reply.type() != QDBusMessage::ReplyMessage)
            entry += u" failed ("_s + reply.errorName() + u')';
        pushed << entry;
        pushNext(generation, queue, pushed);
    });
}

void PowerBridge::pushUndervolt(quint64 generation, QStringList pushed)
{
    const QString value = undervoltValue();
    const QVariantList arguments = {Power1, u"GpuUndervolt"_s, QVariant::fromValue(QDBusVariant(value))};
    m_recording = true;
    call(u"Set"_s, arguments, [=, this](const QDBusMessage &reply) mutable {
        if (generation != m_generation)
            return;
        QString entry = UndervoltKey + u'=' + value;
        if (reply.type() != QDBusMessage::ReplyMessage)
            entry += u" failed ("_s + reply.errorName() + u')';
        qInfo("push to %s: %s and %s", qPrintable(m_owner),
              pushed.isEmpty() ? "nothing differs" : qPrintable(pushed.join(u' ')), qPrintable(entry));
    });
}

QString PowerBridge::undervoltName() const
{
    const QString name = m_settings.storedUnchecked(UndervoltKey).toString();
    return name.isEmpty() ? Off : name;
}

QString PowerBridge::undervoltLevels() const
{
    return undervoltName() == Off ? QString() : m_settings.storedUnchecked(UndervoltLevelsKey).toString();
}

QString PowerBridge::undervoltValue() const
{
    const QString levels = undervoltLevels();
    return levels.isEmpty() ? Off : undervoltName() + u' ' + levels;
}

bool PowerBridge::deeper(const QString &levels) const
{
    if (levels.isEmpty())
        return false;
    const QMap<quint64, uint> now = UndervoltsProvider::parse(undervoltLevels());
    const QMap<quint64, uint> want = UndervoltsProvider::parse(levels);
    if (now.isEmpty() || now.keys() != want.keys())
        return true;
    for (auto it = want.cbegin(); it != want.cend(); ++it)
        if (it.value() < now.value(it.key()))
            return true;
    return false;
}

void PowerBridge::setUndervolt(const QString &name, Done done)
{
    const QString levels = name == Off ? QString() : m_undervolts.levels(name);
    if (name != Off && levels.isEmpty()) {
        done(Errors::InvalidValue, name + u" is not a GPU undervolt preset"_s);
        return;
    }
    if (!deeper(levels)) {
        if (m_settings.store({{UndervoltKey, name}, {UndervoltLevelsKey, levels}}) == Settings::Failed) {
            done(Errors::Failed, u"cannot write the state file"_s);
            return;
        }
        m_pending = 0;
        sendUndervolt();
        done({}, {});
        return;
    }
    if (m_owner.isEmpty()) {
        done(Errors::ServiceUnknown, u"powerd is not running"_s);
        return;
    }
    const uint serial = m_settings.value(UndervoltSerialKey).toUInt() + 1;
    if (m_settings.store(UndervoltSerialKey, serial) == Settings::Failed) {
        done(Errors::Failed, u"cannot write the state file"_s);
        return;
    }
    const QVariantList arguments = {QVariant::fromValue(serial), name, levels};
    const QString owner = m_owner;
    call(Power1, u"RequestUndervolt"_s, arguments, [this, serial, name, owner, done](const QDBusMessage &reply) {
        if (reply.type() != QDBusMessage::ReplyMessage) {
            qInfo("undervolt request %u %s: %s", serial, qPrintable(name), qPrintable(reply.errorName()));
            done(reply.errorName() == Errors::InvalidArgs ? Errors::InvalidValue : reply.errorName(),
                 reply.errorMessage());
            return;
        }
        if (owner != m_owner) {
            done(Errors::Failed, u"powerd restarted"_s);
            return;
        }
        m_pending = serial;
        qInfo("undervolt request %u %s: waiting for A", serial, qPrintable(name));
        done(Errors::HoldPending, u"hold A for 1 s to apply"_s);
    });
}

void PowerBridge::sendUndervolt()
{
    if (m_owner.isEmpty())
        return;
    const QString value = undervoltValue();
    const QVariantList arguments = {Power1, u"GpuUndervolt"_s, QVariant::fromValue(QDBusVariant(value))};
    call(u"Set"_s, arguments, [value](const QDBusMessage &reply) {
        if (reply.type() != QDBusMessage::ReplyMessage)
            qWarning("push %s=%s failed (%s)", qPrintable(UndervoltKey), qPrintable(value), qPrintable(reply.errorName()));
        else
            qInfo("push %s=%s", qPrintable(UndervoltKey), qPrintable(value));
    });
}

bool PowerBridge::recordUndervolt(const QVariantMap &values)
{
    const Schema &schema = m_settings.schema();
    const QVariant active = values.value(u"GpuUndervoltActive"_s);
    const Key *activeKey = schema.key(UndervoltActiveKey);
    if (activeKey && active.isValid() && schema.accepts(*activeKey, active))
        m_settings.setLive(UndervoltActiveKey, active);
    const QVariant trip = values.value(u"GpuTuningTrip"_s);
    const Key *tripKey = schema.key(TuningTripKey);
    if (tripKey && trip.isValid()) {
        if (schema.accepts(*tripKey, trip) && (trip.toString().isEmpty() || Trips.contains(trip.toString())))
            m_settings.setLive(TuningTripKey, trip);
        else
            qWarning("powerd reported %s=%s, outside the schema, not recorded", qPrintable(TuningTripKey),
                     qPrintable(Schema::text(trip)));
    }
    const QVariant reason = values.value(u"GpuTuningFallback"_s);
    const Key *reasonKey = schema.key(TuningFallbackKey);
    if (!reasonKey || !reason.isValid() || reason.toString().isEmpty())
        return false;
    if (!schema.accepts(*reasonKey, reason) || !Reasons.contains(reason.toString())) {
        qWarning("powerd reported %s=%s, outside the schema, not stored", qPrintable(TuningFallbackKey),
                 qPrintable(Schema::text(reason)));
        return false;
    }
    if (m_settings.store({{UndervoltKey, Off}, {UndervoltLevelsKey, QString()}, {TuningFallbackKey, reason}})
        == Settings::Failed) {
        qWarning("cannot store the GPU undervolt fallback %s", qPrintable(reason.toString()));
        return false;
    }
    qInfo("GPU undervolt turned off: %s", qPrintable(reason.toString()));
    return true;
}

void PowerBridge::undervoltDone(uint serial, const QString &result, const QString &name, const QString &levels,
                                const QDBusMessage &message)
{
    if (message.service() != m_owner || !m_pending || serial != m_pending)
        return;
    m_pending = 0;
    if (result != "accepted"_L1) {
        qInfo("undervolt request %u %s: %s", serial, qPrintable(name), qPrintable(result));
        emit holdRefused(UndervoltKey, result.section(u' ', 1));
        return;
    }
    if (m_undervolts.levels(name) != levels) {
        qInfo("undervolt request %u %s: the preset changed while waiting", serial, qPrintable(name));
        emit holdRefused(UndervoltKey, u"the preset changed"_s);
        return;
    }
    if (m_settings.store({{UndervoltKey, name}, {UndervoltLevelsKey, levels}, {TuningFallbackKey, QString()}})
        == Settings::Failed) {
        emit holdRefused(UndervoltKey, u"cannot write the state file"_s);
        return;
    }
    qInfo("undervolt request %u %s: accepted", serial, qPrintable(name));
    sendUndervolt();
}

void PowerBridge::set(const QString &key, const QVariant &value, Done done)
{
    if (key == UndervoltKey) {
        setUndervolt(value.toString(), std::move(done));
        return;
    }
    const auto it = std::find_if(properties().cbegin(), properties().cend(),
                                 [&](const Property &property) { return property.key == key; });
    if (it == properties().cend()) {
        done(Errors::Failed, u"no powerd property for "_s + key);
        return;
    }
    if (m_owner.isEmpty()) {
        done(Errors::ServiceUnknown, u"powerd is not running"_s);
        return;
    }
    const Property property = *it;
    const QVariantList arguments = {property.interface, property.name, QVariant::fromValue(QDBusVariant(value))};
    call(u"Set"_s, arguments, [this, property, done](const QDBusMessage &reply) {
        if (reply.type() != QDBusMessage::ReplyMessage) {
            done(reply.errorName() == Errors::InvalidArgs ? Errors::InvalidValue : reply.errorName(),
                 reply.errorMessage());
            return;
        }
        call(u"Get"_s, {property.interface, property.name}, [this, property, done](const QDBusMessage &got) {
            if (got.type() != QDBusMessage::ReplyMessage) {
                done(Errors::Failed, u"powerd did not report %1: %2"_s.arg(property.name, got.errorMessage()));
                return;
            }
            if (record(property.key, got.arguments().value(0).value<QDBusVariant>().variant()))
                done({}, {});
            else
                done(Errors::Failed, u"cannot store what powerd reported for "_s + property.key);
        });
    });
}

bool PowerBridge::record(const QString &key, const QVariant &value)
{
    const Key *entry = m_settings.schema().key(key);
    const QVariant clamped = entry ? m_settings.schema().clamp(*entry, value) : QVariant();
    if (!clamped.isValid()) {
        qWarning("powerd reported %s=%s, outside the schema, not stored", qPrintable(key), qPrintable(Schema::text(value)));
        return false;
    }
    return m_settings.store(key, clamped) != Settings::Failed;
}

void PowerBridge::propertiesChanged(const QString &interface, const QVariantMap &changed, const QStringList &,
                                    const QDBusMessage &message)
{
    if (!m_recording || message.service() != m_owner)
        return;
    if (interface == Power1 && recordUndervolt(changed))
        sendUndervolt();
    for (const Property &property : properties())
        if (property.interface == interface && changed.contains(property.name))
            record(property.key, changed.value(property.name));
}
