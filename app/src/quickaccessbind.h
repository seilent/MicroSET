#pragma once

#include "quickaccess.h"
#include "settingsmodel.h"

#include <QObject>

template <typename Overlay>
void bindQuickAccess(QuickAccess &quick, SettingsModel &model, Overlay &overlay)
{
    using namespace Qt::StringLiterals;
    model.setQuickAccess(quick.available());
    QObject::connect(&quick, &QuickAccess::availableChanged, &model,
                     [&quick, &model] { model.setQuickAccess(quick.available()); });
    QObject::connect(&model, &SettingsModel::quickAccessRequested, &quick, &QuickAccess::open);
    QObject::connect(&quick, &QuickAccess::opened, &model, [&overlay] { overlay.hide(); });
    QObject::connect(&quick, &QuickAccess::failed, &model,
                     [&model] { emit model.refused(u"Quick access menu"_s, SettingsModel::NotAvailable); });
}
