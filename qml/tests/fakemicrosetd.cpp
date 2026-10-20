#include "fakemicrosetd.h"

#include "schema.h"

#include <QDBusConnection>
#include <QDBusVariant>
#include <QDBusVirtualObject>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTemporaryDir>

using namespace Qt::StringLiterals;

namespace {

const QString Name = u"net.seilent.MicroSet"_s;
const QString Path = u"/net/seilent/MicroSet"_s;
const QString Interface = u"net.seilent.MicroSet1"_s;

}

class FakeMicroSetd::Handler : public QDBusVirtualObject
{
public:
    explicit Handler(FakeMicroSetd *owner)
        : m_owner(owner)
    {
    }

    QString introspect(const QString &) const override
    {
        return {};
    }

    bool handleMessage(const QDBusMessage &message, const QDBusConnection &) override
    {
        return m_owner->handle(message);
    }

private:
    FakeMicroSetd *m_owner;
};

QJsonObject FakeMicroSetd::describe(const QStringList &files)
{
    QTemporaryDir dir;
    for (const QString &file : files)
        QFile::copy(file, dir.filePath(QFileInfo(file).fileName()));
    Schema schema;
    for (const QString &applier : {u"power"_s, u"alsa"_s, u"ssc"_s, u"rgb"_s, u"backlight"_s})
        schema.addApplier(applier);
    schema.addProvider(u"sessions"_s, [] { return QStringList{u"microdap"_s, u"microemu"_s, u"steam"_s}; });
    schema.addProvider(u"gpu_undervolts"_s, [] { return QStringList{u"off"_s, u"low"_s}; },
                       [] { return QJsonObject{{u"off"_s, u"Off"_s}, {u"low"_s, u"Low"_s}}; });
    schema.load(dir.path());
    QJsonObject out = schema.describe({});
    if (out.contains("display.rotation"_L1)) {
        QJsonObject rotation = out.value("display.rotation"_L1).toObject();
        rotation.insert(u"live"_s, true);
        out.insert(u"display.rotation"_s, rotation);
    }
    return out;
}

QString FakeMicroSetd::fixture(const QString &name)
{
    return QStringLiteral(MICROSET_PAGE_FIXTURES) + u'/' + name;
}

QString FakeMicroSetd::shipped()
{
    return QStringLiteral(MICROSET_SOURCE_DIR) + u"/schema/10-microset.json"_s;
}

FakeMicroSetd::FakeMicroSetd(const QString &address)
    : m_address(address)
{
    m_thread.start();
}

FakeMicroSetd::~FakeMicroSetd()
{
    stop();
    m_thread.quit();
    m_thread.wait();
}

bool FakeMicroSetd::start()
{
    m_connection = u"fake-microsetd-%1"_s.arg(++m_starts);
    m_handler = std::make_unique<Handler>(this);
    m_handler->moveToThread(&m_thread);
    QDBusConnection bus = QDBusConnection::connectToBus(m_address, m_connection);
    return bus.isConnected() && bus.registerVirtualObject(Path, m_handler.get()) && bus.registerService(Name);
}

void FakeMicroSetd::stop()
{
    if (m_connection.isEmpty())
        return;
    QDBusConnection(m_connection).unregisterObject(Path);
    QDBusConnection::disconnectFromBus(m_connection);
    QMetaObject::invokeMethod(m_handler.get(), [] {}, Qt::BlockingQueuedConnection);
    m_handler.reset();
    m_connection.clear();
    QMutexLocker locker(&m_lock);
    m_held.clear();
}

void FakeMicroSetd::release(int count)
{
    QList<QDBusMessage> taken;
    {
        QMutexLocker locker(&m_lock);
        while (!m_held.isEmpty() && count-- != 0)
            taken << m_held.takeFirst();
    }
    for (const QDBusMessage &message : std::as_const(taken))
        apply(message);
}

