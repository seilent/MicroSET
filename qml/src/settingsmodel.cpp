#include "settingsmodel.h"

#include <microset/client.h>
#include <microset/color.h>

#include <QColor>
#include <QDBusArgument>
#include <QDBusPendingCallWatcher>
#include <QDBusServiceWatcher>
#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace Qt::StringLiterals;

namespace {

const QString Service = u"net.seilent.MicroSet"_s;
const QString Path = u"/net/seilent/MicroSet"_s;
const QString Interface = u"net.seilent.MicroSet1"_s;
const QString PrivilegedOwner = u"microset"_s;
constexpr int Last = std::numeric_limits<int>::max();

bool isNumeric(const QString &type)
{
    return type == "u"_L1 || type == "i"_L1 || type == "d"_L1;
}

QString ownerRank(const QString &owner)
{
    return owner == PrivilegedOwner ? QString() : owner;
}

QString errorOf(const QDBusMessage &reply)
{
    if (reply.type() == QDBusMessage::ReplyMessage)
        return {};
    return reply.errorName().isEmpty() ? u"org.freedesktop.DBus.Error.Failed"_s : reply.errorName();
}

qsizetype nearest(const QVariantList &list, const QVariant &value)
{
    qsizetype best = -1;
    for (qsizetype k = 0; k < list.size(); ++k) {
        bool ok = false;
        const double number = list.at(k).toDouble(&ok);
        if (!ok || list.at(k).typeId() == QMetaType::QString)
            continue;
        if (best < 0 || std::abs(number - value.toDouble()) < std::abs(list.at(best).toDouble() - value.toDouble()))
            best = k;
    }
    return std::max<qsizetype>(0, best);
}

}

SettingsModel::SettingsModel(QObject *parent)
    : QObject(parent)
    , m_bus(QString())
    , m_fixture(true)
{
    connect(&m_updates, &UpdateInfo::changed, this, &SettingsModel::bump);
}

SettingsModel::SettingsModel(const QDBusConnection &bus, QObject *parent)
    : QObject(parent)
    , m_client(std::make_unique<MicroSet::Client>(bus))
    , m_bus(bus)
{
    connect(m_client.get(), &MicroSet::Client::changed, this, &SettingsModel::onChanged);
    auto *watcher = new QDBusServiceWatcher(Service, bus, QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, this, &SettingsModel::onOwnerChanged);
    connect(&m_updates, &UpdateInfo::changed, this, &SettingsModel::bump);
    m_updates.watch();
}

SettingsModel::~SettingsModel() = default;

void SettingsModel::call(const QString &method, const QVariantList &arguments, Reply done)
{
    QDBusMessage message = QDBusMessage::createMethodCall(Service, Path, Interface, method);
    message.setArguments(arguments);
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [watcher, done = std::move(done)] {
        watcher->deleteLater();
        done(watcher->reply());
    });
}

void SettingsModel::load()
{
    if (!m_client)
        return;
    const int generation = ++m_generation;
    const auto failed = [this, generation](const QString &error) {
        if (generation != m_generation)
            return true;
        if (error.isEmpty())
            return false;
        qWarning("microset settings: cannot read microsetd: %s", qPrintable(error));
        setAvailable(false);
        return true;
    };
    call(u"Schema"_s, {QString()}, [this, failed](const QDBusMessage &reply) {
        const QJsonDocument document = QJsonDocument::fromJson(reply.arguments().value(0).toString().toUtf8());
        if (failed(errorOf(reply)) || (!document.isObject() && failed(u"bad schema"_s)))
            return;
        const QJsonObject schema = document.object();
        call(u"List"_s, {QString()}, [this, failed, schema](const QDBusMessage &reply) {
            if (failed(errorOf(reply)))
                return;
            const QVariantMap values = qdbus_cast<QVariantMap>(reply.arguments().value(0));
            call(u"Stored"_s, {QString()}, [this, failed, schema, values](const QDBusMessage &reply) {
                if (failed(errorOf(reply)))
                    return;
                build(schema, values, qdbus_cast<QVariantMap>(reply.arguments().value(0)));
                setAvailable(true);
                bump();
            });
        });
    });
}

