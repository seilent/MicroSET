#include "harness.h"

#include <microset/client.h>

#include <QDBusVirtualObject>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>

#include <memory>
#include <optional>
#include <tuple>

using namespace Qt::StringLiterals;

namespace {

class FakeMicroSet : public QDBusVirtualObject
{
public:
    explicit FakeMicroSet(const QString &address, const QString &connection)
        : m_connection(connection)
    {
        QDBusConnection bus = QDBusConnection::connectToBus(address, connection);
        bus.registerVirtualObject(u"/net/seilent/MicroSet"_s, this);
        bus.registerService(u"net.seilent.MicroSet"_s);
    }

    ~FakeMicroSet() override
    {
        QDBusConnection(m_connection).unregisterObject(u"/net/seilent/MicroSet"_s);
        QDBusConnection::disconnectFromBus(m_connection);
    }

    QString introspect(const QString &) const override
    {
        return {};
    }

    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override
    {
        const QString key = message.arguments().value(0).toString();
        if (message.member() == "Override"_L1) {
            overrides << key;
            if (refuseOverride)
                connection.send(message.createErrorReply(u"org.freedesktop.DBus.Error.InvalidArgs"_s, key));
        } else if (message.member() == "Subscribe"_L1) {
            subscribed << key;
            connection.send(message.createReply());
        } else if (message.member() == "Get"_L1) {
            connection.send(message.createReply(QVariant::fromValue(QDBusVariant(u"value of "_s + key))));
        } else {
            connection.send(message.createErrorReply(u"org.freedesktop.DBus.Error.UnknownMethod"_s, message.member()));
        }
        return true;
    }

    QStringList subscribed;
    QStringList overrides;
    bool refuseOverride = false;

private:
    QString m_connection;
};

}

class ClientTest : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<Harness> h;

