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

    ProtonResolver(const Settings &settings, const ProtonProfiles &profiles);

    Resolved resolve(const QString &appid, const QString &option, bool profile = true) const;
    Resolved preset(const QString &appid) const;
    bool profileInForce(const QString &appid) const;
    QStringList options(const QString &feature) const;
    QStringList apps() const;
    QJsonObject effective(const QString &appid) const;
    const ProtonProfiles &profiles() const;

private:
    const Settings &m_settings;
    const ProtonProfiles &m_profiles;
};