void SettingsModel::setFixture(const QJsonObject &schema, const QVariantMap &values, const QVariantMap &stored)
{
    build(schema, values, stored);
    setAvailable(true);
    bump();
}

UpdateInfo *SettingsModel::updates()
{
    return &m_updates;
}

bool SettingsModel::available() const
{
    return m_available;
}

int SettingsModel::revision() const
{
    return m_revision;
}

int SettingsModel::accentHue() const
{
    const auto it = m_entries.constFind(AccentKey);
    return it == m_entries.cend() ? -1 : normalizeHue(it->shown.toInt());
}

QString SettingsModel::updatesGroup() const
{
    return UpdatesGroup;
}

int SettingsModel::normalizeHue(int hue)
{
    const int stepped = qRound(hue / 10.0) * 10;
    return ((stepped % 360) + 360) % 360;
}

QString SettingsModel::toastText(const QString &error)
{
    static const QStringList gone = {u"org.freedesktop.DBus.Error.ServiceUnknown"_s, u"org.freedesktop.DBus.Error.NoReply"_s,
                                     u"org.freedesktop.DBus.Error.Disconnected"_s, u"org.freedesktop.DBus.Error.NameHasNoOwner"_s,
                                     u"org.freedesktop.DBus.Error.Timeout"_s};
    return gone.contains(error) ? NotAvailable : SetFailed;
}

QVariant SettingsModel::coerce(const QString &type, const QVariant &value)
{
    if (!value.isValid())
        return {};
    if (type == "s"_L1)
        return value.typeId() == QMetaType::QString ? value : QVariant();
    if (type == "b"_L1)
        return value.typeId() == QMetaType::Bool ? value : QVariant();
    if (type == "as"_L1)
        return value.canConvert<QStringList>() ? QVariant(value.toStringList()) : QVariant();
    bool ok = false;
    const double number = value.toDouble(&ok);
    if (!ok || !std::isfinite(number) || value.typeId() == QMetaType::Bool || value.typeId() == QMetaType::QString)
        return {};
    if (type == "d"_L1)
        return number;
    const double whole = std::round(number);
    if (type == "u"_L1 && whole >= 0 && whole <= std::numeric_limits<uint>::max())
        return uint(whole);
    if (type == "i"_L1 && whole >= std::numeric_limits<int>::min() && whole <= std::numeric_limits<int>::max())
        return int(whole);
    return {};
}

