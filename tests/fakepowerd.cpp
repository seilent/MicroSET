#include "fakepowerd.h"

#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusVariant>

using namespace Qt::StringLiterals;

namespace {

const QString Name = u"net.seilent.MicroOs.Power"_s;
const QString PowerPath = u"/net/seilent/MicroOs/Power"_s;
const QString Properties = u"org.freedesktop.DBus.Properties"_s;

}

FakePowerd::FakePowerd(const QString &address)
    : m_address(address)
{
    values[Power1] = {
        {u"Profile"_s, u"balanced"_s},
        {u"GpuLevel"_s, u"auto"_s},
        {u"ManualGpuClock"_s, 587u},
        {u"CpuGovernor"_s, u"schedutil"_s},
    };
    values[Tdp1] = {{u"TdpLimit"_s, 10u}, {u"TdpLimitMin"_s, 3u}, {u"TdpLimitMax"_s, 10u}};
}

FakePowerd::~FakePowerd()
{
    stop();
}

bool FakePowerd::start()
{
    m_connection = u"fake-powerd-%1"_s.arg(++m_starts);
    QDBusConnection bus = QDBusConnection::connectToBus(m_address, m_connection);
    return bus.isConnected() && bus.registerVirtualObject(PowerPath, this) && bus.registerService(Name);
}

void FakePowerd::stop()
{
    if (m_connection.isEmpty())
        return;
    QDBusConnection(m_connection).unregisterObject(PowerPath);
    QDBusConnection::disconnectFromBus(m_connection);
    m_connection.clear();
}

QString FakePowerd::unique() const
{
    return QDBusConnection(m_connection).baseService();
}

void FakePowerd::emitChanged(const QString &interface, const QVariantMap &changed)
{
    QDBusMessage signal = QDBusMessage::createSignal(PowerPath, Properties, u"PropertiesChanged"_s);
    signal << interface << changed << QStringList();
    QDBusConnection(m_connection).send(signal);
}

QString FakePowerd::introspect(const QString &) const
{
    return {};
}

bool FakePowerd::handleMessage(const QDBusMessage &message, const QDBusConnection &connection)
{
    const QString interface = message.arguments().value(0).toString();
    if (message.interface() != Properties || !values.contains(interface)) {
        connection.send(message.createErrorReply(u"org.freedesktop.DBus.Error.UnknownInterface"_s, interface));
        return true;
    }
    QVariantMap &props = values[interface];
    const QString name = message.arguments().value(1).toString();
    if (message.member() == "GetAll"_L1) {
        connection.send(message.createReply(QVariant(props)));
    } else if (message.member() == "Get"_L1) {
        connection.send(message.createReply(QVariant::fromValue(QDBusVariant(props.value(name)))));
    } else if (message.member() == "Set"_L1) {
        QVariant value = message.arguments().value(2).value<QDBusVariant>().variant();
        sets << interface.section(u'.', -1) + u'.' + name + u'=' + value.toString();
        if (failSets > 0) {
            --failSets;
            connection.send(message.createErrorReply(u"org.freedesktop.DBus.Error.InvalidArgs"_s, u"refused"_s));
            if (!afterFailure.isEmpty())
                emitChanged(Power1, std::exchange(afterFailure, {}));
            return true;
        }
        if (adjust)
            value = adjust(name, value);
        const bool changed = props.value(name) != value;
        props.insert(name, value);
        if (!beforeReply.isEmpty())
            emitChanged(Power1, std::exchange(beforeReply, {}));
        else if (changed)
            emitChanged(interface, {{name, value}});
        connection.send(message.createReply());
    } else {
        connection.send(message.createErrorReply(u"org.freedesktop.DBus.Error.UnknownMethod"_s, message.member()));
    }
    return true;
}
