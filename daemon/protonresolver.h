#pragma once

#include "protonprofiles.h"
#include "settings.h"

#include <QJsonObject>
#include <QString>

class ProtonResolver
{
public:
    struct Resolved
    {
        QString value;
        QString source;
    };

    static constexpr qsizetype MaxAppIds = 2000;

    ProtonResolver(const Settings &settings, const ProtonProfiles &profiles);

    Resolved resolve(const QString &appid, const QString &option, bool profile = true, bool withOwn = true) const;
    Resolved preset(const QString &appid) const;
    bool profileInForce(const QString &appid) const;
    QStringList options(const QString &feature) const;
    QStringList apps() const;
    QJsonObject effective(const QString &appid) const;
    QJsonObject entryOf(const QString &appid, const QString &option) const;
    QJsonObject resolveMany(const QStringList &appids, const QStringList &options, QString *error) const;
    const ProtonProfiles &profiles() const;

private:
    Resolved chain(const QString &appid, const QString &option, bool profile, bool withOwn) const;

    const Settings &m_settings;
    const ProtonProfiles &m_profiles;
};