void SettingsModel::build(const QJsonObject &schema, const QVariantMap &values, const QVariantMap &stored)
{
    QMap<QString, Entry> entries;
    QMap<QString, QString> groupLabels;
    QSet<QString> live;
    QSet<QString> referenced;
    for (auto it = schema.begin(); it != schema.end(); ++it) {
        const QJsonObject object = it.value().toObject();
        const QJsonObject ui = object.value("ui"_L1).toObject();
        const QString groupLabel = object.value("group_ui"_L1).toObject().value("label"_L1).toString();
        if (!groupLabel.isEmpty())
            groupLabels.insert(object.value("group"_L1).toString(), groupLabel);
        if (object.value("live"_L1).toBool())
            live.insert(it.key());
        Entry entry;
        entry.key = it.key();
        entry.group = object.value("group"_L1).toString();
        entry.widget = ui.value("widget"_L1).toString();
        entry.type = object.value("type"_L1).toString();
        if (entry.group.isEmpty() || entry.widget == "none"_L1 || entry.type.isEmpty())
            continue;
        entry.owner = object.value("owner"_L1).toString();
        const QJsonObject groupUi = object.value("group_ui"_L1).toObject();
        if (groupUi.contains("order"_L1))
            entry.groupOrder = groupUi.value("order"_L1).toInt();
        const QString label = object.value("label"_L1).toString();
        entry.title = label.isEmpty() ? entry.key : label;
        if (ui.contains("order"_L1))
            entry.order = ui.value("order"_L1).toInt();
        for (const QJsonValue &choice : object.value("enum"_L1).toArray()) {
            const QVariant value = coerce(entry.type == "as"_L1 ? u"s"_s : entry.type, choice.toVariant());
            if (value.isValid())
                entry.choices << value;
        }
        for (const QJsonValue &step : ui.value("steps"_L1).toArray()) {
            const QVariant value = coerce(entry.type, step.toVariant());
            if (value.isValid())
                entry.steps << value;
        }
        std::sort(entry.steps.begin(), entry.steps.end(),
                  [](const QVariant &a, const QVariant &b) { return a.toDouble() < b.toDouble(); });
        if (object.contains("min"_L1))
            entry.min = object.value("min"_L1).toDouble();
        if (object.contains("max"_L1))
            entry.max = object.value("max"_L1).toDouble();
        entry.step = ui.value("step"_L1).toDouble(1);
        entry.labels = ui.value("values"_L1).toObject();
        entry.unit = ui.value("unit"_L1).toString();
        entry.confirm = ui.value("apply"_L1).toString() == "confirm"_L1;
        const QJsonObject lock = ui.value("lock"_L1).toObject();
        entry.lockKey = lock.value("key"_L1).toString();
        entry.lockIs = lock.value("is"_L1);
        entry.lockText = lock.value("text"_L1).toString();
        entry.lockShow = lock.value("show"_L1).toString();
        if (!entry.lockKey.isEmpty())
            referenced << entry.lockKey;
        if (!entry.lockShow.isEmpty())
            referenced << entry.lockShow;
        entry.writable = object.value("writable"_L1).toBool(true);
        entry.overridable = object.value("override"_L1).toBool(false);
        entry.defaultValue = coerce(entry.type, object.value("default"_L1).toVariant());
        const QVariantMap &source = entry.overridable ? stored : values;
        const QVariant value = coerce(entry.type, source.value(entry.key));
        entry.store = value.isValid() ? value : entry.defaultValue;
        entry.shown = entry.store;
        const auto old = m_entries.constFind(entry.key);
        if (old != m_entries.cend() && old->inflight > 0) {
            entry.inflight = old->inflight;
            entry.shown = old->shown;
        }
        entries.insert(entry.key, entry);
    }
    m_entries = entries;
    m_groupLabels = groupLabels;
    m_values = values;
    m_live = live;
    m_referenced = referenced;
}

void SettingsModel::setAvailable(bool available)
{
    if (m_available == available)
        return;
    m_available = available;
    emit availableChanged();
}

void SettingsModel::bump()
{
    ++m_revision;
    emit revisionChanged();
}

void SettingsModel::refreshStored(const QString &key)
{
    call(u"Stored"_s, {key}, [this, key](const QDBusMessage &reply) {
        const auto it = m_entries.find(key);
        if (it == m_entries.end() || !errorOf(reply).isEmpty())
            return;
        const QVariant value = coerce(it->type, qdbus_cast<QVariantMap>(reply.arguments().value(0)).value(key));
        it->store = value.isValid() ? value : it->defaultValue;
        if (it->inflight > 0 || it->shown == it->store)
            return;
        it->shown = it->store;
        bump();
    });
}

void SettingsModel::onChanged(const QString &key, const QVariant &value)
{
    m_values.insert(key, value);
    const auto it = m_entries.find(key);
    if (it == m_entries.end()) {
        if (m_referenced.contains(key))
            bump();
        return;
    }
    if (it->overridable) {
        refreshStored(key);
        return;
    }
    const QVariant now = coerce(it->type, value);
    if (!now.isValid())
        return;
    it->store = now;
    if (it->inflight == 0 && it->shown != now) {
        it->shown = now;
        bump();
    } else if (m_referenced.contains(key)) {
        bump();
    }
}

