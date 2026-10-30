#pragma once

#include <QString>
#include <QVariantMap>

inline QVariantMap withFrameLimit(QVariantMap info, int limit, int refresh)
{
    if (info.isEmpty())
        return info;
    if (limit >= 0)
        info.insert(QStringLiteral("fpsLimit"), limit);
    if (refresh >= 0)
        info.insert(QStringLiteral("refreshHz"), refresh);
    return info;
}
