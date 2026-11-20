#pragma once

#include "quickaccess.h"
#include "sessionmenu.h"
#include "settingsmodel.h"

#include <QObject>

template <typename Overlay>
void bindQuickAccess(QuickAccess &quick, SettingsModel &model, Overlay &overlay, SessionMenu &menu)
{
    using namespace Qt::StringLiterals;
    const auto push = [&quick, &model] {
        model.setQuickAccess(quick.available());
        model.setSessionSwitch(quick.owned() ? quick.session() : QString());
    };
    push();
    QObject::connect(&quick, &QuickAccess::ownedChanged, &model, push);
    QObject::connect(&model, &SettingsModel::quickAccessRequested, &quick, &QuickAccess::open);
    QObject::connect(&quick, &QuickAccess::opened, &model, [&overlay] { overlay.hide(); });
    QObject::connect(&quick, &QuickAccess::failed, &model,
                     [&model] { emit model.refused(u"Quick access menu"_s, SettingsModel::NotAvailable); });
    QObject::connect(&model, &SettingsModel::sessionSwitchRequested, &menu,
                     [&quick, &menu](bool inGame) { menu.open(quick.session(), inGame); });
    QObject::connect(&menu, &SessionMenu::listed, &model,
                     [&overlay](const QList<SessionMenuEntry> &entries, const QString &current, bool inGame,
                                bool switching) {
                         if (overlay.shown())
                             overlay.showMenu(entries, current, inGame, switching);
                     });
    QObject::connect(&menu, &SessionMenu::openFailed, &model, [&model, &overlay] {
        if (overlay.shown())
            emit model.refused(u"Switch session"_s, SettingsModel::NotAvailable);
    });
}