void SettingsModel::onOwnerChanged(const QString &, const QString &, const QString &newOwner)
{
    if (newOwner.isEmpty()) {
        ++m_generation;
        setAvailable(false);
        return;
    }
    load();
}

QString SettingsModel::titleOf(const QString &group) const
{
    return m_groupLabels.value(group, group);
}

bool SettingsModel::locked(const Entry &entry) const
{
    if (entry.lockKey.isEmpty())
        return false;
    const auto it = m_entries.constFind(entry.lockKey);
    if (it == m_entries.cend() && !m_values.contains(entry.lockKey))
        return false;
    const QVariant value = it != m_entries.cend() ? it->store : m_values.value(entry.lockKey);
    return QJsonValue::fromVariant(value) == entry.lockIs;
}

QString SettingsModel::kindOf(const Entry &entry) const
{
    if (entry.type == "as"_L1 || (entry.type == "s"_L1 && entry.choices.isEmpty()))
        return u"info"_s;
    if (isNumeric(entry.type) && entry.choices.isEmpty() && entry.steps.isEmpty() && (!entry.min || !entry.max))
        return u"info"_s;
    return u"row"_s;
}

bool SettingsModel::editable(const Entry &entry) const
{
    return entry.writable && kindOf(entry) == "row"_L1 && !locked(entry);
}

QString SettingsModel::textOf(const Entry &entry, const QVariant &value) const
{
    if (entry.widget == "hue"_L1)
        return {};
    if (entry.type == "b"_L1)
        return value.toBool() ? u"on"_s : u"off"_s;
    if (entry.type == "as"_L1)
        return value.toStringList().join(u", "_s);
    const QString text = entry.type == "d"_L1 ? QString::number(value.toDouble()) : value.toString();
    const QJsonValue label = entry.labels.value(text);
    if (label.isString())
        return label.toString();
    return entry.unit.isEmpty() || text.isEmpty() ? text : text + u' ' + entry.unit;
}

QVariantMap SettingsModel::rowOf(const Entry &entry) const
{
    const QString kind = kindOf(entry);
    const bool lockedNow = locked(entry);
    QString value = textOf(entry, entry.shown);
    if (lockedNow)
        value = entry.lockShow.isEmpty() ? entry.lockText : textOf(entry, coerce(entry.type, m_values.value(entry.lockShow)));
    QVariantMap row{
        {u"kind"_s, kind},
        {u"key"_s, entry.key},
        {u"title"_s, entry.title},
        {u"value"_s, value},
        {u"readOnly"_s, !editable(entry)},
        {u"widget"_s, lockedNow ? QString() : entry.widget},
        {u"swatch"_s, QString()},
        {u"hue"_s, -1},
        {u"current"_s, entry.shown},
        {u"confirm"_s, entry.confirm},
    };
    if (entry.widget == "hue"_L1 && !lockedNow) {
        const int shown = entry.shown.toInt();
        row.insert(u"hue"_s, normalizeHue(shown));
        if (entry.key == LedHueKey)
            row.insert(u"swatch"_s, QColor::fromRgb(MicroSet::hueColor(shown, 100)).name());
    }
    return row;
}

QVariantMap SettingsModel::label(const QString &owner)
{
    return {{u"kind"_s, u"label"_s}, {u"key"_s, u"label-"_s + owner}, {u"title"_s, owner}, {u"value"_s, QString()},
            {u"readOnly"_s, true}, {u"widget"_s, QString()}, {u"swatch"_s, QString()}, {u"hue"_s, -1},
            {u"confirm"_s, false}};
}

