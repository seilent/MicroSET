#include "settingsmodel.h"

#include <microset/client.h>
#include <microset/color.h>

#include <QColor>
#include <QDBusArgument>
#include <QDBusPendingCallWatcher>
#include <QDBusServiceWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace Qt::StringLiterals;

namespace {

const QString Service = u"net.seilent.MicroSet"_s;
const QString Path = u"/net/seilent/MicroSet"_s;
const QString Interface = u"net.seilent.MicroSet1"_s;
const QString PrivilegedOwner = u"microset"_s;
const QString AppPrefix = u"app."_s;
const QRegularExpression AppKey(u"^app\\.([1-9][0-9]{0,9})\\.(.+)$"_s);
const QRegularExpression AppPage(u"^app\\.([1-9][0-9]{0,9})$"_s);
constexpr int Last = std::numeric_limits<int>::max();
constexpr int MaxSteps = 64;
constexpr uint PanelHz = 60;
const QString LsfgEnabledKey = u"lsfg.enabled"_s;
const QString LsfgModeKey = u"lsfg.mode"_s;
const QString LsfgMultiplierKey = u"lsfg.multiplier"_s;
const QString LsfgCapKey = u"lsfg.fps_cap"_s;
const QString LsfgStateOption = u"lsfg.state"_s;
const QString LsfgGamesKey = u"lsfg-games"_s;
const QString LsfgGamePrefix = u"lsfg-game-"_s;
const QString LsfgInstallKey = u"lsfg-install"_s;
const QString LsfgOffText = u"Frame generation is off"_s;
const QString AndroidGroup = u"lsfg.android"_s;
const QString AndroidAppKey = u"lsfg.android_app"_s;
const QString AndroidStateKey = u"lsfg.android_state"_s;
const QString AndroidFpsKey = u"lsfg.android_fps"_s;
constexpr qsizetype AndroidAppMax = 128;
const QString PerfOverlayKey = u"perf.android_overlay"_s;
const QString ProfilesKey = u"proton.profiles"_s;
const QString ProfileOption = u"profile"_s;
constexpr qsizetype MaxGames = 512;

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

int decimalsOf(double step)
{
    int decimals = 0;
    for (double scaled = step; decimals < 6 && std::abs(scaled - std::round(scaled)) > 1e-9; scaled *= 10)
        ++decimals;
    return decimals;
}

}

SettingsModel::SettingsModel(QObject *parent)
    : QObject(parent)
    , m_bus(QString())
    , m_fixture(true)
{
    connect(&m_updates, &UpdateInfo::changed, this, &SettingsModel::bump);
    connect(&m_lsfg, &LsfgState::changed, this, &SettingsModel::bump);
}

SettingsModel::SettingsModel(const QDBusConnection &bus, QObject *parent)
    : QObject(parent)
    , m_client(std::make_unique<MicroSet::Client>(bus))
    , m_bus(bus)
{
    connect(m_client.get(), &MicroSet::Client::changed, this, &SettingsModel::onChanged);
    connect(m_client.get(), &MicroSet::Client::holdRefused, this, &SettingsModel::onHoldRefused);
    auto *watcher = new QDBusServiceWatcher(Service, bus, QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, this, &SettingsModel::onOwnerChanged);
    connect(&m_updates, &UpdateInfo::changed, this, &SettingsModel::bump);
    connect(&m_lsfg, &LsfgState::changed, this, &SettingsModel::bump);
    m_updates.watch();
    m_lsfg.watch();
}

