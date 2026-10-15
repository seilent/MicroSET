#include "sscapplier.h"

using namespace Qt::StringLiterals;

namespace {

const QString Orientation = u"display.orientation"_s;
const QString AutoRotate = u"display.auto_rotate"_s;
const QString Rotation = u"display.rotation"_s;

}

SscApplier::SscApplier(Settings &settings, SscRotation &rotation, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_rotation(rotation)
{
}

QString SscApplier::nameOf(int turn)
{
    static const QStringList names = {u"portrait"_s, u"landscape"_s, u"portrait-flipped"_s, u"landscape-flipped"_s};
    return names.value(turn);
}

void SscApplier::start()
{
    connect(&m_settings, &Settings::changed, this, [this](const QString &key) {
        if (key == AutoRotate)
            update();
        else if (key == Orientation)
            publish();
    });
    connect(&m_rotation, &SscRotation::turnChanged, this, &SscApplier::publish);
    update();
}

void SscApplier::setSubscribed(bool held)
{
    if (m_subscribed == held)
        return;
    m_subscribed = held;
    qInfo("ssc: %s", held ? "subscribed" : "no subscriber");
    update();
}

void SscApplier::update()
{
    m_rotation.setWanted(m_subscribed && m_settings.value(AutoRotate).toBool());
    publish();
}

void SscApplier::publish()
{
    const QString sensed = m_rotation.wanted() ? nameOf(m_rotation.turn()) : QString();
    m_settings.setLive(Rotation, sensed.isEmpty() ? m_settings.value(Orientation) : QVariant(sensed));
}
