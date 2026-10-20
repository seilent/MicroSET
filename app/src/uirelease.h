#pragma once

#include "overlay.h"
#include "settingsmodel.h"

#include <QQmlApplicationEngine>

#include <malloc.h>

#include <memory>

inline void releaseUi(Overlay &overlay, SettingsModel *model, std::unique_ptr<QQmlApplicationEngine> &engine)
{
    if (model)
        model->endPreviews();
    overlay.setWindow(nullptr);
    overlay.setToastWindow(nullptr);
    engine.reset();
    malloc_trim(0);
}