SettingsModel::~SettingsModel()
{
    endPreviews();
}

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
    call(u"Schema"_s, {QString()}, [this, failed, generation](const QDBusMessage &reply) {
        const QJsonDocument document = QJsonDocument::fromJson(reply.arguments().value(0).toString().toUtf8());
        if (failed(errorOf(reply)) || (!document.isObject() && failed(u"bad schema"_s)))
            return;
        const QJsonObject schema = document.object();
        call(u"List"_s, {QString()}, [this, failed, generation, schema](const QDBusMessage &reply) {
            if (failed(errorOf(reply)))
                return;
            const QVariantMap values = qdbus_cast<QVariantMap>(reply.arguments().value(0));
            call(u"Stored"_s, {QString()}, [this, failed, generation, schema, values](const QDBusMessage &reply) {
                if (failed(errorOf(reply)))
                    return;
                build(schema, values, qdbus_cast<QVariantMap>(reply.arguments().value(0)));
                setAvailable(true);
                keep();
                joinGames();
                queueResolve();
                bump();
                for (const QString &appid : m_effective.keys())
                    fetchEffective(appid);
                call(u"Profiles"_s, {}, [this, generation](const QDBusMessage &reply) {
                    const QByteArray json = reply.arguments().value(0).toString().toUtf8();
                    const QJsonDocument document = QJsonDocument::fromJson(json);
                    if (generation != m_generation || !errorOf(reply).isEmpty() || !document.isObject())
                        return;
                    m_profiles = document.object();
                    keep();
                    joinGames();
                    bump();
                });
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

void SettingsModel::setProtonFixture(const QMap<QString, QJsonObject> &effective, const QJsonObject &profiles)
{
    m_effectiveFixture = effective;
    m_profiles = profiles;
    keep();
    bump();
}

void SettingsModel::setGame(const QVariantMap &game)
{
    if (game == m_game)
        return;
    m_game = game;
    const QString appid = game.value(u"appid"_s).toString();
    if (!appid.isEmpty()) {
        if (!m_names.contains(appid)) {
            const QString name = SteamLibrary(SteamLibrary::defaultRoot()).name(appid);
            if (!name.isEmpty())
                m_names.insert(appid, name);
        }
        fetchEffective(appid);
    }
    bump();
}

void SettingsModel::setAndroidApp(const QString &package)
{
    if (package == m_androidApp)
        return;
    m_androidApp = package;
    bump();
}

UpdateInfo *SettingsModel::updates()
{
    return &m_updates;
}

LsfgState *SettingsModel::lsfg()
{
    return &m_lsfg;
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

bool SettingsModel::accentOff() const
{
    return m_values.value(AccentOffKey).toBool();
}

QString SettingsModel::updatesGroup() const
{
    return UpdatesGroup;
}

QString SettingsModel::accentKey() const
{
    return AccentKey;
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
    if (error == "org.freedesktop.DBus.Error.LimitsExceeded"_L1)
        return TryLater;
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

std::optional<SettingsModel::Entry> SettingsModel::entryOf(const QString &key, const QJsonObject &object)
{
    const QJsonObject ui = object.value("ui"_L1).toObject();
    Entry entry;
    entry.key = key;
    entry.group = object.value("group"_L1).toString();
    entry.widget = ui.value("widget"_L1).toString();
    entry.type = object.value("type"_L1).toString();
    if (entry.group.isEmpty() || entry.widget == "none"_L1 || entry.type.isEmpty())
        return std::nullopt;
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
    if (object.contains("floor"_L1))
        entry.floor = object.value("floor"_L1).toDouble();
    entry.step = ui.value("step"_L1).toDouble(1);
    entry.scale = ui.value("scale"_L1).toDouble(0);
    entry.labels = ui.value("values"_L1).toObject();
    entry.leads = ui.value("leads"_L1).toObject();
    entry.unit = ui.value("unit"_L1).toString();
    entry.confirm = ui.value("apply"_L1).toString() == "confirm"_L1;
    entry.bounded = ui.value("bounded"_L1).toBool();
    const QJsonObject lock = ui.value("lock"_L1).toObject();
    entry.lockKey = lock.value("key"_L1).toString();
    entry.lockIs = lock.value("is"_L1);
    entry.lockText = lock.value("text"_L1).toString();
    entry.lockShow = lock.value("show"_L1).toString();
    entry.writable = object.value("writable"_L1).toBool(true);
    entry.overridable = object.value("override"_L1).toBool(false);
    entry.defaultValue = coerce(entry.type, object.value("default"_L1).toVariant());
    return entry;
}

void SettingsModel::restore(Entry &entry, const QVariant &raw) const
{
    const QVariant value = coerce(entry.type, raw);
    entry.store = value.isValid() ? value : entry.defaultValue;
    entry.shown = entry.store;
    const auto old = m_entries.constFind(entry.key);
    if (old != m_entries.cend() && old->inflight > 0) {
        entry.inflight = old->inflight;
        entry.shown = old->shown;
    }
}

void SettingsModel::instantiate(QMap<QString, Entry> &entries, const QString &appid) const
{
    for (auto it = m_templates.cbegin(); it != m_templates.cend(); ++it) {
        QJsonObject object = it.value();
        object.insert(u"group"_s, AppPrefix + appid);
        std::optional<Entry> entry = entryOf(AppPrefix + appid + u'.' + it.key(), object);
        if (!entry)
            continue;
        entry->appid = appid;
        entry->option = it.key();
        restore(*entry, m_values.value(entry->key));
        entries.insert(entry->key, *entry);
    }
}

void SettingsModel::build(const QJsonObject &schema, const QVariantMap &values, const QVariantMap &stored)
{
    QMap<QString, Entry> entries;
    QMap<QString, QJsonObject> templates;
    QMap<QString, QString> groupLabels;
    QSet<QString> live;
    QSet<QString> referenced;
    QVariantMap lsfgDefaults;
    for (auto it = schema.begin(); it != schema.end(); ++it) {
        QJsonObject object = it.value().toObject();
        if (it.key().startsWith("lsfg.android_"_L1) || it.key() == PerfOverlayKey)
            object.insert(u"group"_s, AndroidGroup);
        else if (it.key().startsWith("lsfg."_L1))
            lsfgDefaults.insert(it.key(),
                                coerce(object.value("type"_L1).toString(), object.value("default"_L1).toVariant()));
        if (object.value("per_app"_L1).toBool() && it.key().startsWith(AppPrefix)) {
            templates.insert(it.key().mid(AppPrefix.size()), object);
            continue;
        }
        const QString groupLabel = object.value("group_ui"_L1).toObject().value("label"_L1).toString();
        if (!groupLabel.isEmpty())
            groupLabels.insert(object.value("group"_L1).toString(), groupLabel);
        if (object.value("live"_L1).toBool())
            live.insert(it.key());
        std::optional<Entry> entry = entryOf(it.key(), object);
        if (!entry)
            continue;
        if (!entry->lockKey.isEmpty())
            referenced << entry->lockKey;
        if (!entry->lockShow.isEmpty())
            referenced << entry->lockShow;
        if (entry->key == UndervoltKey)
            referenced << UndervoltActiveKey;
        restore(*entry, (entry->overridable ? stored : values).value(entry->key));
        entries.insert(entry->key, *entry);
    }
    m_templates = templates;
    m_values = values;
    for (const QString &appid : std::as_const(m_apps))
        instantiate(entries, appid);
    m_entries = entries;
    m_groupLabels = groupLabels;
    m_live = live;
    referenced << AccentOffKey;
    m_referenced = referenced;
    m_accentOffKnown = schema.contains(AccentOffKey);
    m_lsfgDefaults = lsfgDefaults;
}

void SettingsModel::setAvailable(bool available)
{
    if (m_available == available)
        return;
    m_available = available;
    if (!available)
        dropPreview();
    emit availableChanged();
}

void SettingsModel::bump()
{
    if (m_bumpQueued)
        return;
    m_bumpQueued = true;
    QMetaObject::invokeMethod(
        this,
        [this] {
            m_bumpQueued = false;
            ++m_revision;
            emit revisionChanged();
        },
        Qt::QueuedConnection);
}

void SettingsModel::refreshStored(const QString &key)
{
    call(u"Stored"_s, {key}, [this, key](const QDBusMessage &reply) {
        const auto it = m_entries.find(key);
        if (it == m_entries.end() || !errorOf(reply).isEmpty())
            return;
        const QVariant value = coerce(it->type, qdbus_cast<QVariantMap>(reply.arguments().value(0)).value(key));
        const QVariant before = it->store;
        it->store = value.isValid() ? value : it->defaultValue;
        if (it->inflight > 0 || it->shown == it->store) {
            if (it->store != before && m_referenced.contains(key))
                bump();
            return;
        }
        it->shown = it->store;
        bump();
        watchPreview(*it);
    });
}

void SettingsModel::onChanged(const QString &key, const QVariant &value)
{
    m_values.insert(key, value);
    refreshEffective(key);
    const QRegularExpressionMatch app = AppKey.match(key);
    const QString option = app.captured(2);
    if (key == ProfilesKey) {
        keep();
        joinGames();
    } else if (app.hasMatch()) {
        joinApp(app.captured(1), option, value);
    }
    if (key.startsWith("lsfg."_L1) || key == ProfilesKey || option.startsWith("lsfg."_L1) || option == ProfileOption)
        queueResolve();
    const auto it = m_entries.find(key);
    if (it == m_entries.end()) {
        if (m_referenced.contains(key) || m_watching.contains(key) || key.startsWith(AppPrefix))
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
        watchPreview(*it);
    } else if (m_referenced.contains(key)) {
        bump();
    }
}

void SettingsModel::onHoldRefused(const QString &key, const QString &reason)
{
    const auto it = m_entries.constFind(key);
    if (it != m_entries.cend())
        emit refused(it->title, reason);
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

QString SettingsModel::pickOf(const Entry &entry) const
{
    if (!editable(entry))
        return {};
    if (entry.type == "b"_L1)
        return u"sheet"_s;
    if (entry.widget == "hue"_L1)
        return u"swatch"_s;
    if (isNumeric(entry.type) && (entry.widget == "edit"_L1 || sheetValues(entry).isEmpty()))
        return u"adjust"_s;
    return u"sheet"_s;
}

QVariantList SettingsModel::sheetValues(const Entry &entry) const
{
    if (entry.type == "b"_L1)
        return {false, true};
    QVariantList values = !entry.choices.isEmpty() ? entry.choices : entry.steps;
    if (values.isEmpty() && isNumeric(entry.type) && entry.min && entry.max) {
        const double span = (*entry.max - *entry.min) / entry.step;
        const double whole = std::floor(span + 1e-9);
        if (whole + (span - whole > 1e-9 ? 2 : 1) > MaxSteps)
            return {};
        const double scale = std::pow(10.0, decimalsOf(entry.step));
        for (int k = 0; k <= int(whole); ++k)
            values << coerce(entry.type, std::round((*entry.min + k * entry.step) * scale) / scale);
        if (span - whole > 1e-9)
            values << coerce(entry.type, *entry.max);
    }
    if (!isNumeric(entry.type))
        return values;
    if (values.size() > MaxSteps)
        return {};
    if (entry.floor)
        values.removeIf([&](const QVariant &value) { return value.toDouble() < *entry.floor; });
    std::sort(values.begin(), values.end(), [](const QVariant &a, const QVariant &b) { return a.toDouble() > b.toDouble(); });
    return values;
}

QVariant SettingsModel::shownOf(const Entry &entry) const
{
    if (m_preview && m_preview->override && m_preview->key == entry.key && m_preview->last.isValid())
        return m_preview->last;
    if (entry.key == AndroidStateKey || entry.key == AndroidFpsKey) {
        const auto app = m_entries.constFind(AndroidAppKey);
        if (app == m_entries.cend() || app->shown.toString() != m_androidApp)
            return entry.defaultValue;
    }
    return entry.shown;
}

QString SettingsModel::textOf(const Entry &entry, const QVariant &value) const
{
    if (!entry.appid.isEmpty() && value.toString() == "auto"_L1)
        return autoText(entry);
    if (entry.widget == "hue"_L1)
        return {};
    if (entry.type == "b"_L1)
        return value.toBool() ? u"on"_s : u"off"_s;
    if (entry.type == "as"_L1)
        return value.toStringList().join(u", "_s);
    const QString raw = entry.type == "d"_L1 ? QString::number(value.toDouble()) : value.toString();
    const QJsonValue label = entry.labels.value(raw);
    if (label.isString())
        return label.toString();
    if (entry.key == LsfgCapKey && raw == "auto"_L1) {
        const auto multiplier = m_entries.constFind(LsfgMultiplierKey);
        const uint times = multiplier == m_entries.cend() ? 0 : multiplier->shown.toUInt();
        if (times > 0)
            return u"auto (%1)"_s.arg(PanelHz / times);
    }
    const bool scaled = entry.scale > 0 && !raw.isEmpty();
    const QString number = entry.type == "d"_L1 ? QString::number(value.toDouble(), 'f', decimalsOf(entry.step)) : raw;
    const QString text = scaled ? QString::number(std::round(value.toDouble() / entry.scale)) : number;
    return entry.unit.isEmpty() || text.isEmpty() ? text : text + u' ' + entry.unit;
}

QString SettingsModel::rowText(const Entry &entry, const QVariant &value) const
{
    return entry.option == "profile"_L1 ? profileText(entry, value) : textOf(entry, value);
}

QString SettingsModel::autoText(const Entry &entry) const
{
    const QJsonObject own = m_effective.value(entry.appid).value("options"_L1).toObject().value(entry.option).toObject();
    const QString source = own.value("source"_L1).toString();
    if (source.isEmpty() || source == "set"_L1)
        return u"auto"_s;
    if (entry.option == "fex.preset"_L1)
        return u"auto (%1)"_s.arg(source);
    const QString value = own.value("value"_L1).toString();
    if (entry.option == LsfgStateOption && source == "default"_L1)
        return u"auto (%1, %2)"_s.arg(value, own.value("mode"_L1).toString());
    if (entry.option == LsfgMultiplierKey || entry.option == LsfgCapKey) {
        const QJsonValue label = entry.labels.value(value);
        return u"auto (%1)"_s.arg(label.isString() ? label.toString() : value);
    }
    const QString word = value.isEmpty() ? u"default"_s : value;
    return word == source ? u"auto (%1)"_s.arg(word) : u"auto (%1, %2)"_s.arg(word, source);
}

QStringList SettingsModel::templateOrder() const
{
    QStringList out = m_templates.keys();
    std::stable_sort(out.begin(), out.end(), [this](const QString &a, const QString &b) {
        const int x = m_templates.value(a).value("ui"_L1).toObject().value("order"_L1).toInt(Last);
        const int y = m_templates.value(b).value("ui"_L1).toObject().value("order"_L1).toInt(Last);
        return x < y;
    });
    return out;
}

QString SettingsModel::profileText(const Entry &entry, const QVariant &value) const
{
    const QString text = textOf(entry, value);
    const QJsonObject profile = m_effective.value(entry.appid).value("profile"_L1).toObject();
    if (value != entry.store || !profile.value("on"_L1).toBool())
        return text;
    const QJsonObject set = profile.value("set"_L1).toObject();
    const QJsonArray overridden = profile.value("overridden"_L1).toArray();
    QStringList parts;
    for (const QString &option : templateOrder()) {
        if (!set.contains(option))
            continue;
        QString part = entryText(option, set.value(option).toString());
        if (overridden.contains(QJsonValue(option)))
            part += u" overridden"_s;
        parts << part;
    }
    return parts.isEmpty() ? text : text + u" ("_s + parts.join(u", "_s) + u')';
}

QString SettingsModel::entryText(const QString &option, const QString &value) const
{
    if (!option.startsWith("fex."_L1) && !option.startsWith("lsfg."_L1) && option != ProfileOption)
        return value;
    const QJsonObject shape = m_templates.value(option);
    const QJsonValue label = shape.value("ui"_L1).toObject().value("values"_L1).toObject().value(value);
    return shape.value("label"_L1).toString() + u' ' + (label.isString() ? label.toString() : value);
}

QString SettingsModel::leadOf(const Entry &entry, const QVariant &value) const
{
    QString raw = entry.type == "d"_L1 ? QString::number(value.toDouble()) : value.toString();
    if (!entry.appid.isEmpty() && raw == "auto"_L1) {
        const QJsonObject own = m_effective.value(entry.appid).value("options"_L1).toObject().value(entry.option).toObject();
        if (own.value("source"_L1).toString() == "set"_L1)
            return {};
        raw = own.value("value"_L1).toString();
    }
    if (entry.option != "profile"_L1 || raw != "on"_L1)
        return entry.leads.value(raw).toString();
    const QJsonObject profile = m_effective.value(entry.appid).value("profile"_L1).toObject();
    const QJsonObject set = profile.value("set"_L1).toObject();
    const QJsonArray overridden = profile.value("overridden"_L1).toArray();
    for (const QString &option : templateOrder()) {
        const QString word = set.value(option).toString();
        const QString lead = m_templates.value(option).value("ui"_L1).toObject().value("leads"_L1).toObject().value(word).toString();
        if (!word.isEmpty() && !lead.isEmpty() && !overridden.contains(QJsonValue(option)))
            return entryText(option, word) + u": "_s + lead;
    }
    return {};
}

QString SettingsModel::undervoltText(const Entry &entry) const
{
    const QString preset = entry.shown.toString();
    const QString active = m_values.value(UndervoltActiveKey).toString();
    const QString text = textOf(entry, entry.shown);
    if (entry.inflight > 0 || preset == active || (preset == "off"_L1 && (active.isEmpty() || active == "stock"_L1)))
        return text;
    if (active == "waiting"_L1 && preset != "off"_L1)
        return u"Stock until GPU restart"_s;
    return text + u", next boot"_s;
}

QVariantMap SettingsModel::rowOf(const Entry &entry) const
{
    const QString kind = kindOf(entry);
    const bool lockedNow = locked(entry);
    const QVariant shown = shownOf(entry);
    QString value = entry.key == UndervoltKey ? undervoltText(entry) : rowText(entry, shown);
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
        {u"current"_s, shown},
        {u"confirm"_s, entry.confirm},
        {u"pick"_s, pickOf(entry)},
        {u"bounded"_s, entry.bounded},
        {u"min"_s, entry.min ? QVariant(*entry.min) : QVariant()},
        {u"max"_s, entry.max ? QVariant(*entry.max) : QVariant()},
        {u"unit"_s, entry.unit},
        {u"decimals"_s, decimalsOf(entry.step)},
    };
    if (entry.widget == "hue"_L1 && !lockedNow) {
        const int hue = shown.toInt();
        row.insert(u"hue"_s, normalizeHue(hue));
        if (entry.key == AccentKey && accentOff())
            row.insert(u"swatch"_s, u"#ffffff"_s);
        if (entry.key == LedHueKey) {
            const auto saturation = m_entries.constFind(LedSaturationKey);
            const int percent = saturation == m_entries.cend() ? 100 : shownOf(*saturation).toInt();
            row.insert(u"swatch"_s, QColor::fromRgb(MicroSet::hueColor(hue, percent)).name());
        }
    }
    return row;
}

QVariantMap SettingsModel::infoRow(const QString &key, const QString &title, const QString &value)
{
    return {{u"kind"_s, u"info"_s}, {u"key"_s, key}, {u"title"_s, title}, {u"value"_s, value}, {u"readOnly"_s, true},
            {u"widget"_s, QString()}, {u"swatch"_s, QString()}, {u"hue"_s, -1}, {u"confirm"_s, false},
            {u"pick"_s, QString()}, {u"bounded"_s, false}};
}

QVariantMap SettingsModel::label(const QString &owner)
{
    return {{u"kind"_s, u"label"_s}, {u"key"_s, u"label-"_s + owner}, {u"title"_s, owner}, {u"value"_s, QString()},
            {u"readOnly"_s, true}, {u"widget"_s, QString()}, {u"swatch"_s, QString()}, {u"hue"_s, -1},
            {u"confirm"_s, false}, {u"pick"_s, QString()}, {u"bounded"_s, false}};
}

QList<const SettingsModel::Entry *> SettingsModel::visible(const QString &group) const
{
    QList<const Entry *> out;
    const auto fallback = m_entries.constFind(TuningFallbackKey);
    const bool fallbackShown = fallback != m_entries.cend() && !textOf(*fallback, fallback->shown).isEmpty();
    const bool lsfg = lsfgShown();
    for (const Entry &entry : m_entries) {
        if (entry.group == AndroidGroup)
            continue;
        if (!lsfg && (entry.group == LsfgGroup || (!entry.appid.isEmpty() && entry.option.startsWith("lsfg."_L1))))
            continue;
        if (titleOf(entry.group) != group || (kindOf(entry) == "info"_L1 && textOf(entry, entry.shown).isEmpty()))
            continue;
        if (entry.key == TuningTripKey && fallbackShown)
            continue;
        if (entry.option == "profile"_L1 && !m_effective.value(entry.appid).contains("profile"_L1))
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
                           {u"swatch"_s, QString()}, {u"hue"_s, -1}, {u"confirm"_s, false},
                           {u"pick"_s, QString()}, {u"bounded"_s, false}};
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

QSet<QString> SettingsModel::heldApps(const QString &prefix) const
{
    QSet<QString> found;
    for (auto it = m_values.cbegin(); it != m_values.cend(); ++it) {
        const QRegularExpressionMatch match = AppKey.match(it.key());
        const auto shape = m_templates.constFind(match.captured(2));
        if (!match.hasMatch() || shape == m_templates.cend() || !match.captured(2).startsWith(prefix))
            continue;
        if (it.value() != shape->value("default"_L1).toVariant())
            found.insert(match.captured(1));
    }
    return found;
}

QStringList SettingsModel::storedApps() const
{
    QStringList out;
    for (const QString &appid : heldApps())
        if (!SteamLibrary::excluded().contains(appid))
            out << appid;
    std::sort(out.begin(), out.end(), [](const QString &a, const QString &b) { return a.toULongLong() < b.toULongLong(); });
    return out;
}

QString SettingsModel::listOf(const QString &group) const
{
    if (group == GamesGroup)
        return GamesGroup;
    return group == titleOf(LsfgGroup) ? LsfgGroup : QString();
}

QSet<QString> SettingsModel::listedNow(const QString &list) const
{
    const bool lsfg = list == LsfgGroup;
    const QStringList own = lsfg ? lsfgApps() : storedApps();
    QSet<QString> out(own.cbegin(), own.cend());
    if (!profilesOn())
        return out;
    for (const SteamLibrary::Game &game : m_library) {
        const QJsonObject entry = m_profiles.value(game.appid).toObject();
        if (!entry.isEmpty() && (!lsfg || entry.value("set"_L1).toObject().contains(LsfgStateOption)))
            out.insert(game.appid);
    }
    return out;
}

QSet<QString> SettingsModel::listed(const QString &list) const
{
    return listedNow(list) + m_kept.value(list);
}

QList<SteamLibrary::Game> SettingsModel::shownGames(const QSet<QString> &listed) const
{
    QList<SteamLibrary::Game> games;
    for (const SteamLibrary::Game &game : m_library)
        if (listed.contains(game.appid))
            games << game;
    QList<SteamLibrary::Game> dropped;
    for (const SteamLibrary::Game &game : m_libraryDropped)
        if (listed.contains(game.appid))
            dropped << game;
    QList<SteamLibrary::Game> out;
    std::merge(games.cbegin(), games.cend(), dropped.cbegin(), dropped.cend(), std::back_inserter(out),
               &SteamLibrary::inOrder);
    return out;
}

QStringList SettingsModel::missingApps(const QSet<QString> &listed) const
{
    QStringList out;
    for (const QString &appid : listed)
        if (!installed(appid))
            out << appid;
    std::sort(out.begin(), out.end(), [](const QString &a, const QString &b) { return a.toULongLong() < b.toULongLong(); });
    return out;
}

bool SettingsModel::installed(const QString &appid) const
{
    return m_present.contains(appid);
}

QVariantList SettingsModel::pickableNow(const QString &list) const
{
    const QSet<QString> shown = listed(list);
    const QSet<QString> held = heldApps();
    const bool full = held.size() >= MaxGames;
    if (full && list == GamesGroup)
        return {};
    QVariantList out;
    for (const SteamLibrary::Game &game : m_library)
        if (!shown.contains(game.appid) && (!full || held.contains(game.appid)))
            out << QVariantMap{{u"appid"_s, game.appid}, {u"title"_s, game.name}};
    return out;
}

QVariantMap SettingsModel::addRow(const QString &list) const
{
    QVariantMap row = infoRow(list + u"-add"_s, u"Add game"_s, QString());
    row.insert({{u"kind"_s, list == LsfgGroup ? u"sub"_s : u"row"_s}, {u"picker"_s, list}});
    return row;
}

QString SettingsModel::gameText(const QString &appid) const
{
    const QJsonObject options = m_effective.value(appid).value("options"_L1).toObject();
    QStringList parts;
    for (const QString &option : templateOrder()) {
        if (option == ProfileOption) {
            if (m_values.value(AppPrefix + appid + u'.' + option).toString() == "off"_L1)
                parts << entryText(option, u"off"_s);
            continue;
        }
        const QJsonObject entry = options.value(option).toObject();
        const QString source = entry.value("source"_L1).toString();
        if (source == "set"_L1 || source == "profile"_L1)
            parts << entryText(option, entry.value("value"_L1).toString());
    }
    return parts.isEmpty() ? u"default"_s : parts.join(u", "_s);
}

QVariantMap SettingsModel::gameRow(const QString &appid, const QString &title) const
{
    QVariantMap row = infoRow(u"game-"_s + appid, title, gameText(appid));
    row.insert({{u"kind"_s, u"row"_s}, {u"group"_s, AppPrefix + appid}});
    return row;
}

QVariantList SettingsModel::gameRows() const
{
    QVariantList out;
    if (m_templates.isEmpty() || !m_libraryRead)
        return out;
    const QSet<QString> shown = listed(GamesGroup);
    for (const SteamLibrary::Game &game : shownGames(shown))
        if (m_effective.contains(game.appid))
            out << gameRow(game.appid, game.name);
    for (const QString &appid : missingApps(shown)) {
        QVariantMap row = gameRow(appid, m_names.value(appid, appid));
        row.insert(u"value"_s, u"not installed"_s);
        out << row;
    }
    if (!pickableNow(GamesGroup).isEmpty())
        out << addRow(GamesGroup);
    return out;
}

QVariantList SettingsModel::thisGame() const
{
    const QString appid = m_game.value(u"appid"_s).toString();
    if (appid.isEmpty() || m_templates.isEmpty())
        return {};
    QVariantMap row = gameRow(appid, pageTitle(AppPrefix + appid, 0));
    row.insert(u"value"_s, QString());
    QVariantList out{label(u"this game"_s), row};
    const QString hook = m_game.value(u"hook"_s).toString();
    if (hook.isEmpty() && !m_game.value(u"proton"_s).toBool())
        return out;
    const QString state = u"settings not reaching the game"_s;
    if (m_game.value(u"gone"_s).toBool())
        out << infoRow(u"game-state"_s, state, u"unknown"_s);
    else if (hook.isEmpty())
        out << infoRow(u"game-missing"_s, u"hook missing in"_s, m_game.value(u"tool"_s).toString());
    else if (hook != "ok"_L1)
        out << infoRow(u"game-state"_s, state, QString(hook).replace(u',', u", "_s));
    const QJsonObject profile = m_effective.value(appid).value("profile"_L1).toObject();
    const bool ran = !hook.isEmpty() && !m_game.value(u"gone"_s).toBool();
    if (ran && m_game.value(u"profile"_s).toString() == "other-tool"_L1)
        out << infoRow(u"game-profile"_s, u"profile not for this Proton"_s, QString());
    else if (ran && !profile.isEmpty())
        out << infoRow(u"game-profile"_s, u"MicroOS profile"_s, profile.value("on"_L1).toBool() ? u"on"_s : u"off"_s);
    const QString yielded = m_game.value(u"yielded"_s).toString();
    if (ran && !yielded.isEmpty())
        out << infoRow(u"game-yield"_s, u"profile gives way to Steam"_s, QString(yielded).replace(u',', u", "_s));
    const QString lsfg = m_game.value(u"lsfg"_s).toString();
    if (m_game.value(u"gone"_s).toBool() || lsfg.isEmpty() || !lsfgShown())
        return out;
    const bool held = m_game.value(u"lsfgHeld"_s).toBool();
    const QMap<QString, QString> words{{u"off"_s, u"off"_s},
                                       {u"no-dll"_s, u"Lossless.dll not found"_s},
                                       {u"disabled-by-env"_s, u"off by launch option"_s},
                                       {u"check-failed"_s, u"check failed"_s}};
    QString text = words.value(lsfg, lsfg);
    if ((lsfg == "on"_L1 || lsfg == "cap-overridden"_L1) && held)
        text = lsfg == "on"_L1 ? u"on"_s : u"on, cap by launch option"_s;
    else if (lsfg == "on"_L1 || lsfg == "cap-overridden"_L1)
        text = m_game.value(u"lsfgYoung"_s).toBool() ? u"not running yet"_s : u"failed to start"_s;
    out << infoRow(u"game-lsfg"_s, u"Frame generation"_s, text);
    bool limitKnown = false;
    bool refreshKnown = false;
    const int limit = m_game.value(u"fpsLimit"_s).toInt(&limitKnown);
    const int refresh = m_game.value(u"refreshHz"_s).toInt(&refreshKnown);
    if (held && limitKnown && refreshKnown && 0 < limit && limit < refresh)
        out << infoRow(u"game-lsfg-limit"_s, u"Steam frame limit"_s, u"on: counts generated frames"_s);
    return out;
}

QVariantList SettingsModel::thisApp() const
{
    const auto state = m_entries.constFind(AndroidStateKey);
    const auto fps = m_entries.constFind(AndroidFpsKey);
    if (m_androidApp.isEmpty() || m_lsfg.session() != "android"_L1 || state == m_entries.cend() || fps == m_entries.cend())
        return {};
    QVariantMap generation = rowOf(*state);
    generation.insert(u"title"_s, u"Frame generation"_s);
    QVariantMap limit = rowOf(*fps);
    limit.insert(u"title"_s, u"Frame limit"_s);
    QVariantList out{label(u"this app"_s), infoRow(u"android-app"_s, m_androidApp, QString()), generation};
    if (!lsfgOn())
        out << infoRow(u"lsfg-off"_s, LsfgOffText, QString());
    return out << limit << infoRow(u"android-next"_s, u"Frame generation applies at next start"_s, QString());
}

QVariantList SettingsModel::androidPerf() const
{
    const auto overlay = m_entries.constFind(PerfOverlayKey);
    if (m_lsfg.session() != "android"_L1 || overlay == m_entries.cend())
        return {};
    return {label(u"android"_s), rowOf(*overlay)};
}

bool SettingsModel::profilesOn() const
{
    const auto shape = m_entries.constFind(ProfilesKey);
    const QVariant fallback = shape == m_entries.cend() ? QVariant(u"on"_s) : shape->defaultValue;
    return m_values.value(ProfilesKey, fallback).toString() == "on"_L1;
}

bool SettingsModel::lsfgOn() const
{
    const auto enabled = m_entries.constFind(LsfgEnabledKey);
    return enabled == m_entries.cend() || enabled->shown.toBool();
}

bool SettingsModel::lsfgShown() const
{
    const QJsonObject state = m_lsfg.state();
    const QString install = state.value("install"_L1).toString();
    if (state.value("owned"_L1).toString() == "yes"_L1 || (!install.isEmpty() && install != "none"_L1))
        return true;
    for (auto it = m_lsfgDefaults.cbegin(); it != m_lsfgDefaults.cend(); ++it)
        if (m_values.contains(it.key()) && m_values.value(it.key()) != it.value())
            return true;
    return !lsfgApps().isEmpty();
}

bool SettingsModel::installShown() const
{
    const QJsonObject state = m_lsfg.state();
    return state.value("owned"_L1).toString() == "yes"_L1 && state.value("install"_L1).toString() == "none"_L1
        && m_lsfg.session() == "steam"_L1;
}

QStringList SettingsModel::lsfgApps() const
{
    QStringList out;
    for (const QString &appid : heldApps(u"lsfg."_s))
        if (!SteamLibrary::excluded().contains(appid))
            out << appid;
    std::sort(out.begin(), out.end(), [](const QString &a, const QString &b) { return a.toULongLong() < b.toULongLong(); });
    return out;
}

QVariantList SettingsModel::lsfgRows(const QVariantList &rows) const
{
    QVariantList out;
    for (const QVariant &row : rows) {
        const QString key = row.toMap().value(u"key"_s).toString();
        if (key == LsfgEnabledKey) {
            out << infoRow(u"lsfg-status"_s, u"Lossless Scaling"_s, m_lsfg.status());
            if (installShown()) {
                QVariantMap install = infoRow(LsfgInstallKey, u"Install Lossless Scaling"_s, QString());
                install.insert({{u"kind"_s, u"row"_s}, {u"action"_s, LsfgInstallKey}, {u"ask"_s, u"lossless scaling"_s},
                                {u"accept"_s, u"install"_s}});
                out << install;
            }
        }
        out << row;
        if (key == LsfgModeKey)
            out << lsfgGames();
    }
    out << infoRow(u"lsfg-limit"_s, u"Steam frame limit"_s, u"set it off for these games"_s);
    return out;
}

QVariantList SettingsModel::lsfgGames() const
{
    const bool on = lsfgOn();
    int count = 0;
    for (const QJsonValue &entries : m_resolved)
        count += entries.toObject().value(LsfgStateOption).toObject().value("value"_L1).toString() == "on"_L1 ? 1 : 0;
    const QString games = m_resolvedAny ? u"%1 on"_s.arg(count) : QString();
    QVariantMap row = infoRow(LsfgGamesKey, u"Games"_s, on ? games : u"off"_s);
    row.insert(u"kind"_s, u"row"_s);
    if (on)
        row.insert(u"expand"_s, true);
    QVariantList out{row};
    if (!on)
        out << infoRow(u"lsfg-off"_s, LsfgOffText, QString());
    return out;
}

QVariantList SettingsModel::subRows(const QString &key, int) const
{
    if (key != LsfgGamesKey || !lsfgOn())
        return {};
    QVariantList out{infoRow(u"lsfg-proton"_s, u"Proton games only"_s, QString())};
    const QSet<QString> shown = listed(LsfgGroup);
    const auto sub = [](const QString &appid, const QString &title, const QString &value, bool present) {
        QVariantMap item = infoRow(LsfgGamePrefix + appid, title, value);
        item.insert({{u"kind"_s, u"sub"_s}, {u"installed"_s, present}});
        return item;
    };
    for (const SteamLibrary::Game &game : shownGames(shown)) {
        const QJsonObject state = m_resolved.value(game.appid).toObject().value(LsfgStateOption).toObject();
        if (!state.isEmpty() && !m_lsfgJoining.contains(game.appid))
            out << sub(game.appid, game.name, state.value("value"_L1).toString(), true);
    }
    for (const QString &appid : missingApps(shown))
        out << sub(appid, m_names.value(appid, appid), u"not installed"_s, false);
    if (!pickableNow(LsfgGroup).isEmpty())
        out << addRow(LsfgGroup);
    return out;
}

void SettingsModel::flipLsfg(const QString &appid)
{
    const QString key = AppPrefix + appid + u'.' + LsfgStateOption;
    const QJsonObject state = m_resolved.value(appid).toObject().value(LsfgStateOption).toObject();
    const bool present = installed(appid);
    if (!AppPage.match(AppPrefix + appid).hasMatch() || !m_templates.contains(LsfgStateOption)
        || (present && state.isEmpty()) || busy(key) || !lsfgOn())
        return;
    if (!m_apps.contains(appid)) {
        m_apps.insert(appid);
        instantiate(m_entries, appid);
    }
    if (!present) {
        QStringList keys;
        for (auto it = m_templates.cbegin(); it != m_templates.cend(); ++it)
            if (it.key().startsWith("lsfg."_L1))
                keys << AppPrefix + appid + u'.' + it.key();
        if (std::none_of(keys.cbegin(), keys.cend(), [this](const QString &item) { return busy(item); }))
            for (const QString &item : std::as_const(keys))
                write(item, u"auto"_s);
        return;
    }
    const bool own = state.value("source"_L1).toString() == "set"_L1;
    const QJsonObject given = own ? state.value("auto"_L1).toObject() : state;
    const bool fallback = given.value("value"_L1).toString() == "on"_L1;
    const bool wanted = state.value("value"_L1).toString() != "on"_L1;
    write(key, wanted == fallback ? u"auto"_s : wanted ? u"on"_s : u"off"_s);
}

void SettingsModel::addLsfg(const QString &appid)
{
    if (!AppPage.match(AppPrefix + appid).hasMatch() || !m_templates.contains(LsfgStateOption)
        || listed(LsfgGroup).contains(appid))
        return;
    const QString name = pageTitle(AppPrefix + appid, 0);
    if (!lsfgOn()) {
        emit refused(name, LsfgOffText);
        return;
    }
    if (m_resolveFailed) {
        emit refused(name, NotAvailable);
        return;
    }
    if (!m_lsfgAdds.contains(appid))
        m_lsfgAdds << appid;
    applyAdds();
}

void SettingsModel::applyAdds()
{
    const QStringList adds = m_lsfgAdds;
    for (const QString &appid : adds) {
        const QJsonObject state = m_resolved.value(appid).toObject().value(LsfgStateOption).toObject();
        if (state.isEmpty())
            continue;
        m_lsfgAdds.removeAll(appid);
        if (listed(LsfgGroup).contains(appid) || !lsfgOn())
            continue;
        if (!m_apps.contains(appid)) {
            m_apps.insert(appid);
            instantiate(m_entries, appid);
        }
        const bool off = state.value("value"_L1).toString() == "off"_L1;
        write(AppPrefix + appid + u'.' + LsfgStateOption, off ? u"on"_s : u"off"_s);
    }
}

void SettingsModel::act(const QString &key)
{
    if (key == LsfgInstallKey && installShown())
        m_lsfg.install();
}

void SettingsModel::toggle(const QString &key)
{
    if (key.startsWith(LsfgGamePrefix))
        flipLsfg(key.mid(LsfgGamePrefix.size()));
}

void SettingsModel::fetchEffective(const QString &appid)
{
    if (m_fixture) {
        m_effective.insert(appid, m_effectiveFixture.value(appid));
        return;
    }
    if (!m_client)
        return;
    ++m_effectivePending;
    m_effectiveWaiting.insert(appid);
    call(u"Effective"_s, {appid}, [this, appid](const QDBusMessage &reply) {
        --m_effectivePending;
        m_effectiveWaiting.remove(appid);
        const QJsonDocument document = QJsonDocument::fromJson(reply.arguments().value(0).toString().toUtf8());
        if (errorOf(reply).isEmpty() && document.isObject() && m_effective.value(appid) != document.object()) {
            m_effective.insert(appid, document.object());
            m_effectiveMoved = true;
        } else if (!m_effective.contains(appid)) {
            m_effective.insert(appid, {});
            m_effectiveMoved = true;
        }
        if (m_effectivePending > 0 || !m_effectiveMoved)
            return;
        m_effectiveMoved = false;
        bump();
    });
}

void SettingsModel::refreshEffective(const QString &key)
{
    const QString appid = AppKey.match(key).captured(1);
    if (!appid.isEmpty() && m_effective.contains(appid)) {
        fetchEffective(appid);
        return;
    }
    if (key.startsWith("fex."_L1) || key.startsWith("wine."_L1) || key.startsWith("proton."_L1)
        || key.startsWith("lsfg."_L1)) {
        for (const QString &cached : m_effective.keys())
            fetchEffective(cached);
    }
}

QString SettingsModel::pageTitle(const QString &group, int) const
{
    const QString appid = AppPage.match(group).captured(1);
    if (appid.isEmpty())
        return group;
    for (const QList<SteamLibrary::Game> *games : {&m_library, &m_libraryDropped})
        for (const SteamLibrary::Game &game : *games)
            if (game.appid == appid)
                return game.name;
    const QString name = m_names.value(appid);
    return name.isEmpty() ? appid : name;
}

QVariantList SettingsModel::rows(const QString &group, int) const
{
    const QString page = AppPage.match(group).captured(1);
    if (!page.isEmpty() && !m_effective.contains(page))
        return {};
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
    if (group == titleOf(LsfgGroup) && !out.isEmpty())
        out = lsfgRows(out);
    const QVariantList games = group == GamesGroup ? gameRows() : QVariantList();
    if (!games.isEmpty())
        out << label(GamesGroup) << games;
    const QString appid = AppPage.match(group).captured(1);
    if (appid.isEmpty() || out.isEmpty())
        return out;
    out << infoRow(u"game-next"_s, u"Applies at next launch"_s, QString());
    if (appid == m_game.value(u"appid"_s).toString() && m_game.value(u"steamFex"_s).toBool()) {
        const QString tso = m_templates.value(u"fex.tso"_s).value("label"_L1).toString();
        const QString multiblock = m_templates.value(u"fex.multiblock"_s).value("label"_L1).toString();
        out << infoRow(u"game-steam"_s, u"Set in Steam"_s, tso + u", "_s + multiblock);
    }
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
        if (!entry.appid.isEmpty())
            continue;
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
    QVariantList out = thisApp() + androidPerf() + thisGame();
    QString section;
    for (const Item &item : std::as_const(items)) {
        if (out.isEmpty() || item.owner != section) {
            section = item.owner;
            out << label(section);
        }
        out << QVariantMap{{u"kind"_s, u"row"_s}, {u"key"_s, u"group-"_s + item.title}, {u"title"_s, item.title},
                           {u"value"_s, QString()}, {u"readOnly"_s, true}, {u"group"_s, item.title},
                           {u"widget"_s, QString()}, {u"swatch"_s, QString()}, {u"hue"_s, -1}, {u"confirm"_s, false},
                           {u"pick"_s, QString()}, {u"bounded"_s, false}};
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
    endPreviews();
    const QVariant shown = shownOf(*it);
    m_preview = Preview{key, shown, {}, it->overridable, it->confirm || key == AccentKey, {shown}};
    return true;
}

bool SettingsModel::busy(const QString &key) const
{
    const auto it = m_entries.constFind(key);
    return it != m_entries.cend() && it->inflight > 0;
}

QVariantMap SettingsModel::sheet(const QString &key) const
{
    const auto it = m_entries.constFind(key);
    const QString pick = it == m_entries.cend() ? QString() : pickOf(*it);
    const bool draft = it != m_entries.cend() && (it->confirm || key == AccentKey);
    if (pick == "swatch"_L1) {
        const bool off = key == AccentKey && m_accentOffKnown;
        QVariantList values;
        QStringList words;
        QStringList swatches;
        if (off)
            values << -1;
        const auto saturation = m_entries.constFind(LedSaturationKey);
        const int percent = saturation == m_entries.cend() ? 100 : shownOf(*saturation).toInt();
        for (int hue = 0; hue < 360; hue += 10) {
            values << hue;
            if (key == LedHueKey)
                swatches << QColor::fromRgb(MicroSet::hueColor(hue, percent)).name();
        }
        for (qsizetype i = 0; i < values.size(); ++i)
            words << QString();
        const int focus = off && accentOff() ? 0 : normalizeHue(shownOf(*it).toInt()) / 10 + (off ? 1 : 0);
        return {{u"values"_s, values}, {u"words"_s, words}, {u"leads"_s, words}, {u"swatches"_s, swatches},
                {u"focus"_s, focus}, {u"mark"_s, -1}, {u"wrap"_s, true}, {u"draft"_s, draft}};
    }
    if (pick != "sheet"_L1)
        return {};
    const QVariantList values = sheetValues(*it);
    QStringList words;
    QStringList leads;
    for (const QVariant &value : values) {
        words << textOf(*it, value);
        leads << leadOf(*it, value);
    }
    const QVariant shown = shownOf(*it);
    const qsizetype mark = values.indexOf(shown);
    const qsizetype focus = mark >= 0 ? mark : isNumeric(it->type) ? nearest(values, shown) : 0;
    return {{u"values"_s, values}, {u"words"_s, words}, {u"leads"_s, leads}, {u"focus"_s, int(focus)}, {u"mark"_s, int(mark)},
            {u"wrap"_s, !it->bounded}, {u"draft"_s, draft}};
}

bool SettingsModel::preview(const QVariant &value)
{
    if (!m_preview)
        return false;
    const auto it = m_entries.constFind(m_preview->key);
    const QVariant wanted = it == m_entries.cend() ? QVariant() : coerce(it->type, value);
    if (!wanted.isValid())
        return false;
    if (m_preview->draft)
        return true;
    m_preview->ours << wanted;
    if (m_preview->override)
        return overrideTo(m_preview->key, wanted);
    return set(m_preview->key, wanted);
}

void SettingsModel::endPreview(const QVariant &value)
{
    if (!m_preview)
        return;
    const auto it = m_entries.constFind(m_preview->key);
    if (it == m_entries.cend() || !editable(*it)) {
        cancelPreview();
        return;
    }
    const Preview session = *m_preview;
    m_preview.reset();
    write(session.key, value);
    if (session.override)
        clearOverride(session.key);
    closePreview(session.key);
}

void SettingsModel::cancelPreview()
{
    if (!m_preview)
        return;
    const Preview session = *m_preview;
    m_preview.reset();
    if (session.override)
        clearOverride(session.key);
    else if (!session.draft)
        write(session.key, session.start);
    closePreview(session.key);
}

void SettingsModel::endPreviews()
{
    if (!m_preview)
        return;
    const Preview session = *m_preview;
    m_preview.reset();
    if (session.override) {
        write(session.key, session.last.isValid() ? session.last : session.start);
        clearOverride(session.key);
    }
    closePreview(session.key);
}

bool SettingsModel::overrideTo(const QString &key, const QVariant &value)
{
    const auto it = m_entries.constFind(key);
    if (it == m_entries.cend() || !editable(*it))
        return false;
    if (!m_fixture && !m_available) {
        emit refused(it->title, NotAvailable);
        return false;
    }
    m_preview->last = value;
    bump();
    if (!m_client)
        return true;
    m_client->override(key, value, [this, key, value](const MicroSet::Error &error) {
        const auto it = m_entries.constFind(key);
        if (error.ok() || it == m_entries.cend())
            return;
        emit refused(it->title, toastText(error.name));
        if (m_preview && m_preview->key == key && m_preview->last == value) {
            m_preview->last = QVariant();
            bump();
        }
    });
    return true;
}

void SettingsModel::clearOverride(const QString &key)
{
    if (m_client)
        m_client->clearOverride(key);
}

void SettingsModel::closePreview(const QString &key)
{
    m_preview.reset();
    bump();
    emit previewEnded(key);
}

void SettingsModel::dropPreview()
{
    if (!m_preview)
        return;
    const QString key = m_preview->key;
    if (m_preview->override)
        clearOverride(key);
    closePreview(key);
}

void SettingsModel::watchPreview(const Entry &entry)
{
    if (m_preview && m_preview->key == entry.key && !m_preview->ours.contains(entry.shown))
        dropPreview();
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
    const auto floored = [&](double value) {
        if (!entry.floor || value >= *entry.floor)
            return coerce(entry.type, value);
        return coerce(entry.type, delta > 0 ? *entry.floor : std::min(base.toDouble(), *entry.floor));
    };
    if (!entry.choices.isEmpty()) {
        qsizetype index = entry.choices.indexOf(base);
        if (index < 0)
            index = nearest(entry.choices, base);
        return entry.choices.at(std::clamp<qsizetype>(index + delta, 0, entry.choices.size() - 1));
    }
    if (!entry.steps.isEmpty()) {
        qsizetype index = entry.steps.indexOf(base);
        if (index < 0 && delta != 0) {
            const qsizetype below = std::count_if(entry.steps.cbegin(), entry.steps.cend(),
                                                  [&](const QVariant &item) { return item.toDouble() < base.toDouble(); });
            if ((delta > 0 && below == entry.steps.size()) || (delta < 0 && below == 0))
                return base;
            index = delta > 0 ? below - 1 : below;
        } else if (index < 0) {
            index = nearest(entry.steps, base);
        }
        return floored(entry.steps.at(std::clamp<qsizetype>(index + delta, 0, entry.steps.size() - 1)).toDouble());
    }
    if (!isNumeric(entry.type) || !entry.min || !entry.max)
        return {};
    const double target = base.toDouble() + delta * entry.step;
    double value = target >= *entry.max ? *entry.max
        : target <= *entry.min           ? *entry.min
                                         : *entry.min + std::round((target - *entry.min) / entry.step) * entry.step;
    value = std::clamp(value, *entry.min, *entry.max);
    return floored(value);
}

QString SettingsModel::text(const QString &key, const QVariant &value) const
{
    const auto it = m_entries.constFind(key);
    if (it == m_entries.cend())
        return {};
    const QVariant typed = coerce(it->type, value);
    return rowText(*it, typed.isValid() ? typed : it->shown);
}

bool SettingsModel::set(const QString &key, const QVariant &value)
{
    const auto it = m_entries.constFind(key);
    return it != m_entries.cend() && editable(*it) && write(key, value);
}

void SettingsModel::pickAccent(int hue)
{
    if (m_preview && m_preview->key == AccentKey)
        cancelPreview();
    if (!m_fixture && !m_available) {
        const auto accent = m_entries.constFind(AccentKey);
        emit refused(accent == m_entries.cend() ? AccentKey : accent->title, NotAvailable);
        return;
    }
    if (hue < 0) {
        writeHidden(AccentOffKey, true);
        return;
    }
    if (accentOff())
        writeHidden(AccentOffKey, false);
    set(AccentKey, hue);
}

void SettingsModel::writeHidden(const QString &key, const QVariant &value)
{
    const QVariant old = m_values.value(key);
    m_values.insert(key, value);
    bump();
    if (!m_client)
        return;
    m_client->set(key, value, [this, key, old](const MicroSet::Error &error) {
        if (error.ok())
            return;
        m_values.insert(key, old);
        bump();
        const auto accent = m_entries.constFind(AccentKey);
        emit refused(accent == m_entries.cend() ? key : accent->title, toastText(error.name));
    });
}

bool SettingsModel::write(const QString &key, const QVariant &value)
{
    const auto it = m_entries.find(key);
    if (it == m_entries.end() || !it->writable)
        return false;
    const QVariant wanted = coerce(it->type, value);
    if (!wanted.isValid())
        return false;
    if (!m_fixture && !m_available) {
        emit refused(it->title, NotAvailable);
        return false;
    }
    if (key == AndroidStateKey || key == AndroidFpsKey) {
        const auto app = m_entries.constFind(AndroidAppKey);
        if (m_androidApp.isEmpty() || app == m_entries.cend())
            return false;
        if (m_androidApp.size() > AndroidAppMax) {
            emit refused(it->title, SetFailed);
            return false;
        }
        if (app->shown.toString() != m_androidApp) {
            if (!write(AndroidAppKey, m_androidApp))
                return false;
            const auto other = m_entries.constFind(key == AndroidStateKey ? AndroidFpsKey : AndroidStateKey);
            if (other != m_entries.cend())
                write(other->key, other->defaultValue);
        }
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
    if (!error.isEmpty() && error != HoldPending)
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
    watchPreview(*it);
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
    for (const QString &key : liveKeys(group))
        hold(key);
    const QString appid = AppPage.match(group).captured(1);
    if (!appid.isEmpty() && !m_templates.isEmpty()) {
        if (!m_apps.contains(appid)) {
            m_apps.insert(appid);
            instantiate(m_entries, appid);
        }
        if (!m_names.contains(appid)) {
            const QString name = SteamLibrary(SteamLibrary::defaultRoot()).name(appid);
            if (!name.isEmpty())
                m_names.insert(appid, name);
        }
        fetchEffective(appid);
        bump();
        return;
    }
    const QString list = listOf(group);
    if (list.isEmpty() || m_templates.isEmpty())
        return;
    readLibrary();
    if (list == LsfgGroup) {
        sendResolve();
    } else {
        for (const SteamLibrary::Game &game : shownGames(listed(GamesGroup)))
            fetchEffective(game.appid);
    }
    bump();
}

bool SettingsModel::readLibrary()
{
    const SteamLibrary library(SteamLibrary::defaultRoot());
    const QByteArray stamp = library.stamp();
    if (m_libraryRead && stamp == m_libraryStamp)
        return false;
    m_library = library.games(&m_libraryDropped);
    m_libraryStamp = stamp;
    m_libraryRead = true;
    m_present.clear();
    for (const QList<SteamLibrary::Game> *games : {&m_library, &m_libraryDropped})
        for (const SteamLibrary::Game &game : *games)
            m_present.insert(game.appid);
    keep();
    return true;
}

void SettingsModel::leave(const QString &group)
{
    for (const QString &key : liveKeys(group))
        drop(key);
}

void SettingsModel::beginVisit(const QString &group)
{
    const QString list = listOf(group);
    if (!list.isEmpty() && m_visits[list]++ == 0)
        m_kept.insert(list, listedNow(list));
}

void SettingsModel::endVisit(const QString &group)
{
    const auto it = m_visits.find(listOf(group));
    if (it == m_visits.end() || --*it > 0)
        return;
    const QString list = it.key();
    m_visits.erase(it);
    m_kept.remove(list);
    if (list == LsfgGroup) {
        m_lsfgAdds.clear();
        m_lsfgJoining.clear();
        m_resolveQueued = false;
    }
    bump();
}

QVariantList SettingsModel::pickable(const QString &caller)
{
    if ((caller != GamesGroup && caller != LsfgGroup) || m_templates.isEmpty())
        return {};
    if (readLibrary()) {
        joinGames();
        if (m_visits.value(LsfgGroup) > 0)
            sendResolve();
        bump();
    }
    return pickableNow(caller);
}

void SettingsModel::keep()
{
    for (auto it = m_kept.begin(); it != m_kept.end(); ++it)
        it->unite(listedNow(it.key()));
}

void SettingsModel::joinGames()
{
    if (m_visits.value(GamesGroup) == 0 || !m_libraryRead || m_templates.isEmpty())
        return;
    for (const SteamLibrary::Game &game : shownGames(listed(GamesGroup)))
        if (!m_effective.contains(game.appid) && !m_effectiveWaiting.contains(game.appid))
            fetchEffective(game.appid);
}

void SettingsModel::joinApp(const QString &appid, const QString &option, const QVariant &value)
{
    const auto shape = m_templates.constFind(option);
    if (shape == m_templates.cend() || value == shape->value("default"_L1).toVariant()
        || SteamLibrary::excluded().contains(appid))
        return;
    const auto games = m_kept.find(GamesGroup);
    if (games != m_kept.end())
        games->insert(appid);
    const auto lsfg = m_kept.find(LsfgGroup);
    if (lsfg != m_kept.end() && option.startsWith("lsfg."_L1) && !lsfg->contains(appid)) {
        lsfg->insert(appid);
        m_lsfgJoining.insert(appid, m_resolveSerial + 1);
    }
    if (m_visits.value(GamesGroup) > 0 && installed(appid) && !m_effective.contains(appid)
        && !m_effectiveWaiting.contains(appid))
        fetchEffective(appid);
}

void SettingsModel::queueResolve()
{
    if (m_visits.value(LsfgGroup) == 0 || m_resolveQueued)
        return;
    m_resolveQueued = true;
    QMetaObject::invokeMethod(
        this,
        [this] {
            if (m_resolveQueued)
                sendResolve();
        },
        Qt::QueuedConnection);
}

void SettingsModel::sendResolve()
{
    m_resolveQueued = false;
    QStringList appids;
    for (const SteamLibrary::Game &game : std::as_const(m_library))
        appids << game.appid;
    const QSet<QString> shown = listed(LsfgGroup);
    for (const SteamLibrary::Game &game : std::as_const(m_libraryDropped))
        if (shown.contains(game.appid))
            appids << game.appid;
    const int serial = ++m_resolveSerial;
    if (m_fixture) {
        QJsonObject out;
        for (const QString &appid : std::as_const(appids)) {
            const auto it = m_effectiveFixture.constFind(appid);
            if (it == m_effectiveFixture.cend())
                continue;
            const QJsonValue state = it->value("options"_L1).toObject().value(LsfgStateOption);
            if (state.isObject())
                out.insert(appid, QJsonObject{{LsfgStateOption, state}});
        }
        takeResolved(out, serial);
        return;
    }
    if (!m_client)
        return;
    call(u"Resolve"_s, {appids, QStringList{LsfgStateOption}}, [this, serial](const QDBusMessage &reply) {
        if (serial < m_resolveSeen)
            return;
        m_resolveSeen = serial;
        const QJsonDocument document = QJsonDocument::fromJson(reply.arguments().value(0).toString().toUtf8());
        if (!errorOf(reply).isEmpty() || !document.isObject()) {
            m_resolveFailed = true;
            return;
        }
        takeResolved(document.object(), serial);
    });
}

void SettingsModel::takeResolved(const QJsonObject &resolved, int serial)
{
    m_resolved = resolved;
    m_resolvedAny = true;
    m_resolveFailed = false;
    m_lsfgJoining.removeIf([serial](const auto &joining) { return joining.value() <= serial; });
    bump();
    applyAdds();
}

QVariant SettingsModel::value(const QString &key, int) const
{
    return m_values.value(key);
}

void SettingsModel::hold(const QString &key)
{
    if (!m_client)
        return;
    if (m_watching[key]++ == 0)
        m_client->subscribe(key);
}

void SettingsModel::drop(const QString &key)
{
    if (!m_client)
        return;
    const auto it = m_watching.find(key);
    if (it == m_watching.end())
        return;
    if (--*it == 0) {
        m_watching.erase(it);
        m_client->unsubscribe(key);
    }
}
