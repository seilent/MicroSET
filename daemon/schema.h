#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QMap>
#include <QSet>
#include <QStringList>
#include <QVariant>

#include <functional>
#include <optional>

struct Key
{
    QString name;
    QString owner;
    QString file;
    QString type;
    QVariant defaultValue;
    QVariantList enumValues;
    QString enumFrom;
    std::optional<double> min;
    std::optional<double> max;
    std::optional<double> floorValue;
    bool coalesce = false;
    int maxLength = 256;
    std::optional<int> maxItems;
    QString validator;
    bool writable = true;
    bool overridable = false;
    bool perApp = false;
    QString applier;
    QString group;
    QString label;
    QJsonObject ui;
};

struct Group
{
    QString label;
    int order = 0;
    QString file;
};

class Schema
{
public:
    using Provider = std::function<QStringList()>;
    using Labels = std::function<QJsonObject()>;
    using Preset = QMap<QString, QString>;

    void addApplier(const QString &name);
    void addProvider(const QString &name, Provider provider, Labels labels = {});
    void load(const QString &dir);

    const Key *key(const QString &name) const;
    const Key *perApp(const QString &option) const;
    const Group *group(const QString &name) const;
    const Preset *preset(const QString &name) const;
    QStringList keys(const QString &prefix) const;
    QStringList options() const;
    bool accepts(const Key &key, const QVariant &value) const;
    QVariant clamp(const Key &key, const QVariant &value) const;
    QJsonObject describe(const QString &prefix) const;

    static bool matches(const QString &key, const QString &prefix);
    static bool isAppId(const QString &appid);
    static bool splitApp(const QString &name, QString *appid, QString *option);
    static QVariant fromJson(const QString &type, const QJsonValue &json);
    static QJsonValue toJson(const QVariant &value);
    static QString text(const QVariant &value);

private:
    void loadFile(const QString &dir, const QString &file);
    std::optional<Key> parseKey(const QString &name, const QString &owner, const QJsonObject &object, QString *why) const;
    void parseGroups(const QString &path, const QString &file, const QJsonValue &groups);
    void parsePresets(const QString &path, const QJsonValue &presets);
    const Key *defined(const QString &name) const;
    QJsonObject entry(const Key &key) const;
    QStringList provided(const Key &key) const;

    QMap<QString, Key> m_keys;
    QMap<QString, Key> m_templates;
    QMap<QString, Preset> m_presets;
    QMap<QString, Group> m_groups;
    QSet<QString> m_appliers;
    QMap<QString, Provider> m_providers;
    QMap<QString, Labels> m_labels;
};
