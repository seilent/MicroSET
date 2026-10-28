#include "schema.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace Qt::StringLiterals;

namespace {

const QString PrivilegedFile = u"10-microset.json"_s;
const QString PrivilegedOwner = u"microset"_s;
const QStringList Reserved = {u"microset"_s, u"power"_s, u"meta"_s, u"session"_s, u"audio"_s, u"display"_s, u"rgb"_s,
                               u"system"_s, u"app"_s, u"fex"_s, u"wine"_s, u"proton"_s, u"lsfg"_s};
const QStringList Types = {u"s"_s, u"b"_s, u"u"_s, u"i"_s, u"d"_s, u"as"_s};
const QStringList Validators = {u"eq_filters"_s};
const QStringList Widgets = {u"none"_s, u"hue"_s, u"edit"_s};
const QStringList UiFields = {u"order"_s, u"widget"_s, u"step"_s, u"steps"_s, u"values"_s, u"unit"_s, u"scale"_s, u"apply"_s,
                               u"bounded"_s, u"lock"_s, u"leads"_s};
const QStringList Applies = {u"live"_s, u"confirm"_s};
const QStringList LockFields = {u"key"_s, u"is"_s, u"text"_s, u"show"_s};
const QRegularExpression KeyPattern(u"^[a-z][a-z0-9]*(\\.[A-Za-z0-9_]+)+$"_s);
const QRegularExpression TemplatePattern(u"^app\\.[a-z][a-z0-9_]*(\\.[a-z0-9_]+)*$"_s);
const QRegularExpression AppPattern(u"^app\\.([1-9][0-9]{0,9})(\\..*)?$"_s);
const QRegularExpression AppIdPattern(u"^[1-9][0-9]{0,9}$"_s);
const QString AppPrefix = u"app."_s;
constexpr int MaxSteps = 64;
constexpr int MaxUnit = 16;
const QStringList GroupFields = {u"label"_s, u"order"_s};
const QStringList FilterTypes = {u"peak"_s,     u"lowshelf"_s, u"highshelf"_s, u"bandpass"_s,
                                 u"lowpass"_s,  u"highpass"_s, u"allpass"_s,   u"matchedpeak"_s};
constexpr int MaxBands = 10;
constexpr int MaxItems = 1024;
constexpr int MaxNumberLength = 16;

bool isNumeric(const QString &type)
{
    return type == "u"_L1 || type == "i"_L1 || type == "d"_L1;
}

bool isOrder(const QJsonValue &value)
{
    const double number = value.toDouble(-1);
    return value.isDouble() && number >= 0 && number <= std::numeric_limits<int>::max() && number == std::floor(number);
}

bool onlyFields(const QJsonObject &object, const QStringList &fields)
{
    const QStringList keys = object.keys();
    return std::all_of(keys.cbegin(), keys.cend(), [&](const QString &field) { return fields.contains(field); });
}

bool fitsKey(const Key &key, const QJsonValue &value)
{
    if (!value.isDouble() || !std::isfinite(value.toDouble()))
        return false;
    const double number = value.toDouble();
    if (key.type != "d"_L1 && number != std::floor(number))
        return false;
    if (key.type == "u"_L1 && number < 0)
        return false;
    return (!key.min || number >= *key.min) && (!key.max || number <= *key.max);
}

bool validLock(const QJsonValue &value)
{
    const QJsonObject lock = value.toObject();
    const QJsonValue is = lock.value("is"_L1);
    const bool text = lock.value("text"_L1).isString() && !lock.value("text"_L1).toString().isEmpty()
        && lock.value("text"_L1).toString().size() <= 256;
    const bool show = lock.value("show"_L1).isString() && KeyPattern.match(lock.value("show"_L1).toString()).hasMatch();
    return value.isObject() && onlyFields(lock, LockFields) && lock.value("key"_L1).isString()
        && KeyPattern.match(lock.value("key"_L1).toString()).hasMatch() && (is.isBool() || is.isString() || is.isDouble())
        && int(lock.contains("text"_L1)) + int(lock.contains("show"_L1)) == 1 && (text || show);
}

std::optional<QJsonObject> uiHint(const Key &key, const QJsonValue &value, QString *why, QStringList *notes)
{
    const QJsonObject ui = value.toObject();
    if (!value.isObject() || !onlyFields(ui, UiFields)) {
        *why = u"not an object of order, widget, step, steps, values, unit, scale, apply, bounded, lock and leads"_s;
        return {};
    }
    if (ui.contains("order"_L1) && !isOrder(ui.value("order"_L1))) {
        *why = u"bad order"_s;
        return {};
    }
    const QString widget = ui.value("widget"_L1).toString();
    const bool range = isNumeric(key.type) && key.enumValues.isEmpty() && key.min && key.max;
    if (ui.contains("widget"_L1)
        && (!Widgets.contains(widget) || (widget == "hue"_L1 && key.type != "u"_L1) || (widget == "edit"_L1 && !range))) {
        *why = u"bad widget"_s;
        return {};
    }
    const QJsonValue step = ui.value("step"_L1);
    if (ui.contains("step"_L1)
        && (!isNumeric(key.type) || !key.enumValues.isEmpty() || !step.isDouble() || !(step.toDouble() > 0)
            || !std::isfinite(step.toDouble()) || (key.type != "d"_L1 && step.toDouble() != std::floor(step.toDouble())))) {
        *why = u"bad step"_s;
        return {};
    }
    const QJsonArray steps = ui.value("steps"_L1).toArray();
    if (ui.contains("steps"_L1)
        && (!isNumeric(key.type) || !key.enumValues.isEmpty() || ui.contains("step"_L1) || !ui.value("steps"_L1).isArray()
            || steps.isEmpty() || steps.size() > MaxSteps
            || std::any_of(steps.begin(), steps.end(), [&](const QJsonValue &item) { return !fitsKey(key, item); }))) {
        *why = u"bad steps"_s;
        return {};
    }
    for (const QString &field : {u"values"_s, u"leads"_s}) {
        const QJsonValue texts = ui.value(field);
        if (!ui.contains(field))
            continue;
        const QJsonObject labels = texts.toObject();
        if (!texts.isObject() || std::any_of(labels.begin(), labels.end(), [&](const QJsonValue &label) {
                return !label.isString() || label.toString().isEmpty() || label.toString().size() > key.maxLength;
            })) {
            *why = u"bad "_s + field;
            return {};
        }
        if (!key.enumValues.isEmpty()) {
            for (const QString &text : labels.keys())
                if (std::none_of(key.enumValues.cbegin(), key.enumValues.cend(),
                                 [&](const QVariant &item) { return Schema::text(item) == text; }))
                    *notes << u"%1 for %2 is not an enum value"_s.arg(field == "values"_L1 ? u"label"_s : u"lead"_s, text);
        }
    }
    const QJsonValue unit = ui.value("unit"_L1);
    if (ui.contains("unit"_L1) && (!unit.isString() || unit.toString().isEmpty() || unit.toString().size() > MaxUnit)) {
        *why = u"bad unit"_s;
        return {};
    }
    const QJsonValue scale = ui.value("scale"_L1);
    if (ui.contains("scale"_L1)
        && (!isNumeric(key.type) || !key.enumValues.isEmpty() || !scale.isDouble() || !std::isfinite(scale.toDouble())
            || !(scale.toDouble() > 0))) {
        *why = u"bad scale"_s;
        return {};
    }
    if (ui.contains("apply"_L1) && !Applies.contains(ui.value("apply"_L1).toString())) {
        *why = u"bad apply"_s;
        return {};
    }
    if (ui.contains("bounded"_L1) && !ui.value("bounded"_L1).isBool()) {
        *why = u"bad bounded"_s;
        return {};
    }
    if (ui.contains("lock"_L1) && !validLock(ui.value("lock"_L1))) {
        *why = u"bad lock"_s;
        return {};
    }
    return ui;
}

bool hasType(const QString &type, const QVariant &value)
{
    static const QMap<QString, int> ids = {
        {u"s"_s, QMetaType::QString}, {u"b"_s, QMetaType::Bool},   {u"u"_s, QMetaType::UInt},
        {u"i"_s, QMetaType::Int},     {u"d"_s, QMetaType::Double}, {u"as"_s, QMetaType::QStringList},
    };
    return ids.contains(type) && value.typeId() == ids.value(type);
}

bool inRange(const QString &text, double low, double high)
{
    static const QRegularExpression pattern(u"^-?[0-9]+(\\.[0-9]+)?$"_s);
    if (text.size() > MaxNumberLength || !pattern.match(text).hasMatch())
        return false;
    const double number = text.toDouble();
    return number >= low && number <= high;
}

bool eqFilters(const QStringList &items)
{
    if (items.isEmpty() || items.size() > MaxBands + 1)
        return false;
    const QStringList preamp = items.first().split(u' ');
    if (preamp.size() != 2 || preamp.at(0) != "preamp"_L1 || !inRange(preamp.at(1), -24, 0))
        return false;
    return std::all_of(items.cbegin() + 1, items.cend(), [](const QString &item) {
        const QStringList band = item.split(u' ');
        return band.size() == 4 && FilterTypes.contains(band.at(0)) && inRange(band.at(1), 20, 20000)
            && inRange(band.at(2), -24, 12) && inRange(band.at(3), 0.1, 10);
    });
}

}

