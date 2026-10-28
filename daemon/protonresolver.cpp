#include "protonresolver.h"

#include <QJsonArray>
#include <QSet>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

const QString PresetOption = u"fex.preset"_s;
const QString ProfileOption = u"profile"_s;
const QString ProfilesKey = u"proton.profiles"_s;
const QString On = u"on"_s;

QString app(const QString &appid, const QString &option)
{
    return u"app."_s + appid + u'.' + option;
}

QJsonObject entry(const ProtonResolver::Resolved &resolved)
{
    return {{u"value"_s, resolved.value}, {u"source"_s, resolved.source}};
}

}

ProtonResolver::ProtonResolver(const Settings &settings, const ProtonProfiles &profiles)
    : m_settings(settings)
    , m_profiles(profiles)
{
}

ProtonResolver::Resolved ProtonResolver::preset(const QString &appid) const
{
    if (!appid.isEmpty() && m_settings.isSet(app(appid, PresetOption)))
        return {m_settings.value(app(appid, PresetOption)).toString(), u"set"_s};
    return {m_settings.value(PresetOption).toString(), m_settings.isSet(PresetOption) ? u"global"_s : u"default"_s};
}

ProtonResolver::Resolved ProtonResolver::resolve(const QString &appid, const QString &option, bool profile) const
{
    const Schema &schema = m_settings.schema();
    const bool fex = option.startsWith("fex."_L1);
    if (!appid.isEmpty() && m_settings.isSet(app(appid, option)))
        return {m_settings.value(app(appid, option)).toString(), u"set"_s};
    if (fex) {
        const Resolved chosen = preset(appid);
        const Schema::Preset *values = schema.preset(chosen.value);
        if (chosen.source == "set"_L1)
            return {values ? values->value(option) : QString(), u"preset"_s};
        if (chosen.source == "global"_L1 && values && values->contains(option))
            return {values->value(option), u"global"_s};
    } else if (m_settings.isSet(option)) {
        return {m_settings.value(option).toString(), u"global"_s};
    }
    const ProtonProfiles::Entry *own = profile ? m_profiles.entry(appid) : nullptr;
    if (own && own->set.contains(option) && profileInForce(appid))
        return {own->set.value(option), u"profile"_s};
    const Key *global = fex ? nullptr : schema.key(option);
    return {global ? global->defaultValue.toString() : QString(), u"default"_s};
}

bool ProtonResolver::profileInForce(const QString &appid) const
{
    return !appid.isEmpty() && m_profiles.entry(appid) && m_settings.value(ProfilesKey) == On
        && m_settings.value(app(appid, ProfileOption)) == On;
}

QStringList ProtonResolver::options(const QString &feature) const
{
    QStringList out;
    for (const QString &option : m_settings.schema().options())
        if (option.startsWith(feature + u'.') && option != PresetOption)
            out << option;
    return out;
}

QStringList ProtonResolver::apps() const
{
    QSet<QString> all = m_settings.apps();
    for (const QString &appid : m_profiles.apps())
        all.insert(appid);
    QStringList out(all.cbegin(), all.cend());
    std::sort(out.begin(), out.end());
    return out;
}

QJsonObject ProtonResolver::effective(const QString &appid) const
{
    QJsonObject options{{PresetOption, entry(preset(appid))}};
    for (const QString &option : m_settings.schema().options())
        if (option != PresetOption && option != ProfileOption)
            options.insert(option, entry(resolve(appid, option)));
    QJsonObject out{{u"appid"_s, appid}, {u"options"_s, options}};
    const ProtonProfiles::Entry *own = m_profiles.entry(appid);
    if (!own)
        return out;
    QJsonObject set;
    QJsonArray overridden;
    for (auto it = own->set.cbegin(); it != own->set.cend(); ++it) {
        set.insert(it.key(), it.value());
        if (resolve(appid, it.key(), false).source != "default"_L1)
            overridden << it.key();
    }
    QJsonObject profile{{u"on"_s, profileInForce(appid)}, {u"set"_s, set}, {u"overridden"_s, overridden}};
    if (!own->tool.isEmpty())
        profile.insert(u"tool"_s, own->tool);
    out.insert(u"profile"_s, profile);
    return out;
}

const ProtonProfiles &ProtonResolver::profiles() const
{
    return m_profiles;
}
