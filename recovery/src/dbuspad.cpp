#include "dbuspad.h"

#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QDebug>

using namespace Qt::StringLiterals;

namespace {

struct NameRow
{
    const char *name;
    int action;
};

constexpr NameRow nameTable[] = {
    {"ui_up", DbusPad::Up},
    {"ui_down", DbusPad::Down},
    {"ui_left", DbusPad::Left},
    {"ui_right", DbusPad::Right},
    {"ui_accept", DbusPad::A},
    {"ui_back", DbusPad::B},
    {"ui_context", DbusPad::X},
    {"ui_action", DbusPad::Y},
    {"ui_l1", DbusPad::LB},
    {"ui_r1", DbusPad::RB},
    {"ui_l2", DbusPad::LT},
    {"ui_r2", DbusPad::RT},
    {"ui_option", DbusPad::Start},
    {"ui_select", DbusPad::Back},
};

constexpr const char *labels[DbusPad::ActionCount] = {
    "up", "down", "left", "right", "a", "b", "x", "y", "lb", "rb", "lt", "rt", "start", "back",
};

QString propertiesInterface()
{
    return u"org.freedesktop.DBus.Properties"_s;
}

QString holderProperty()
{
    return u"Holder"_s;
}

}

DbusPad::DbusPad(const QDBusConnection &session, const QDBusConnection &system, QObject *parent)
    : QObject(parent)
    , m_session(session)
    , m_system(system)
    , m_watcher(arbiterService(), session, QDBusServiceWatcher::WatchForOwnerChange)
{
    m_repeat.setTimerType(Qt::PreciseTimer);
    connect(&m_repeat, &QTimer::timeout, this, &DbusPad::repeatTick);
    connect(&m_watcher, &QDBusServiceWatcher::serviceRegistered, this, &DbusPad::acquire);
    connect(&m_watcher, &QDBusServiceWatcher::serviceUnregistered, this, [this] { setHolder({}); });
    if (!m_system.connect(u"org.shadowblip.InputPlumber"_s, QString(), u"org.shadowblip.Input.DBusDevice"_s,
                          u"InputEvent"_s, this, SLOT(onInputEvent(QString,double))))
        qWarning() << "microset-recovery: cannot subscribe to InputEvent" << m_system.lastError().message();
    if (!m_session.connect(arbiterService(), arbiterPath(), propertiesInterface(), u"PropertiesChanged"_s, this,
                           SLOT(onArbiterProperties(QString,QVariantMap,QStringList))))
        qWarning() << "microset-recovery: cannot subscribe to the intercept arbiter" << m_session.lastError().message();
    acquire();
}

int DbusPad::actionFor(const QString &name)
{
    for (const NameRow &row : nameTable) {
        if (name == QLatin1StringView(row.name))
            return row.action;
    }
    return -1;
}

QString DbusPad::arbiterService()
{
    return u"net.seilent.MicroOs.Intercept"_s;
}

QString DbusPad::arbiterPath()
{
    return u"/net/seilent/MicroOs/Intercept"_s;
}

QString DbusPad::arbiterInterface()
{
    return u"net.seilent.MicroOs.Intercept1"_s;
}

bool DbusPad::holding() const
{
    return m_holding;
}

QString DbusPad::actionName(DbusPad::Action action) const
{
    if (action < 0 || action >= ActionCount)
        return {};
    return QString::fromLatin1(labels[action]);
}

void DbusPad::acquire()
{
    const QDBusMessage call = QDBusMessage::createMethodCall(arbiterService(), arbiterPath(), arbiterInterface(),
                                                             u"Acquire"_s);
    auto *watcher = new QDBusPendingCallWatcher(m_session.asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        const QDBusPendingReply<> reply = *w;
        if (reply.isError()) {
            qWarning() << "microset-recovery: Acquire failed" << reply.error().message();
            return;
        }
        readHolder();
    });
}

void DbusPad::readHolder()
{
    QDBusMessage call = QDBusMessage::createMethodCall(arbiterService(), arbiterPath(), propertiesInterface(),
                                                       u"Get"_s);
    call << arbiterInterface() << holderProperty();
    auto *watcher = new QDBusPendingCallWatcher(m_session.asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        const QDBusPendingReply<QDBusVariant> reply = *w;
        setHolder(reply.isError() ? QString() : reply.value().variant().toString());
    });
}

void DbusPad::setHolder(const QString &holder)
{
    const bool holding = !holder.isEmpty() && holder == m_session.baseService();
    if (holding == m_holding)
        return;
    m_holding = holding;
    if (!holding)
        releaseAll();
    emit holdingChanged();
}

void DbusPad::onArbiterProperties(const QString &interface, const QVariantMap &changed,
                                  const QStringList &invalidated)
{
    if (interface != arbiterInterface())
        return;
    if (changed.contains(holderProperty()))
        setHolder(changed.value(holderProperty()).toString());
    else if (invalidated.contains(holderProperty()))
        readHolder();
}

void DbusPad::onInputEvent(const QString &name, double value)
{
    const int mapped = actionFor(name);
    if (mapped < 0 || !m_holding)
        return;
    const Action a = static_cast<Action>(mapped);
    const bool on = value >= PressThreshold;
    if (on == m_down[a])
        return;
    if (on) {
        press(a);
        if (a <= Right)
            startRepeat(a);
        return;
    }
    release(a);
    if (m_repeatAction != a)
        return;
    stopRepeat();
    for (int i = Up; i <= Right; ++i) {
        if (m_down[i]) {
            startRepeat(static_cast<Action>(i));
            return;
        }
    }
}

void DbusPad::repeatTick()
{
    if (m_repeatAction < 0) {
        m_repeat.stop();
        return;
    }
    emit action(static_cast<Action>(m_repeatAction), true);
    if (m_repeat.interval() != RepeatIntervalMs)
        m_repeat.start(RepeatIntervalMs);
}

void DbusPad::startRepeat(Action direction)
{
    m_repeatAction = direction;
    m_repeat.start(RepeatInitialMs);
}

void DbusPad::stopRepeat()
{
    m_repeat.stop();
    m_repeatAction = -1;
}

void DbusPad::press(Action a)
{
    m_down[a] = true;
    emit pressed(a);
    emit action(a, false);
}

void DbusPad::release(Action a)
{
    if (!m_down[a])
        return;
    m_down[a] = false;
    emit released(a);
}

void DbusPad::releaseAll()
{
    stopRepeat();
    for (int i = 0; i < ActionCount; ++i)
        release(static_cast<Action>(i));
}