bool Schema::matches(const QString &key, const QString &prefix)
{
    if (prefix.isEmpty())
        return true;
    if (prefix.endsWith(u'.'))
        return key.startsWith(prefix);
    return key == prefix || key.startsWith(prefix + u'.');
}

bool Schema::isAppId(const QString &appid)
{
    return AppIdPattern.match(appid).hasMatch();
}

bool Schema::splitApp(const QString &name, QString *appid, QString *option)
{
    const QRegularExpressionMatch match = AppPattern.match(name);
    if (!match.hasMatch() || match.captured(2).size() < 2)
        return false;
    if (appid)
        *appid = match.captured(1);
    if (option)
        *option = match.captured(2).mid(1);
    return true;
}

QVariant Schema::fromJson(const QString &type, const QJsonValue &json)
{
    if (type == "s"_L1)
        return json.isString() ? QVariant(json.toString()) : QVariant();
    if (type == "b"_L1)
        return json.isBool() ? QVariant(json.toBool()) : QVariant();
    if (type == "as"_L1) {
        if (!json.isArray())
            return {};
        QStringList list;
        for (const QJsonValue &item : json.toArray()) {
            if (!item.isString())
                return {};
            list << item.toString();
        }
        return list;
    }
    if (!json.isDouble())
        return {};
    const double number = json.toDouble();
    if (!std::isfinite(number))
        return {};
    if (type == "d"_L1)
        return number;
    if (number != std::floor(number))
        return {};
    if (type == "u"_L1 && number >= 0 && number <= std::numeric_limits<uint>::max())
        return uint(number);
    if (type == "i"_L1 && number >= std::numeric_limits<int>::min() && number <= std::numeric_limits<int>::max())
        return int(number);
    return {};
}

