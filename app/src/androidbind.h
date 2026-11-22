#pragma once

#include "androidstatus.h"
#include "settingsmodel.h"

#include <QObject>

inline void bindAndroid(AndroidStatus &android, SettingsModel &model)
{
    using namespace Qt::StringLiterals;
    model.setAndroid(android.status());
    QObject::connect(&android, &AndroidStatus::changed, &model, [&android, &model] { model.setAndroid(android.status()); });
    QObject::connect(&model, &SettingsModel::googlePlayRequested, &android, &AndroidStatus::installGooglePlay);
    QObject::connect(&android, &AndroidStatus::failed, &model,
                     [&model] { emit model.refused(u"Install Google Play"_s, SettingsModel::NotAvailable); });
}
