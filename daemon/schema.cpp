#include "schema.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

#include <cmath>
#include <limits>

using namespace Qt::StringLiterals;

namespace {

const QString PrivilegedFile = u"10-microset.json"_s;
const QString PrivilegedOwner = u"microset"_s;
const QStringList Reserved = {u"microset"_s, u"power"_s, u"meta"_s, u"session"_s,
                               u"audio"_s, u"display"_s, u"rgb"_s, u"system"_s};
const QStringList Types = {u"s"_s, u"b"_s, u"u"_s, u"i"_s, u"d"_s, u"as"_s};

bool isNumeric(const QString &type)
{
    return type == "u"_L1 || type == "i"_L1 || type == "d"_L1;
}

bool hasType(const QString &type, const QVariant &value)
{
    static const QMap<QString, int> ids = {
        {u"s"_s, QMetaType::QString}, {u"b"_s, QMetaType::Bool},   {u"u"_s, QMetaType::UInt},
        {u"i"_s, QMetaType::Int},     {u"d"_s, QMetaType::Double}, {u"as"_s, QMetaType::QStringList},
    };
    return ids.contains(type) && value.typeId() == ids.value(type);
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

void Schema::addProvider(const QString &name, Provider provider)
{
    m_providers.insert(name, std::move(provider));
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
    static const QRegularExpression keyPattern(u"^[a-z][a-z0-9]*(\\.[A-Za-z0-9_]+)+$"_s);

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
        if (!keyPattern.match(name).hasMatch()) {
            qWarning("%s: %s is not a key name, refused", qPrintable(path), qPrintable(name));
            continue;
        }
        if (!privileged && !name.startsWith(owner + u'.')) {
            qWarning("%s: %s is outside %s.*, refused", qPrintable(path), qPrintable(name), qPrintable(owner));
            continue;
        }
        if (const Key *first = key(name)) {
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
        m_keys.insert(name, *parsed);
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
    const QJsonValue maxLength = object.value("max_len"_L1);
    if (!maxLength.isUndefined()) {
        if (key.type != "s"_L1 || !maxLength.isDouble() || maxLength.toDouble() < 1
            || maxLength.toDouble() > 65536 || maxLength.toDouble() != std::floor(maxLength.toDouble())) {
            *why = u"bad max_len"_s;
            return {};
        }
        key.maxLength = maxLength.toInt();
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
    return it == m_keys.cend() ? nullptr : &*it;
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
    for (const Key &key : m_keys) {
        if (!matches(key.name, prefix))
            continue;
        QJsonObject entry{
            {u"owner"_s, key.owner},
            {u"type"_s, key.type},
            {u"default"_s, toJson(key.defaultValue)},
            {u"writable"_s, key.writable},
            {u"applier"_s, key.applier.isEmpty() ? QJsonValue() : QJsonValue(key.applier)},
            {u"group"_s, key.group.isEmpty() ? QJsonValue() : QJsonValue(key.group)},
            {u"label"_s, key.label},
        };
        if (!key.enumFrom.isEmpty()) {
            entry.insert(u"enum_from"_s, key.enumFrom);
            entry.insert(u"enum"_s, QJsonArray::fromStringList(provided(key)));
        } else if (!key.enumValues.isEmpty()) {
            QJsonArray values;
            for (const QVariant &value : key.enumValues)
                values << toJson(value);
            entry.insert(u"enum"_s, values);
        }
        if (key.type == "s"_L1)
            entry.insert(u"max_len"_s, key.maxLength);
        if (key.min)
            entry.insert(u"min"_s, *key.min);
        if (key.max)
            entry.insert(u"max"_s, *key.max);
        out.insert(key.name, entry);
    }
    return out;
}