QJsonValue Schema::toJson(const QVariant &value)
{
    switch (value.typeId()) {
    case QMetaType::QStringList:
        return QJsonArray::fromStringList(value.toStringList());
    case QMetaType::UInt:
        return qint64(value.toUInt());
    default:
        return QJsonValue::fromVariant(value);
    }
}

QString Schema::text(const QVariant &value)
{
    if (value.typeId() == QMetaType::QStringList)
        return value.toStringList().join(u',');
    return value.toString();
}

void Schema::addApplier(const QString &name)
{
    m_appliers.insert(name);
}

void Schema::addProvider(const QString &name, Provider provider, Labels labels)
{
    m_providers.insert(name, std::move(provider));
    if (labels)
        m_labels.insert(name, std::move(labels));
}

void Schema::load(const QString &dir)
{
    QStringList files = QDir(dir).entryList({u"*.json"_s}, QDir::Files, QDir::Name);
    if (files.removeOne(PrivilegedFile))
        files.prepend(PrivilegedFile);
    for (const QString &file : files)
        loadFile(dir, file);
}

void Schema::loadFile(const QString &dir, const QString &file)
{
    static const QRegularExpression ownerPattern(u"^[a-z][a-z0-9]*$"_s);

    const QString path = dir + u'/' + file;
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly)) {
        qWarning("%s: %s, skipped", qPrintable(path), qPrintable(input.errorString()));
        return;
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(input.readAll(), &error);
    const QJsonObject root = document.object();
    const QString owner = root.value("owner"_L1).toString();
    if (error.error != QJsonParseError::NoError || !document.isObject() || !root.value("keys"_L1).isObject()
        || owner.isEmpty()) {
        const QString why = error.error != QJsonParseError::NoError ? error.errorString() : u"no owner or keys"_s;
        qWarning("%s: not a schema file (%s), skipped", qPrintable(path), qPrintable(why));
        return;
    }
    const bool privileged = file == PrivilegedFile && owner == PrivilegedOwner;
    if (!privileged
        && (!ownerPattern.match(owner).hasMatch() || Reserved.contains(owner))) {
        qWarning("%s: owner %s refused, skipped", qPrintable(path), qPrintable(owner));
        return;
    }
    const QJsonObject keys = root.value("keys"_L1).toObject();
    for (auto it = keys.begin(); it != keys.end(); ++it) {
        const QString name = it.key();
        if (!KeyPattern.match(name).hasMatch()) {
            qWarning("%s: %s is not a key name, refused", qPrintable(path), qPrintable(name));
            continue;
        }
        if (!privileged && !name.startsWith(owner + u'.')) {
            qWarning("%s: %s is outside %s.*, refused", qPrintable(path), qPrintable(name), qPrintable(owner));
            continue;
        }
        if (const Key *first = defined(name)) {
            qWarning("%s: %s already defined in %s, ignored", qPrintable(path), qPrintable(name), qPrintable(first->file));
            continue;
        }
        QString why = u"not an object"_s;
        std::optional<Key> parsed = it.value().isObject() ? parseKey(name, owner, it.value().toObject(), &why) : std::nullopt;
        if (!parsed) {
            qWarning("%s: %s: %s, refused", qPrintable(path), qPrintable(name), qPrintable(why));
            continue;
        }
        parsed->file = file;
        const QJsonValue ui = it.value().toObject().value("ui"_L1);
        if (!ui.isUndefined()) {
            QStringList notes;
            if (const std::optional<QJsonObject> hint = uiHint(*parsed, ui, &why, &notes))
                parsed->ui = *hint;
            else
                qWarning("%s: %s: %s, ui hint dropped", qPrintable(path), qPrintable(name), qPrintable(why));
            for (const QString &note : std::as_const(notes))
                qWarning("%s: %s: %s", qPrintable(path), qPrintable(name), qPrintable(note));
        }
        (parsed->perApp ? m_templates : m_keys).insert(name, *parsed);
    }
    parseGroups(path, file, root.value("groups"_L1));
    if (privileged)
        parsePresets(path, root.value("presets"_L1));
    else if (root.contains("presets"_L1))
        qWarning("%s: presets only in %s, ignored", qPrintable(path), qPrintable(PrivilegedFile));
}