QList<const SettingsModel::Entry *> SettingsModel::visible(const QString &group) const
{
    QList<const Entry *> out;
    for (const Entry &entry : m_entries) {
        if (titleOf(entry.group) != group || (kindOf(entry) == "info"_L1 && textOf(entry, entry.shown).isEmpty()))
            continue;
        out << &entry;
    }
    std::stable_sort(out.begin(), out.end(), [](const Entry *a, const Entry *b) {
        if (a->owner != b->owner)
            return ownerRank(a->owner) < ownerRank(b->owner);
        const int ga = a->groupOrder.value_or(Last);
        const int gb = b->groupOrder.value_or(Last);
        if (ga != gb || a->group != b->group)
            return ga != gb ? ga < gb : a->group < b->group;
        const int x = a->order.value_or(Last);
        const int y = b->order.value_or(Last);
        return x != y ? x < y : a->key < b->key;
    });
    return out;
}

QVariantList SettingsModel::updateRows() const
{
    QVariantList out;
    const QString image = m_updates.imageVersion();
    const QVariantMap base{{u"kind"_s, u"info"_s}, {u"readOnly"_s, true}, {u"widget"_s, QString()},
                           {u"swatch"_s, QString()}, {u"hue"_s, -1}, {u"confirm"_s, false}};
    if (!image.isEmpty()) {
        QVariantMap row = base;
        row.insert({{u"key"_s, u"image"_s}, {u"title"_s, u"image"_s}, {u"value"_s, image}});
        out << row;
    }
    QVariantMap row = base;
    row.insert({{u"key"_s, u"update"_s}, {u"title"_s, u"system update"_s},
                {u"value"_s, UpdateInfo::text(m_updates.status(), m_updates.version())}});
    out << row;
    return out;
}

QVariantList SettingsModel::rows(const QString &group, int) const
{
    QVariantList out;
    QString owner;
    bool placed = group != UpdatesGroup;
    const auto place = [&] {
        if (owner != PrivilegedOwner)
            out << label(PrivilegedOwner);
        out << updateRows();
        owner = PrivilegedOwner;
        placed = true;
    };
    for (const Entry *entry : visible(group)) {
        if (!placed && entry->owner != PrivilegedOwner)
            place();
        if (out.isEmpty() || entry->owner != owner) {
            owner = entry->owner;
            out << label(owner);
        }
        out << rowOf(*entry);
    }
    if (!placed)
        place();
    return out;
}

QVariantList SettingsModel::groups(int) const
{
    struct Item
    {
        QString title;
        QString owner;
        int order;
    };
    QMap<QString, Item> found;
    for (const Entry &entry : m_entries) {
        const QString title = titleOf(entry.group);
        Item &item = found[title];
        if (item.title.isEmpty())
            item = {title, entry.owner, Last};
        if (ownerRank(entry.owner) < ownerRank(item.owner))
            item.owner = entry.owner;
        if (entry.groupOrder)
            item.order = std::min(item.order, *entry.groupOrder);
    }
    QList<Item> items;
    for (const Item &item : std::as_const(found))
        if (item.title != UpdatesGroup && !visible(item.title).isEmpty())
            items << item;
    items << Item{UpdatesGroup, PrivilegedOwner, Last};
    std::stable_sort(items.begin(), items.end(), [](const Item &a, const Item &b) {
        if (a.owner != b.owner)
            return ownerRank(a.owner) < ownerRank(b.owner);
        if ((a.title == UpdatesGroup) != (b.title == UpdatesGroup))
            return b.title == UpdatesGroup;
        return a.order != b.order ? a.order < b.order : a.title < b.title;
    });
    QVariantList out;
    QString section;
    for (const Item &item : std::as_const(items)) {
        if (out.isEmpty() || item.owner != section) {
            section = item.owner;
            out << label(section);
        }
        out << QVariantMap{{u"kind"_s, u"row"_s}, {u"key"_s, u"group-"_s + item.title}, {u"title"_s, item.title},
                           {u"value"_s, QString()}, {u"readOnly"_s, true}, {u"group"_s, item.title},
                           {u"widget"_s, QString()}, {u"swatch"_s, QString()}, {u"hue"_s, -1}, {u"confirm"_s, false}};
    }
    return out;
}