private slots:
    void init()
    {
        h = std::make_unique<Harness>();
        h->writeSessions();
        QVERIFY(h->startBus());
    }

    void cleanup()
    {
        h.reset();
    }

    void calls()
    {
        QVERIFY(h->startDaemon());
        MicroSet::Client client(h->client());
        QVERIFY(client.available());
        MicroSet::Error error;
        QCOMPARE(client.get(u"session.default"_s, &error), QVariant(u"steam"_s));
        QVERIFY(error.ok());
        client.get(u"nope.key"_s, &error);
        QCOMPARE(error.name, u"net.seilent.MicroSet1.Error.UnknownKey"_s);
        QCOMPARE(client.list(u"power"_s).size(), 5);
        QCOMPARE(QJsonDocument::fromJson(client.schema(u"power.tdp"_s).toUtf8()).object().keys(), QStringList{u"power.tdp"_s});

        std::optional<MicroSet::Error> result;
        client.set(u"session.default"_s, u"microdap"_s, [&](const MicroSet::Error &e) { result = e; });
        QTRY_VERIFY(result.has_value());
        QVERIFY(result->ok());
        QCOMPARE(client.get(u"session.default"_s), QVariant(u"microdap"_s));
        result.reset();
        client.set(u"power.tdp"_s, 6, [&](const MicroSet::Error &e) { result = e; });
        QTRY_VERIFY(result.has_value());
        QCOMPARE(result->name, u"net.seilent.MicroSet1.Error.InvalidValue"_s);
    }

    void watchedKeysReemittedOnRestart()
    {
        QVERIFY(h->startDaemon());
        MicroSet::Client client(h->client());
        client.watch(u"session.default"_s);
        QSignalSpy spy(&client, &MicroSet::Client::changed);
        h->stopDaemon();
        QVERIFY(!client.available());
        h->writeState(R"({"session.default": "microdap"})");
        QVERIFY(h->startDaemon());
        QTRY_COMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), u"session.default"_s);
        QCOMPARE(spy.at(0).at(1), QVariant(u"microdap"_s));
    }

    void storedAndImport()
    {
        h->writeState(R"({"audio.output": "bt"})");
        QVERIFY(h->startDaemon());
        MicroSet::Client client(h->client());
        MicroSet::Error error;
        QCOMPARE(client.stored(u"audio"_s, &error), (QVariantMap{{u"audio.output"_s, u"bt"_s}}));
        QVERIFY(error.ok());
        std::optional<std::tuple<QStringList, QMap<QString, QString>, MicroSet::Error>> result;
        client.import({{u"audio.output"_s, u"usb"_s}, {u"audio.bt_codec"_s, u"aptx"_s}, {u"nope.key"_s, 1u}},
                      [&](const QStringList &taken, const QMap<QString, QString> &refused, const MicroSet::Error &e) {
                          result = {taken, refused, e};
                      });
        QTRY_VERIFY(result.has_value());
        QVERIFY(std::get<2>(*result).ok());
        QCOMPARE(std::get<0>(*result), QStringList{u"audio.bt_codec_headphones"_s});
        QCOMPARE(std::get<1>(*result), (QMap<QString, QString>{{u"nope.key"_s, u"no key"_s}}));
        QCOMPARE(client.stored(u"audio"_s),
                 (QVariantMap{{u"audio.output"_s, u"bt"_s}, {u"audio.bt_codec_headphones"_s, u"aptx"_s}}));
    }

    void overrideAgainAfterRestart()
    {
        QVERIFY(h->startDaemon());
        MicroSet::Client client(h->client());
        client.watch(u"rgb.enabled"_s);
        std::optional<MicroSet::Error> result;
        client.override(u"rgb.enabled"_s, true, [&](const MicroSet::Error &e) { result = e; });
        QTRY_VERIFY(result.has_value());
        QVERIFY(result->ok());
        QCOMPARE(client.get(u"rgb.enabled"_s), QVariant(true));
        QSignalSpy spy(&client, &MicroSet::Client::changed);
        h->stopDaemon();
        QVERIFY(h->startDaemon());
        QTRY_COMPARE(client.get(u"rgb.enabled"_s), QVariant(true));
        QTRY_VERIFY(!spy.isEmpty());
        QCOMPARE(spy.last().at(1), QVariant(true));
        QVERIFY(client.stored(u"rgb"_s).isEmpty());
        QVERIFY(h->daemonLog().contains(u"override rgb.enabled by %1"_s.arg(h->client().baseService())));

        result.reset();
        client.override(u"audio.output"_s, u"usb"_s, [&](const MicroSet::Error &e) { result = e; });
        QTRY_VERIFY(result.has_value());
        QCOMPARE(result->name, u"org.freedesktop.DBus.Error.InvalidArgs"_s);

        result.reset();
        client.clearOverride(u"rgb.enabled"_s, [&](const MicroSet::Error &e) { result = e; });
        QTRY_VERIFY(result.has_value());
        QVERIFY(result->ok());
        QCOMPARE(client.get(u"rgb.enabled"_s), QVariant(false));
        h->stopDaemon();
        QVERIFY(h->startDaemon());
        QTest::qWait(300);
        QCOMPARE(client.get(u"rgb.enabled"_s), QVariant(false));
        QCOMPARE(client.get(u"audio.output"_s), QVariant(u"auto"_s));
        QVERIFY(!h->daemonLog().contains("override audio.output"_L1));
    }

    void overrideInFlightSentAgain()
    {
        auto first = std::make_unique<FakeMicroSet>(h->address(), u"fake-microset-4"_s);
        MicroSet::Client client(h->client());
        std::optional<MicroSet::Error> result;
        client.override(u"rgb.enabled"_s, true, [&](const MicroSet::Error &e) { result = e; });
        QTRY_COMPARE(first->overrides, QStringList{u"rgb.enabled"_s});
        first.reset();
        QTRY_VERIFY(result.has_value());
        QVERIFY(!result->ok());
        auto second = std::make_unique<FakeMicroSet>(h->address(), u"fake-microset-5"_s);
        QTRY_COMPARE(second->overrides, QStringList{u"rgb.enabled"_s});

        second->refuseOverride = true;
        result.reset();
        client.override(u"rgb.hue"_s, 255u, [&](const MicroSet::Error &e) { result = e; });
        QTRY_VERIFY(result.has_value());
        QCOMPARE(result->name, u"org.freedesktop.DBus.Error.InvalidArgs"_s);
        second.reset();
        auto third = std::make_unique<FakeMicroSet>(h->address(), u"fake-microset-6"_s);
        QTRY_COMPARE(third->overrides, QStringList{u"rgb.enabled"_s});
        QTest::qWait(300);
        QCOMPARE(third->overrides, QStringList{u"rgb.enabled"_s});
    }

    void resubscribeOnOwnerChange()
    {
        auto first = std::make_unique<FakeMicroSet>(h->address(), u"fake-microset-1"_s);
        MicroSet::Client client(h->client());
        QSignalSpy spy(&client, &MicroSet::Client::changed);
        client.subscribe(u"display.rotation"_s);
        client.subscribe(u"display.rotation"_s);
        QTRY_COMPARE(first->subscribed, QStringList{u"display.rotation"_s});
        QTest::qWait(200);
        QCOMPARE(first->subscribed.size(), 1);
        first.reset();
        auto second = std::make_unique<FakeMicroSet>(h->address(), u"fake-microset-2"_s);
        QTRY_COMPARE(second->subscribed, QStringList{u"display.rotation"_s});
        QTRY_COMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), u"display.rotation"_s);
        QCOMPARE(spy.at(0).at(1), QVariant(u"value of display.rotation"_s));
        client.unsubscribe(u"display.rotation"_s);
        second.reset();
        auto third = std::make_unique<FakeMicroSet>(h->address(), u"fake-microset-3"_s);
        QTest::qWait(300);
        QVERIFY(third->subscribed.isEmpty());
    }
};

QTEST_GUILESS_MAIN(ClientTest)
#include "tst_client.moc"
