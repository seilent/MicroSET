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

}

const QList<PowerBridge::Property> &PowerBridge::properties()
{
    static const QList<Property> list = {
        {u"power.profile"_s, Power1, u"Profile"_s},
        {u"power.gpu_level"_s, Power1, u"GpuLevel"_s},
        {u"power.gpu_clock"_s, Power1, u"ManualGpuClock"_s},
        {u"power.governor"_s, Power1, u"CpuGovernor"_s},
        {u"power.tdp"_s, Tdp1, u"TdpLimit"_s},
    };
    return list;
}

PowerBridge::PowerBridge(const QDBusConnection &bus, Settings &settings, QObject *parent)
    : QObject(parent)
    , m_bus(bus)
    , m_settings(settings)
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
    if (owner.isEmpty())
        qInfo("powerd left the bus");
    else
        push();
}

void PowerBridge::call(const QString &method, const QVariantList &arguments,
                       std::function<void(const QDBusMessage &)> done)
{
    QDBusMessage message = QDBusMessage::createMethodCall(m_owner, PowerPath, Properties, method);
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
        m_recording = true;
        qInfo("push to %s: %s", qPrintable(m_owner), pushed.isEmpty() ? "nothing differs" : qPrintable(pushed.join(u' ')));
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

void PowerBridge::set(const QString &key, const QVariant &value, Done done)
{
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
    for (const Property &property : properties())
        if (property.interface == interface && changed.contains(property.name))
            record(property.key, changed.value(property.name));
}
