#pragma once

#include "settings.h"
#include "sscrotation.h"

#include <QObject>

class SscApplier : public QObject
{
    Q_OBJECT

public:
    SscApplier(Settings &settings, SscRotation &rotation, QObject *parent = nullptr);

    static QString nameOf(int turn);

    void start();
    void setSubscribed(bool held);

private:
    void update();
    void publish();

    Settings &m_settings;
    SscRotation &m_rotation;
    bool m_subscribed = false;
};