void Schema::parsePresets(const QString &path, const QJsonValue &presets)
{
    static const QRegularExpression namePattern(u"^[a-z][a-z0-9_]*$"_s);
    if (presets.isUndefined())
        return;
    if (!presets.isObject()) {
        qWarning("%s: presets is not an object, ignored", qPrintable(path));
        return;
    }
    const QJsonObject entries = presets.toObject();
    for (auto it = entries.begin(); it != entries.end(); ++it) {
        QString why;
        Preset preset;
        if (!namePattern.match(it.key()).hasMatch() || !it.value().isObject())
            why = u"not a name and an object"_s;
        const QJsonObject options = it.value().toObject();
        for (auto option = options.begin(); why.isEmpty() && option != options.end(); ++option) {
            const Key *target = perApp(option.key());
            const QString value = option.value().toString();
            if (!option.key().startsWith("fex."_L1) || option.key() == "fex.preset"_L1 || !target
                || !option.value().isString() || !accepts(*target, value) || target->defaultValue == value)
                why = u"%1 is not a FEX option value"_s.arg(option.key());
            else
                preset.insert(option.key(), value);
        }
        if (!why.isEmpty()) {
            qWarning("%s: preset %s: %s, dropped", qPrintable(path), qPrintable(it.key()), qPrintable(why));
            continue;
        }
        m_presets.insert(it.key(), preset);
    }
}

void Schema::parseGroups(const QString &path, const QString &file, const QJsonValue &groups)
{
    if (groups.isUndefined())
        return;
    if (!groups.isObject()) {
        qWarning("%s: groups is not an object, ignored", qPrintable(path));
        return;
    }
    const QJsonObject entries = groups.toObject();
    for (auto it = entries.begin(); it != entries.end(); ++it) {
        const QString name = it.key();
        const QJsonObject entry = it.value().toObject();
        const bool used = std::any_of(m_keys.cbegin(), m_keys.cend(), [&](const Key &key) {
            return key.file == file && key.group == name;
        });
        if (!used) {
            qWarning("%s: group %s has no key in this file, dropped", qPrintable(path), qPrintable(name));
            continue;
        }
        if (const Group *first = group(name)) {
            qWarning("%s: group %s already described in %s, ignored", qPrintable(path), qPrintable(name),
                     qPrintable(first->file));
            continue;
        }
        const QJsonValue label = entry.value("label"_L1);
        if (!it.value().isObject() || !onlyFields(entry, GroupFields) || !label.isString() || label.toString().isEmpty()
            || label.toString().size() > 256 || !isOrder(entry.value("order"_L1))) {
            qWarning("%s: group %s: not an object of label and order, dropped", qPrintable(path), qPrintable(name));
            continue;
        }
        m_groups.insert(name, Group{label.toString(), entry.value("order"_L1).toInt(), file});
    }
}

