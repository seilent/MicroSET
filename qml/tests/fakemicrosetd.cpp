#include "fakemicrosetd.h"

#include "protonprofiles.h"
#include "protonresolver.h"
#include "schema.h"
#include "settings.h"
#include "store.h"

#include <QDBusConnection>
#include <QDBusVariant>
#include <QDBusVirtualObject>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtEndian>

using namespace Qt::StringLiterals;

namespace {

const QString Name = u"net.seilent.MicroSet"_s;
const QString Path = u"/net/seilent/MicroSet"_s;
const QString Interface = u"net.seilent.MicroSet1"_s;

void loadSchema(Schema &schema, const QStringList &files, const QTemporaryDir &dir)
{
    for (const QString &file : files)
        QFile::copy(file, dir.filePath(QFileInfo(file).fileName()));
    for (const QString &applier : {u"power"_s, u"alsa"_s, u"ssc"_s, u"rgb"_s, u"backlight"_s})
        schema.addApplier(applier);
    schema.addProvider(u"sessions"_s, [] { return QStringList{u"microdap"_s, u"microemu"_s, u"steam"_s}; });
    schema.addProvider(u"gpu_undervolts"_s, [] { return QStringList{u"off"_s, u"low"_s}; },
                       [] { return QJsonObject{{u"off"_s, u"Off"_s}, {u"low"_s, u"Low"_s}}; });
    schema.load(dir.path());
}

void write(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(data);
}

QByteArray binaryText(char type, const QByteArray &key, const QByteArray &value)
{
    return QByteArray(1, type) + key + '\0' + value + '\0';
}

QByteArray binaryInt(const QByteArray &key, quint32 value)
{
    QByteArray out = QByteArray(1, '\x02') + key + '\0';
    out.resize(out.size() + 4);
    qToLittleEndian(value, out.data() + out.size() - 4);
    return out;
}

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
    Schema schema;
    loadSchema(schema, files, dir);
    QJsonObject out = schema.describe({});
    if (out.contains("display.rotation"_L1)) {
        QJsonObject rotation = out.value("display.rotation"_L1).toObject();
        rotation.insert(u"live"_s, true);
        out.insert(u"display.rotation"_s, rotation);
    }
    return out;
}

QJsonObject FakeMicroSetd::effective(const QStringList &files, const QVariantMap &stored, const QByteArray &profiles,
                                     const QString &appid)
{
    QTemporaryDir dir;
    QTemporaryDir state;
    Schema schema;
    loadSchema(schema, files, dir);
    write(state.filePath(u"settings.json"_s),
          QJsonDocument(QJsonObject{{u"version"_s, 1}, {u"values"_s, QJsonObject::fromVariantMap(stored)}}).toJson());
    write(state.filePath(u"profiles.json"_s), profiles);
    Store store(state.path());
    store.load();
    Settings settings(schema, store);
    ProtonProfiles table;
    table.load(state.filePath(u"profiles.json"_s), schema);
    return ProtonResolver(settings, table).effective(appid);
}

void FakeMicroSetd::writeSteam(const QString &root, const QList<QPair<QString, QString>> &games,
                               const QList<QPair<quint32, QString>> &shortcuts)
{
    write(root + u"/steamapps/libraryfolders.vdf"_s,
          "\"libraryfolders\"\n{\n\t\"0\"\n\t{\n\t\t\"path\"\t\t\"" + root.toUtf8() + "\"\n\t}\n}\n");
    for (const auto &[appid, name] : games)
        write(root + u"/steamapps/appmanifest_"_s + appid + u".acf"_s,
              "\"AppState\"\n{\n\t\"appid\"\t\t\"" + appid.toUtf8() + "\"\n\t\"name\"\t\t\"" + name.toUtf8()
                  + "\"\n\t\"installdir\"\t\t\"" + appid.toUtf8() + "\"\n}\n");
    if (shortcuts.isEmpty())
        return;
    QByteArray body;
    for (qsizetype n = 0; n < shortcuts.size(); ++n)
        body += '\0' + QByteArray::number(n) + '\0' + binaryInt("appid", shortcuts.at(n).first)
            + binaryText('\x01', "AppName", shortcuts.at(n).second.toUtf8()) + binaryText('\x01', "Exe", "\"game.exe\"")
            + '\x08';
    write(root + u"/userdata/1145555777/config/shortcuts.vdf"_s, QByteArray(1, '\0') + "shortcuts" + '\0' + body + "\x08\x08");
}