void FakeMicroSetd::emitChanged(const QString &key, const QVariant &value)
{
    QDBusMessage signal = QDBusMessage::createSignal(Path, Interface, u"Changed"_s);
    signal << key << QVariant::fromValue(QDBusVariant(value));
    QDBusConnection(m_connection).send(signal);
}

void FakeMicroSetd::emitHoldRefused(const QString &key, const QString &reason)
{
    QDBusMessage signal = QDBusMessage::createSignal(Path, Interface, u"HoldRefused"_s);
    signal << key << reason;
    QDBusConnection(m_connection).send(signal);
}

void FakeMicroSetd::setSchema(const QJsonObject &schema)
{
    QMutexLocker locker(&m_lock);
    m_schema = schema;
}

void FakeMicroSetd::setValues(const QVariantMap &values)
{
    QMutexLocker locker(&m_lock);
    m_values = values;
}

void FakeMicroSetd::setStored(const QVariantMap &stored)
{
    QMutexLocker locker(&m_lock);
    m_stored = stored;
}

void FakeMicroSetd::setHold(bool hold)
{
    QMutexLocker locker(&m_lock);
    m_hold = hold;
}

void FakeMicroSetd::refuse(const QString &key, const QString &error)
{
    QMutexLocker locker(&m_lock);
    m_refuse.insert(key, error);
}

QVariantMap FakeMicroSetd::stored() const
{
    QMutexLocker locker(&m_lock);
    return m_stored;
}

FakeMicroSetd::Sets FakeMicroSetd::sets() const
{
    QMutexLocker locker(&m_lock);
    return m_sets;
}

QStringList FakeMicroSetd::subscriptions() const
{
    QMutexLocker locker(&m_lock);
    return m_subscriptions;
}

void FakeMicroSetd::apply(const QDBusMessage &message)
{
    const QString key = message.arguments().value(0).toString();
    const QVariant value = message.arguments().value(1).value<QDBusVariant>().variant();
    bool moved = false;
    {
        QMutexLocker locker(&m_lock);
        m_stored.insert(key, value);
        moved = m_values.value(key) != value;
        m_values.insert(key, value);
    }
    if (moved)
        emitChanged(key, value);
    QDBusConnection(m_connection).send(message.createReply());
}

bool FakeMicroSetd::handle(const QDBusMessage &message)
{
    QDBusConnection bus(m_connection);
    const QString key = message.arguments().value(0).toString();
    QMutexLocker locker(&m_lock);
    if (message.member() == "Schema"_L1) {
        bus.send(message.createReply(QString::fromUtf8(QJsonDocument(m_schema).toJson(QJsonDocument::Compact))));
    } else if (message.member() == "List"_L1) {
        bus.send(message.createReply(QVariant(m_values)));
    } else if (message.member() == "Stored"_L1) {
        QVariantMap out;
        for (auto it = m_stored.cbegin(); it != m_stored.cend(); ++it)
            if (key.isEmpty() || it.key() == key || it.key().startsWith(key + u'.'))
                out.insert(it.key(), it.value());
        bus.send(message.createReply(QVariant(out)));
    } else if (message.member() == "Get"_L1) {
        bus.send(message.createReply(QVariant::fromValue(QDBusVariant(m_values.value(key)))));
    } else if (message.member() == "Subscribe"_L1 || message.member() == "Unsubscribe"_L1) {
        m_subscriptions << message.member() + u' ' + key;
        bus.send(message.createReply());
    } else if (message.member() == "Set"_L1) {
        m_sets << qMakePair(key, message.arguments().value(1).value<QDBusVariant>().variant());
        if (m_refuse.contains(key)) {
            bus.send(message.createErrorReply(m_refuse.value(key), key));
        } else if (m_hold) {
            m_held << message;
        } else {
            locker.unlock();
            apply(message);
        }
    } else {
        bus.send(message.createErrorReply(u"org.freedesktop.DBus.Error.UnknownMethod"_s, message.member()));
    }
    return true;
}