bool SettingsModel::beginEdit(const QString &key)
{
    const auto it = m_entries.constFind(key);
    if (it == m_entries.cend() || !editable(*it))
        return false;
    if (!m_fixture && !m_available) {
        emit refused(it->title, NotAvailable);
        return false;
    }
    return true;
}

QVariant SettingsModel::next(const QString &key, const QVariant &from, int delta) const
{
    const auto it = m_entries.constFind(key);
    if (it == m_entries.cend() || !editable(*it))
        return {};
    const Entry &entry = *it;
    const QVariant base = coerce(entry.type, from).isValid() ? coerce(entry.type, from) : entry.shown;
    if (entry.type == "b"_L1)
        return !base.toBool();
    const QVariantList &list = entry.choices.isEmpty() ? entry.steps : entry.choices;
    if (!list.isEmpty()) {
        qsizetype index = list.indexOf(base);
        if (index < 0)
            index = nearest(list, base);
        return list.at(std::clamp<qsizetype>(index + delta, 0, list.size() - 1));
    }
    if (!isNumeric(entry.type) || !entry.min || !entry.max)
        return {};
    const double target = base.toDouble() + delta * entry.step;
    double value = target >= *entry.max ? *entry.max
        : target <= *entry.min           ? *entry.min
                                         : *entry.min + std::round((target - *entry.min) / entry.step) * entry.step;
    value = std::clamp(value, *entry.min, *entry.max);
    return coerce(entry.type, value);
}

QString SettingsModel::text(const QString &key, const QVariant &value) const
{
    const auto it = m_entries.constFind(key);
    if (it == m_entries.cend())
        return {};
    const QVariant typed = coerce(it->type, value);
    return textOf(*it, typed.isValid() ? typed : it->shown);
}

bool SettingsModel::set(const QString &key, const QVariant &value)
{
    const auto it = m_entries.find(key);
    if (it == m_entries.end() || !editable(*it))
        return false;
    const QVariant wanted = coerce(it->type, value);
    if (!wanted.isValid())
        return false;
    if (!m_fixture && !m_available) {
        emit refused(it->title, NotAvailable);
        return false;
    }
    if (it->shown == wanted)
        return true;
    it->shown = wanted;
    if (m_fixture) {
        it->store = wanted;
        bump();
        return true;
    }
    ++it->inflight;
    bump();
    m_client->set(key, wanted, [this, key](const MicroSet::Error &error) { settle(key, error.name); });
    return true;
}

void SettingsModel::settle(const QString &key, const QString &error)
{
    const auto it = m_entries.find(key);
    if (it == m_entries.end())
        return;
    it->inflight = std::max(0, it->inflight - 1);
    if (!error.isEmpty())
        emit refused(it->title, toastText(error));
    if (it->inflight > 0)
        return;
    if (it->overridable && m_available) {
        refreshStored(key);
        return;
    }
    if (it->shown == it->store)
        return;
    it->shown = it->store;
    bump();
}

void SettingsModel::reloadUpdates()
{
    if (!m_fixture)
        m_updates.reload();
}

QStringList SettingsModel::liveKeys(const QString &group) const
{
    QStringList out;
    for (const Entry *entry : visible(group))
        if (m_live.contains(entry->lockShow) && !out.contains(entry->lockShow))
            out << entry->lockShow;
    return out;
}

void SettingsModel::enter(const QString &group)
{
    if (!m_client)
        return;
    for (const QString &key : liveKeys(group))
        if (m_watching[key]++ == 0)
            m_client->subscribe(key);
}

void SettingsModel::leave(const QString &group)
{
    if (!m_client)
        return;
    for (const QString &key : liveKeys(group)) {
        const auto it = m_watching.find(key);
        if (it == m_watching.end())
            continue;
        if (--*it == 0) {
            m_watching.erase(it);
            m_client->unsubscribe(key);
        }
    }
}