void FakeMicroSetd::writeAppInfo(const QString &root, const QList<QPair<quint32, QByteArray>> &types,
                                 const QMap<quint32, QByteArray> &extra)
{
    const auto number = [](quint32 value) {
        QByteArray out(4, '\0');
        qToLittleEndian(value, out.data());
        return out;
    };
    const auto key = [&number](char type, quint32 index) { return QByteArray(1, type) + number(index); };
    QByteArray apps;
    for (const auto &[appid, type] : types) {
        QByteArray common = key('\x01', 4) + "app " + QByteArray::number(appid) + '\0' + extra.value(appid);
        if (!type.isEmpty())
            common += key('\x01', 3) + type + '\0';
        const QByteArray section = QByteArray(60, '\0') + key('\x00', 0) + key('\x02', 1) + number(appid) + key('\x00', 2)
            + common + "\x08\x08\x08";
        apps += number(appid) + number(quint32(section.size())) + section;
    }
    apps += number(0);
    QByteArray head = number(0x07564429) + number(1) + QByteArray(8, '\0');
    qToLittleEndian(qint64(head.size() + apps.size()), head.data() + 8);
    write(root + u"/appcache/appinfo.vdf"_s,
          head + apps + number(5) + QByteArray("appinfo\0appid\0common\0type\0name\0", 31));
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

void FakeMicroSetd::setProton(const QStringList &files, const QByteArray &profiles)
{
    QMutexLocker locker(&m_lock);
    m_files = files;
    m_profiles = profiles;
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

QStringList FakeMicroSetd::calls() const
{
    QMutexLocker locker(&m_lock);
    return m_calls;
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
        const QVariant effective = m_overrides.value(key, value);
        moved = m_values.value(key) != effective;
        m_values.insert(key, effective);
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
    } else if (message.member() == "Effective"_L1 && !m_files.isEmpty()) {
        const QJsonObject out = effective(m_files, m_stored, m_profiles, key);
        bus.send(message.createReply(QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact))));
    } else if (message.member() == "Get"_L1) {
        bus.send(message.createReply(QVariant::fromValue(QDBusVariant(m_values.value(key)))));
    } else if (message.member() == "Subscribe"_L1 || message.member() == "Unsubscribe"_L1) {
        m_subscriptions << message.member() + u' ' + key;
        bus.send(message.createReply());
    } else if (message.member() == "Override"_L1 || message.member() == "ClearOverride"_L1) {
        const bool clear = message.member() == "ClearOverride"_L1;
        if (!m_overrides.contains(key))
            m_base.insert(key, m_values.value(key));
        const QVariant value = !clear ? message.arguments().value(1).value<QDBusVariant>().variant()
            : m_stored.contains(key) ? m_stored.value(key)
                                     : m_base.value(key);
        m_calls << (clear ? message.member() + u' ' + key : message.member() + u' ' + key + u' ' + value.toString());
        if (clear) {
            m_overrides.remove(key);
            m_base.remove(key);
        } else {
            m_overrides.insert(key, value);
        }
        const bool moved = value.isValid() && m_values.value(key) != value;
        if (value.isValid())
            m_values.insert(key, value);
        else
            m_values.remove(key);
        bus.send(message.createReply());
        if (moved) {
            locker.unlock();
            emitChanged(key, value);
        }
    } else if (message.member() == "Set"_L1) {
        m_sets << qMakePair(key, message.arguments().value(1).value<QDBusVariant>().variant());
        m_calls << u"Set "_s + key + u' ' + m_sets.last().second.toString();
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
