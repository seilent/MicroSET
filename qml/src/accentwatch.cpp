#include "accentwatch.h"

#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusServiceWatcher>

using namespace Qt::StringLiterals;

namespace {

const QString Service = u"net.seilent.MicroSet"_s;
const QString Path = u"/net/seilent/MicroSet"_s;
const QString Interface = u"net.seilent.MicroSet1"_s;

}

AccentWatch::AccentWatch(QObject *parent)
    : QObject(parent)
    , m_bus(QString())
{
}

AccentWatch::AccentWatch(const QDBusConnection &bus, QObject *parent)
    : QObject(parent)
    , m_bus(bus)
{
    m_bus.connect(Service, Path, Interface, u"Changed"_s, this, SLOT(onChanged(QString, QDBusVariant)));
    auto *watcher = new QDBusServiceWatcher(Service, m_bus, QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
            [this](const QString &, const QString &, const QString &owner) {
                if (!owner.isEmpty())
                    read();
            });
    read();
}

int AccentWatch::hue() const
{
    return m_hue;
}

bool AccentWatch::off() const
{
    return m_off;
}

void AccentWatch::setFixture(int hue, bool off)
{
    m_hue = hue;
    m_off = off;
    emit changed();
}

void AccentWatch::onChanged(const QString &key, const QDBusVariant &value)
{
    take(key, value.variant());
}

void AccentWatch::read()
{
    for (const QString &key : {HueKey, OffKey}) {
        QDBusMessage get = QDBusMessage::createMethodCall(Service, Path, Interface, u"Get"_s);
        get << key;
        auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(get), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, key](QDBusPendingCallWatcher *w) {
            const QDBusMessage reply = w->reply();
            if (reply.type() == QDBusMessage::ReplyMessage)
                take(key, reply.arguments().value(0).value<QDBusVariant>().variant());
            w->deleteLater();
        });
    }
}

void AccentWatch::take(const QString &key, const QVariant &value)
{
    if (key == HueKey && value.isValid() && value.toInt() != m_hue) {
        m_hue = value.toInt();
        emit changed();
    } else if (key == OffKey && value.isValid() && value.toBool() != m_off) {
        m_off = value.toBool();
        emit changed();
    }
}