std::optional<Key> Schema::parseKey(const QString &name, const QString &owner, const QJsonObject &object, QString *why) const
{
    Key key;
    key.name = name;
    key.owner = owner;
    key.type = object.value("type"_L1).toString();
    if (!Types.contains(key.type)) {
        *why = u"bad type"_s;
        return {};
    }
    const QString element = key.type == "as"_L1 ? u"s"_s : key.type;
    const bool hasEnum = object.contains("enum"_L1);
    const bool hasFrom = object.contains("enum_from"_L1);
    const bool hasRange = object.contains("min"_L1) || object.contains("max"_L1);
    if (int(hasEnum) + int(hasFrom) + int(hasRange) > 1) {
        *why = u"enum, enum_from and min/max exclude each other"_s;
        return {};
    }
    if (hasEnum) {
        const QJsonValue values = object.value("enum"_L1);
        if (!values.isArray() || values.toArray().isEmpty() || key.type == "b"_L1) {
            *why = u"bad enum"_s;
            return {};
        }
        for (const QJsonValue &value : values.toArray()) {
            const QVariant item = fromJson(element, value);
            if (!item.isValid()) {
                *why = u"enum value of the wrong type"_s;
                return {};
            }
            key.enumValues << item;
        }
    }
    if (hasFrom) {
        key.enumFrom = object.value("enum_from"_L1).toString();
        if (key.type != "s"_L1 || !m_providers.contains(key.enumFrom)) {
            *why = u"unknown enum_from"_s;
            return {};
        }
    }
    if (hasRange) {
        if (!isNumeric(key.type)) {
            *why = u"min/max on a non numeric key"_s;
            return {};
        }
        for (const auto &[field, bound] : {std::pair{"min"_L1, &key.min}, std::pair{"max"_L1, &key.max}}) {
            if (!object.contains(field))
                continue;
            if (!object.value(field).isDouble()) {
                *why = u"bad min/max"_s;
                return {};
            }
            *bound = object.value(field).toDouble();
        }
        if (key.min && key.max && *key.min > *key.max) {
            *why = u"min above max"_s;
            return {};
        }
    }
    const QJsonValue floor = object.value("floor"_L1);
    if (!floor.isUndefined()) {
        if (!hasRange || !fitsKey(key, floor)) {
            *why = u"bad floor"_s;
            return {};
        }
        key.floorValue = floor.toDouble();
    }
    const QJsonValue maxLength = object.value("max_len"_L1);
    if (!maxLength.isUndefined()) {
        if ((key.type != "s"_L1 && key.type != "as"_L1) || !maxLength.isDouble() || maxLength.toDouble() < 1
            || maxLength.toDouble() > 65536 || maxLength.toDouble() != std::floor(maxLength.toDouble())) {
            *why = u"bad max_len"_s;
            return {};
        }
        key.maxLength = maxLength.toInt();
    }
    const QJsonValue maxItems = object.value("max_items"_L1);
    if (!maxItems.isUndefined()) {
        if (key.type != "as"_L1 || !maxItems.isDouble() || maxItems.toDouble() < 1 || maxItems.toDouble() > MaxItems
            || maxItems.toDouble() != std::floor(maxItems.toDouble())) {
            *why = u"bad max_items"_s;
            return {};
        }
        key.maxItems = maxItems.toInt();
    }
    const QJsonValue validator = object.value("validator"_L1);
    if (!validator.isUndefined()) {
        if (key.type != "as"_L1 || !Validators.contains(validator.toString())) {
            *why = u"unknown validator"_s;
            return {};
        }
        key.validator = validator.toString();
    }
    const QJsonValue writable = object.value("writable"_L1);
    if (!writable.isUndefined() && !writable.isBool()) {
        *why = u"bad writable"_s;
        return {};
    }
    key.writable = writable.toBool(true);
    const QJsonValue applier = object.value("applier"_L1);
    if (!applier.isUndefined() && !applier.isNull()) {
        if (!applier.isString() || !m_appliers.contains(applier.toString())) {
            *why = u"unknown applier"_s;
            return {};
        }
        key.applier = applier.toString();
    }
    const QJsonValue overridable = object.value("override"_L1);
    if (!overridable.isUndefined() && !overridable.isBool()) {
        *why = u"bad override"_s;
        return {};
    }
    key.overridable = overridable.toBool(false);
    if (key.overridable && (!key.writable || key.applier == "power"_L1)) {
        *why = u"override on a read only or power key"_s;
        return {};
    }
    const QJsonValue store = object.value("store"_L1);
    if (!store.isUndefined()) {
        if (store.toString() != "coalesce"_L1 || !key.writable || key.applier == "power"_L1) {
            *why = u"bad store"_s;
            return {};
        }
        key.coalesce = true;
    }
    const QJsonValue perApp = object.value("per_app"_L1);
    if (!perApp.isUndefined() && !perApp.isBool()) {
        *why = u"bad per_app"_s;
        return {};
    }
    key.perApp = perApp.toBool(false);
    if (key.perApp != name.startsWith(AppPrefix) || (key.perApp && !TemplatePattern.match(name).hasMatch())) {
        *why = u"per_app keys are the app.* keys"_s;
        return {};
    }
    if (key.perApp
        && (key.type != "s"_L1 || !key.writable || key.overridable || key.coalesce || !key.applier.isEmpty())) {
        *why = u"per_app on a non string, read only, override, coalesced or applier key"_s;
        return {};
    }
    const QJsonValue group = object.value("group"_L1);
    const QJsonValue label = object.value("label"_L1);
    if (!(group.isUndefined() || group.isNull() || group.isString()) || !(label.isUndefined() || label.isString())) {
        *why = u"bad group or label"_s;
        return {};
    }
    key.group = group.toString();
    key.label = label.toString();
    key.defaultValue = fromJson(key.type, object.value("default"_L1));
    if (!key.defaultValue.isValid() || (key.enumFrom.isEmpty() && !accepts(key, key.defaultValue))) {
        *why = u"bad default"_s;
        return {};
    }
    return key;
}

const Key *Schema::key(const QString &name) const
{
    const auto it = m_keys.constFind(name);
    if (it != m_keys.cend())
        return &*it;
    QString option;
    return splitApp(name, nullptr, &option) ? perApp(option) : nullptr;
}

const Key *Schema::perApp(const QString &option) const
{
    const auto it = m_templates.constFind(AppPrefix + option);
    return it == m_templates.cend() ? nullptr : &*it;
}

const Key *Schema::defined(const QString &name) const
{
    const auto it = m_keys.constFind(name);
    if (it != m_keys.cend())
        return &*it;
    const auto templ = m_templates.constFind(name);
    return templ == m_templates.cend() ? nullptr : &*templ;
}

const Group *Schema::group(const QString &name) const
{
    const auto it = m_groups.constFind(name);
    return it == m_groups.cend() ? nullptr : &*it;
}

const Schema::Preset *Schema::preset(const QString &name) const
{
    const auto it = m_presets.constFind(name);
    return it == m_presets.cend() ? nullptr : &*it;
}

QStringList Schema::options() const
{
    QStringList out;
    for (const QString &name : m_templates.keys())
        out << name.mid(AppPrefix.size());
    return out;
}

QStringList Schema::keys(const QString &prefix) const
{
    QStringList out;
    for (auto it = m_keys.cbegin(); it != m_keys.cend(); ++it)
        if (matches(it.key(), prefix))
            out << it.key();
    return out;
}

QStringList Schema::provided(const Key &key) const
{
    const auto it = m_providers.constFind(key.enumFrom);
    return it == m_providers.cend() ? QStringList() : (*it)();
}

bool Schema::accepts(const Key &key, const QVariant &value) const
{
    if (!hasType(key.type, value))
        return false;
    if (key.type == "s"_L1 && value.toString().size() > key.maxLength)
        return false;
    if (key.type == "as"_L1) {
        const QStringList items = value.toStringList();
        if (key.maxItems && items.size() > *key.maxItems)
            return false;
        if (std::any_of(items.cbegin(), items.cend(), [&](const QString &item) { return item.size() > key.maxLength; }))
            return false;
        if (key.validator == "eq_filters"_L1 && !eqFilters(items))
            return false;
    }
    if (!key.enumFrom.isEmpty())
        return provided(key).contains(value.toString());
    if (!key.enumValues.isEmpty()) {
        if (key.type != "as"_L1)
            return key.enumValues.contains(value);
        const QStringList items = value.toStringList();
        return std::all_of(items.cbegin(), items.cend(), [&](const QString &item) {
            return key.enumValues.contains(QVariant(item));
        });
    }
    if (isNumeric(key.type)) {
        const double number = value.toDouble();
        if (!std::isfinite(number) || (key.min && number < *key.min) || (key.max && number > *key.max))
            return false;
    }
    return true;
}

QVariant Schema::clamp(const Key &key, const QVariant &value) const
{
    if (!hasType(key.type, value))
        return {};
    if (!isNumeric(key.type) || !key.enumValues.isEmpty())
        return accepts(key, value) ? value : QVariant();
    double number = value.toDouble();
    if (key.min)
        number = std::max(number, *key.min);
    if (key.max)
        number = std::min(number, *key.max);
    QVariant out = number;
    if (key.type == "u"_L1)
        out = uint(number);
    else if (key.type == "i"_L1)
        out = int(number);
    return accepts(key, out) ? out : QVariant();
}

QJsonObject Schema::describe(const QString &prefix) const
{
    QJsonObject out;
    for (const Key &key : m_keys)
        if (matches(key.name, prefix))
            out.insert(key.name, entry(key));
    const QRegularExpressionMatch app = AppPattern.match(prefix);
    for (const Key &key : m_templates) {
        const QString instance = AppPrefix + app.captured(1) + key.name.mid(AppPrefix.size() - 1);
        if (matches(key.name, prefix))
            out.insert(key.name, entry(key));
        else if (app.hasMatch() && matches(instance, prefix))
            out.insert(instance, entry(key));
    }
    return out;
}

QJsonObject Schema::entry(const Key &key) const
{
    QJsonObject entry{
        {u"owner"_s, key.owner},
        {u"type"_s, key.type},
        {u"default"_s, toJson(key.defaultValue)},
        {u"writable"_s, key.writable},
        {u"override"_s, key.overridable},
        {u"applier"_s, key.applier.isEmpty() ? QJsonValue() : QJsonValue(key.applier)},
        {u"group"_s, key.group.isEmpty() ? QJsonValue() : QJsonValue(key.group)},
        {u"label"_s, key.label},
        {u"ui"_s, key.ui},
    };
    const Group *described = key.group.isEmpty() ? nullptr : group(key.group);
    entry.insert(u"group_ui"_s, described ? QJsonValue(QJsonObject{{u"label"_s, described->label},
                                                                     {u"order"_s, described->order}})
                                          : QJsonValue());
    if (!key.enumFrom.isEmpty()) {
        entry.insert(u"enum_from"_s, key.enumFrom);
        entry.insert(u"enum"_s, QJsonArray::fromStringList(provided(key)));
        const auto labels = m_labels.constFind(key.enumFrom);
        if (labels != m_labels.cend()) {
            QJsonObject ui = key.ui;
            QJsonObject values = (*labels)();
            const QJsonObject own = key.ui.value("values"_L1).toObject();
            for (auto it = own.begin(); it != own.end(); ++it)
                values.insert(it.key(), it.value());
            ui.insert(u"values"_s, values);
            entry.insert(u"ui"_s, ui);
        }
    } else if (!key.enumValues.isEmpty()) {
        QJsonArray values;
        for (const QVariant &value : key.enumValues)
            values << toJson(value);
        entry.insert(u"enum"_s, values);
    }
    if (key.type == "s"_L1 || key.type == "as"_L1)
        entry.insert(u"max_len"_s, key.maxLength);
    if (key.maxItems)
        entry.insert(u"max_items"_s, *key.maxItems);
    if (!key.validator.isEmpty())
        entry.insert(u"validator"_s, key.validator);
    if (key.min)
        entry.insert(u"min"_s, *key.min);
    if (key.max)
        entry.insert(u"max"_s, *key.max);
    if (key.floorValue)
        entry.insert(u"floor"_s, *key.floorValue);
    if (key.coalesce)
        entry.insert(u"store"_s, u"coalesce"_s);
    if (key.perApp)
        entry.insert(u"per_app"_s, true);
    return entry;
}
