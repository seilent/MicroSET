#include "auth.h"
#include "backlightapplier.h"
#include "fakelogind.h"
#include "harness.h"
#include "powerbridge.h"
#include "schema.h"
#include "service.h"
#include "settings.h"
#include "sleepinhibitor.h"
#include "sscapplier.h"
#include "ssclink.h"
#include "sscrotation.h"
#include "sscwire.h"
#include "store.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTest>

#include <algorithm>
#include <memory>
#include <optional>

using namespace Qt::StringLiterals;

namespace {

constexpr int retryMs = 50;
constexpr int holdMs = 300;
constexpr int shortMs = 100;
constexpr int waitMs = 5000;
constexpr quint32 fakeNode = 1;
constexpr quint32 serverNode = 9;
constexpr quint32 serverPort = 13;
constexpr quint32 otherPort = 14;
constexpr ssc::Suid orientSuid{0x1122334455667788ull, 0x99aabbccddeeff00ull};
constexpr ssc::Suid otherSuid{0x0102030405060708ull, 0x1112131415161718ull};

const QString Rotation = u"display.rotation"_s;
const QString Orientation = u"display.orientation"_s;
const QString AutoRotate = u"display.auto_rotate"_s;

struct Datagram
{
    quint32 node = 0;
    quint32 port = 0;
    QByteArray bytes;
};

struct Sent
{
    quint16 txn = 0;
    quint32 msgId = 0;
    ssc::Suid suid;
    QByteArray body;
};

class FakeLink : public SscLink
{
public:
    FakeLink(QList<Datagram> *sent, bool open)
        : m_sent(sent)
        , m_open(open)
    {
    }

    bool open() const override
    {
        return m_open;
    }

    quint32 localNode() const override
    {
        return fakeNode;
    }

    bool send(quint32 node, quint32 port, const QByteArray &bytes) override
    {
        if (m_failSends > 0) {
            --m_failSends;
            return false;
        }
        m_sent->append(Datagram{node, port, bytes});
        return true;
    }

    bool reopen() override
    {
        ++m_reopens;
        m_open = !refuseReopen;
        return m_open;
    }

    void inject(quint32 node, quint32 port, const QByteArray &bytes)
    {
        emit received(node, port, bytes);
    }

    void fail()
    {
        m_open = false;
        emit failed();
    }

    void failSends(int count)
    {
        m_failSends = count;
    }

    int reopens() const
    {
        return m_reopens;
    }

    bool refuseReopen = false;

private:
    QList<Datagram> *m_sent = nullptr;
    bool m_open = true;
    int m_failSends = 0;
    int m_reopens = 0;
};

void putLe16(QByteArray &buf, quint16 value)
{
    buf.append(char(value & 0xff));
    buf.append(char(value >> 8));
}

void putLe32(QByteArray &buf, quint32 value)
{
    for (int i = 0; i < 4; ++i)
        buf.append(char((value >> (8 * i)) & 0xff));
}

QByteArray bytesOf(std::initializer_list<int> values)
{
    QByteArray buf;
    for (const int value : values)
        buf.append(char(value));
    return buf;
}

QByteArray controlPacket(quint32 cmd, quint32 service, quint32 node, quint32 port)
{
    QByteArray buf;
    putLe32(buf, cmd);
    putLe32(buf, service);
    putLe32(buf, service ? 1 : 0);
    putLe32(buf, node);
    putLe32(buf, port);
    return buf;
}

QByteArray qmi(quint8 type, quint16 txn, quint16 msgId, const QByteArray &tlvs)
{
    QByteArray buf;
    buf.append(char(type));
    putLe16(buf, txn);
    putLe16(buf, msgId);
    putLe16(buf, quint16(tlvs.size()));
    buf.append(tlvs);
    return buf;
}

QByteArray response(quint16 txn, quint16 result = 0, quint16 error = 0)
{
    QByteArray tlvs;
    tlvs.append(char(2));
    putLe16(tlvs, 4);
    putLe16(tlvs, result);
    putLe16(tlvs, error);
    return qmi(ssc::typeResponse, txn, ssc::msgControl, tlvs);
}

QByteArray event(quint32 msgId, const QByteArray &payload)
{
    QByteArray buf;
    ssc::putFixed32(buf, 1, msgId);
    ssc::putBytes(buf, 3, payload);
    return buf;
}

QByteArray indication(const ssc::Suid &suid, const QList<QByteArray> &events)
{
    QByteArray report;
    ssc::putBytes(report, 1, ssc::suidBytes(suid));
    for (const QByteArray &entry : events)
        ssc::putBytes(report, 2, entry);
    QByteArray data;
    putLe16(data, quint16(report.size()));
    data.append(report);
    QByteArray tlvs;
    tlvs.append(char(2));
    putLe16(tlvs, quint16(data.size()));
    tlvs.append(data);
    return qmi(ssc::typeIndication, 0, ssc::msgReportLarge, tlvs);
}

QByteArray suidAnswer(const std::optional<ssc::Suid> &suid)
{
    QByteArray payload;
    ssc::putBytes(payload, 1, QByteArrayLiteral("device_orient"));
    if (suid)
        ssc::putBytes(payload, 2, ssc::suidBytes(*suid));
    return indication(ssc::lookupSuid, {event(ssc::msgSuidEvent, payload)});
}

QByteArray orientEvent(quint64 value)
{
    QByteArray payload;
    ssc::putUint(payload, 1, value);
    return event(ssc::msgOrientEvent, payload);
}

QByteArray orient(quint64 value, const ssc::Suid &suid = orientSuid)
{
    return indication(suid, {orientEvent(value)});
}

std::optional<Sent> decode(const Datagram &datagram)
{
    const std::optional<ssc::Message> message = ssc::parse(datagram.bytes);
    if (!message || message->type != ssc::typeRequest || message->msgId != ssc::msgControl)
        return std::nullopt;
    const std::optional<QByteArrayView> tlv = message->tlv(1);
    if (!tlv || tlv->size() < 2)
        return std::nullopt;
    Sent sent;
    sent.txn = message->txn;
    for (const ssc::Field &field : ssc::fields(tlv->sliced(2))) {
        if (field.id == 1 && field.type == ssc::WireType::Bytes)
            sent.suid = ssc::parseSuid(field.bytes).value_or(ssc::Suid{});
        else if (field.id == 2 && field.type == ssc::WireType::Fixed32)
            sent.msgId = quint32(field.value);
        else if (field.id == 4 && field.type == ssc::WireType::Bytes)
            sent.body = field.bytes.toByteArray();
    }
    return sent;
}

}

class SscTest : public QObject
{
    Q_OBJECT

private:
    struct Daemon
    {
        Harness h;
        Schema schema;
        Store store{h.path(u"state"_s)};
        Settings settings{schema, store};
        std::unique_ptr<PowerBridge> bridge;
        std::unique_ptr<Auth> auth;
        std::unique_ptr<BacklightApplier> backlight;
        std::unique_ptr<Service> service;
        std::unique_ptr<SscApplier> applier;

        Daemon()
        {
            for (const QString &name : {u"power"_s, u"alsa"_s, u"ssc"_s, u"rgb"_s, u"backlight"_s})
                schema.addApplier(name);
            schema.addProvider(u"sessions"_s, [] { return QStringList{u"steam"_s}; });
            schema.load(QStringLiteral(MICROSET_SOURCE_DIR) + u"/schema"_s);
            if (!h.startBus())
                qFatal("no test bus");
            store.load();
        }

        ~Daemon()
        {
            applier.reset();
            service.reset();
            backlight.reset();
            QDBusConnection::disconnectFromBus(u"microsetd"_s);
        }

        void start(SscRotation &rotation)
        {
            QDBusConnection bus = QDBusConnection::connectToBus(h.address(), u"microsetd"_s);
            bridge = std::make_unique<PowerBridge>(bus, settings);
            auth = std::make_unique<Auth>(bus, h.path(u"proc"_s));
            backlight = std::make_unique<BacklightApplier>(settings, h.path(u"backlight"_s), h.path(u"run"_s));
            service = std::make_unique<Service>(bus, settings, *auth, *bridge, *backlight);
            service->addLive(Rotation);
            applier = std::make_unique<SscApplier>(settings, rotation);
            QObject::connect(service.get(), &Service::subscribersChanged, applier.get(),
                             [this](const QString &key, bool held) {
                                 if (key == Rotation)
                                     applier->setSubscribed(held);
                             });
            applier->start();
            if (!bus.registerObject(u"/net/seilent/MicroSet"_s, service.get(), QDBusConnection::ExportScriptableContents)
                || !bus.registerService(u"net.seilent.MicroSet"_s))
                qFatal("cannot export the service");
        }

        QDBusConnection caller(const QString &name) const
        {
            return QDBusConnection::connectToBus(h.address(), name);
        }

        static QString call(const QDBusConnection &bus, const QString &method, const QString &key)
        {
            QDBusMessage message = QDBusMessage::createMethodCall(u"net.seilent.MicroSet"_s, u"/net/seilent/MicroSet"_s,
                                                                  u"net.seilent.MicroSet1"_s, method);
            message << key;
            const QDBusMessage reply = bus.call(message, QDBus::BlockWithGui, waitMs);
            return reply.type() == QDBusMessage::ErrorMessage ? reply.errorName() : QString();
        }

        QString rotation() const
        {
            return settings.value(Rotation).toString();
        }
    };

    QList<Datagram> m_sent;
    FakeLink *m_link = nullptr;
    std::unique_ptr<Harness> m_bus;
    std::unique_ptr<FakeLogind> m_logind;
    std::unique_ptr<SleepInhibitor> m_sleep;
    std::unique_ptr<SscRotation> m_rotation;

    void make(bool open = true)
    {
        m_sent.clear();
        m_link = new FakeLink(&m_sent, open);
        m_rotation = std::make_unique<SscRotation>(m_link, m_sleep.get(), retryMs, holdMs);
    }

    void makeSleeping()
    {
        m_bus = std::make_unique<Harness>();
        QVERIFY(m_bus->startBus());
        m_logind = std::make_unique<FakeLogind>(m_bus->address());
        QVERIFY(m_logind->start());
        m_sleep = std::make_unique<SleepInhibitor>(m_bus->client(), u"stop the rotation sensor"_s);
        QVERIFY(m_sleep->subscribed());
        m_sleep->take();
        QTRY_VERIFY(m_sleep->held());
    }

    void sleep(bool start)
    {
        m_logind->prepareForSleep(start);
    }

    void serve(quint32 port = serverPort)
    {
        m_link->inject(fakeNode, ssc::controlPort, controlPacket(ssc::newServer, ssc::service, serverNode, port));
    }

    void answer(const std::optional<ssc::Suid> &suid, quint32 port = serverPort)
    {
        inject(suidAnswer(suid), port);
    }

    void inject(const QByteArray &bytes, quint32 port = serverPort)
    {
        m_link->inject(serverNode, port, bytes);
    }

    void ready()
    {
        if (!m_rotation)
            make();
        m_rotation->setWanted(true);
        serve();
        answer(orientSuid);
        QVERIFY(m_rotation->supported());
    }

    void claim()
    {
        ready();
        QCOMPARE(count(ssc::msgOnChange), 1);
        inject(response(lastTxn(ssc::msgOnChange)));
        QVERIFY(m_rotation->claimed());
    }

    void claimSettled(quint64 value)
    {
        claim();
        inject(orient(value));
        QTRY_COMPARE_WITH_TIMEOUT(m_rotation->turn(), int(value), waitMs);
    }

    QList<Sent> sentOf(quint32 msgId) const
    {
        QList<Sent> out;
        for (const Datagram &datagram : m_sent) {
            const std::optional<Sent> sent = decode(datagram);
            if (sent && sent->msgId == msgId)
                out.append(*sent);
        }
        return out;
    }

    int count(quint32 msgId) const
    {
        return int(sentOf(msgId).size());
    }

    quint16 lastTxn(quint32 msgId) const
    {
        const QList<Sent> sent = sentOf(msgId);
        return sent.isEmpty() ? 0 : sent.constLast().txn;
    }

    int lookups() const
    {
        return int(std::count_if(m_sent.cbegin(), m_sent.cend(), [](const Datagram &datagram) {
            return datagram.port == ssc::controlPort && datagram.bytes == ssc::lookup(ssc::service);
        }));
    }

private slots:
    void cleanup()
    {
        m_rotation.reset();
        m_link = nullptr;
        m_sleep.reset();
        m_logind.reset();
        m_bus.reset();
        m_sent.clear();
    }

    void wireRoundTrip()
    {
        QByteArray buf;
        ssc::putFixed64(buf, 1, 0xABABABABABABABABull);
        ssc::putFixed32(buf, 2, 513);
        ssc::putUint(buf, 3, 300);
        ssc::putBytes(buf, 4, QByteArrayLiteral("accel"));
        const QList<ssc::Field> fields = ssc::fields(buf);
        QCOMPARE(fields.size(), 4);
        QCOMPARE(fields.at(0).id, 1u);
        QVERIFY(fields.at(0).type == ssc::WireType::Fixed64);
        QCOMPARE(fields.at(0).value, 0xABABABABABABABABull);
        QCOMPARE(fields.at(1).id, 2u);
        QVERIFY(fields.at(1).type == ssc::WireType::Fixed32);
        QCOMPARE(fields.at(1).value, 513ull);
        QCOMPARE(fields.at(2).id, 3u);
        QVERIFY(fields.at(2).type == ssc::WireType::Varint);
        QCOMPARE(fields.at(2).value, 300ull);
        QCOMPARE(fields.at(3).id, 4u);
        QVERIFY(fields.at(3).type == ssc::WireType::Bytes);
        QCOMPARE(fields.at(3).bytes.toByteArray(), QByteArrayLiteral("accel"));

        QByteArray varint;
        ssc::putVarint(varint, 300);
        QCOMPARE(varint, bytesOf({0xac, 0x02}));
        QCOMPARE(ssc::uintField(buf, 3).value_or(0), 300ull);
        QVERIFY(!ssc::uintField(buf, 1));
    }

    void wireTruncated()
    {
        QCOMPARE(ssc::fields(bytesOf({0x0a, 0x05, 0x01})).size(), 0);
        QCOMPARE(ssc::fields(bytesOf({0x09, 0x01})).size(), 0);
        QCOMPARE(ssc::fields(bytesOf({0x08, 0x01, 0x09, 0x01})).size(), 1);
        QCOMPARE(ssc::fields(bytesOf({0x08, 0x01, 0x0b, 0x01})).size(), 1);
    }

    void wireControl()
    {
        const QByteArray buf = ssc::control(7, bytesOf({0xaa, 0xbb}));
        QCOMPARE(buf, bytesOf({0, 7, 0, 0x20, 0, 11, 0, 0x01, 4, 0, 2, 0, 0xaa, 0xbb, 0x10, 1, 0, 1}));
        const std::optional<ssc::Message> message = ssc::parse(buf);
        QVERIFY(message);
        QCOMPARE(int(message->type), int(ssc::typeRequest));
        QCOMPARE(message->txn, quint16(7));
        QCOMPARE(message->msgId, ssc::msgControl);
        QCOMPARE(message->tlv(0x10).value_or(QByteArrayView()).toByteArray(), bytesOf({1}));
    }

    void wireReport()
    {
        const QByteArray buf = bytesOf({4, 5, 0, 0x22, 0, 16, 0, 0x01, 8, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0x02, 2, 0, 0, 0});
        const std::optional<ssc::Message> message = ssc::parse(buf);
        QVERIFY(message);
        QCOMPARE(int(message->type), int(ssc::typeIndication));
        QCOMPARE(message->txn, quint16(5));
        QCOMPARE(message->msgId, ssc::msgReportLarge);
        const std::optional<QByteArrayView> data = message->data();
        QVERIFY(data);
        QCOMPARE(data->size(), 0);

        const QByteArray report = indication(orientSuid, {orientEvent(1), orientEvent(3)});
        const std::optional<ssc::Message> parsed = ssc::parse(report);
        QVERIFY(parsed);
        const std::optional<ssc::Report> events = ssc::parseReport(parsed->data().value_or(QByteArrayView()));
        QVERIFY(events);
        QVERIFY(events->suid == orientSuid);
        QCOMPARE(events->events.size(), 2);
        QCOMPARE(events->events.at(0).msgId, ssc::msgOrientEvent);
        QCOMPARE(ssc::uintField(events->events.at(0).payload, 1).value_or(9), 1ull);
        QCOMPARE(ssc::uintField(events->events.at(1).payload, 1).value_or(9), 3ull);
    }

    void wireShort()
    {
        QVERIFY(!ssc::parse(bytesOf({4, 5, 0, 0x22, 0, 16, 0, 0x01, 8, 0})));
        QVERIFY(!ssc::parse(bytesOf({4, 5, 0, 0x22})));
        const QByteArray packet = controlPacket(ssc::newServer, ssc::service, serverNode, serverPort);
        QVERIFY(!ssc::parseControl(packet.first(19)));
        const std::optional<ssc::Control> control = ssc::parseControl(packet);
        QVERIFY(control);
        QCOMPARE(control->cmd, ssc::newServer);
        QCOMPARE(control->service, ssc::service);
        QCOMPARE(control->node, serverNode);
        QCOMPARE(control->port, serverPort);
    }

    void wireRequest()
    {
        const std::optional<Sent> suid = decode(Datagram{serverNode, serverPort,
                                                         ssc::control(1, ssc::suidRequest("device_orient"))});
        QVERIFY(suid);
        QCOMPARE(suid->msgId, ssc::msgSuidRequest);
        QVERIFY(suid->suid == ssc::lookupSuid);
        QByteArray payload;
        ssc::putBytes(payload, 1, QByteArrayLiteral("device_orient"));
        ssc::putUint(payload, 2, 0);
        ssc::putUint(payload, 3, 1);
        QByteArray body;
        ssc::putBytes(body, 2, payload);
        QCOMPARE(suid->body, body);

        const std::optional<Sent> enable = decode(Datagram{serverNode, serverPort,
                                                           ssc::control(2, ssc::request(orientSuid, ssc::msgOnChange,
                                                                                        QByteArray()))});
        QVERIFY(enable);
        QVERIFY(enable->suid == orientSuid);
        QCOMPARE(enable->body, bytesOf({0x12, 0x00}));

        const std::optional<Sent> disable = decode(Datagram{serverNode, serverPort,
                                                            ssc::control(3, ssc::request(orientSuid, ssc::msgDisable,
                                                                                         std::nullopt))});
        QVERIFY(disable);
        QCOMPARE(disable->msgId, ssc::msgDisable);
        QVERIFY(disable->body.isEmpty());
    }

    void names()
    {
        QCOMPARE(SscApplier::nameOf(0), u"portrait"_s);
        QCOMPARE(SscApplier::nameOf(1), u"landscape"_s);
        QCOMPARE(SscApplier::nameOf(2), u"portrait-flipped"_s);
        QCOMPARE(SscApplier::nameOf(3), u"landscape-flipped"_s);
        QCOMPARE(SscApplier::nameOf(-1), QString());
        QCOMPARE(SscApplier::nameOf(4), QString());
    }

    void noTrafficUntilWanted()
    {
        make();
        QTest::qWait(shortMs);
        QVERIFY(m_sent.isEmpty());
        m_rotation->setWanted(true);
        QCOMPARE(m_sent.size(), 1);
        QCOMPARE(m_sent.at(0).node, fakeNode);
        QCOMPARE(m_sent.at(0).port, ssc::controlPort);
        QCOMPARE(m_sent.at(0).bytes, ssc::lookup(ssc::service));
        const std::optional<ssc::Control> control = ssc::parseControl(m_sent.at(0).bytes);
        QVERIFY(control);
        QCOMPARE(control->cmd, ssc::newLookup);
        QCOMPARE(control->service, ssc::service);
        QVERIFY(!m_rotation->supported());
        QCOMPARE(m_rotation->turn(), -1);
        m_rotation->setWanted(false);
        m_rotation->setWanted(true);
        QCOMPARE(lookups(), 1);
    }

    void closedLink()
    {
        make(false);
        m_link->refuseReopen = true;
        QElapsedTimer timer;
        timer.start();
        m_rotation->setWanted(true);
        QCOMPARE(m_link->reopens(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(m_link->reopens(), 2, waitMs);
        QVERIFY(timer.elapsed() >= retryMs * 9 / 10);
        QVERIFY(m_sent.isEmpty());
        QVERIFY(!m_rotation->supported());
        m_link->refuseReopen = false;
        QTRY_COMPARE_WITH_TIMEOUT(m_link->reopens(), 3, waitMs);
        QVERIFY(timer.elapsed() >= 3 * retryMs * 9 / 10);
        QCOMPARE(lookups(), 1);
        m_rotation->setWanted(false);
        m_rotation->setWanted(true);
        QTest::qWait(4 * retryMs);
        QCOMPARE(m_link->reopens(), 3);
        QCOMPARE(lookups(), 1);
    }

    void closedLinkStopsWhenUnwanted()
    {
        make(false);
        m_link->refuseReopen = true;
        m_rotation->setWanted(true);
        QCOMPARE(m_link->reopens(), 1);
        m_rotation->setWanted(false);
        QTest::qWait(4 * retryMs);
        QCOMPARE(m_link->reopens(), 1);
        m_link->refuseReopen = false;
        m_rotation->setWanted(true);
        QCOMPARE(m_link->reopens(), 2);
        QCOMPARE(lookups(), 1);
    }

    void otherServiceIgnored()
    {
        make();
        m_rotation->setWanted(true);
        m_link->inject(fakeNode, ssc::controlPort, controlPacket(ssc::newServer, 401, serverNode, otherPort));
        QCOMPARE(m_sent.size(), 1);
        serve();
        QCOMPARE(m_sent.size(), 2);
        QCOMPARE(count(ssc::msgSuidRequest), 1);
        QCOMPARE(m_sent.constLast().node, serverNode);
        QCOMPARE(m_sent.constLast().port, serverPort);
        serve(otherPort);
        QCOMPARE(m_sent.size(), 2);
    }

    void suidEnables()
    {
        make();
        m_rotation->setWanted(true);
        serve();
        QSignalSpy spy(m_rotation.get(), &SscRotation::supportedChanged);
        answer(orientSuid);
        QVERIFY(m_rotation->supported());
        QCOMPARE(spy.count(), 1);
        const QList<Sent> enables = sentOf(ssc::msgOnChange);
        QCOMPARE(enables.size(), 1);
        QVERIFY(enables.at(0).suid == orientSuid);
        QCOMPARE(m_sent.constLast().port, serverPort);
    }

    void suidAfterReleaseDoesNotEnable()
    {
        make();
        m_rotation->setWanted(true);
        serve();
        m_rotation->setWanted(false);
        answer(orientSuid);
        QVERIFY(m_rotation->supported());
        QCOMPARE(count(ssc::msgOnChange), 0);
        m_rotation->setWanted(true);
        QCOMPARE(count(ssc::msgOnChange), 1);
    }

    void secondSuidIgnored()
    {
        ready();
        QCOMPARE(count(ssc::msgOnChange), 1);
        answer(otherSuid);
        QCOMPARE(count(ssc::msgOnChange), 1);
        inject(response(lastTxn(ssc::msgOnChange)));
        QVERIFY(m_rotation->claimed());
        inject(orient(1, otherSuid));
        QCOMPARE(m_rotation->raw(), -1);
        inject(orient(1));
        QCOMPARE(m_rotation->raw(), 1);
    }

    void noSuidRequestUntilWanted()
    {
        make();
        m_rotation->setWanted(true);
        m_rotation->setWanted(false);
        serve();
        QTest::qWait(4 * retryMs);
        QCOMPARE(count(ssc::msgSuidRequest), 0);
        m_rotation->setWanted(true);
        QCOMPARE(count(ssc::msgSuidRequest), 1);
        m_rotation->setWanted(false);
        m_rotation->setWanted(true);
        QCOMPARE(count(ssc::msgSuidRequest), 1);
        answer(orientSuid);
        QCOMPARE(count(ssc::msgOnChange), 1);
    }

    void failedSuidResponseRetries()
    {
        make();
        m_rotation->setWanted(true);
        serve();
        QCOMPARE(count(ssc::msgSuidRequest), 1);
        QTest::ignoreMessage(QtWarningMsg, "ssc: request failed 3");
        QElapsedTimer timer;
        timer.start();
        inject(response(lastTxn(ssc::msgSuidRequest), 1, 3));
        QTRY_COMPARE_WITH_TIMEOUT(count(ssc::msgSuidRequest), 2, waitMs);
        QVERIFY(timer.elapsed() >= retryMs * 9 / 10);
        answer(orientSuid);
        QVERIFY(m_rotation->supported());
        QCOMPARE(count(ssc::msgOnChange), 1);
    }

    void emptySuidRetries()
    {
        make();
        m_rotation->setWanted(true);
        serve();
        QCOMPARE(count(ssc::msgSuidRequest), 1);
        QElapsedTimer timer;
        timer.start();
        answer(std::nullopt);
        QVERIFY(!m_rotation->supported());
        QTRY_COMPARE_WITH_TIMEOUT(count(ssc::msgSuidRequest), 2, waitMs);
        QVERIFY(timer.elapsed() >= retryMs * 9 / 10);
        timer.restart();
        answer(std::nullopt);
        QTRY_COMPARE_WITH_TIMEOUT(count(ssc::msgSuidRequest), 3, waitMs);
        QVERIFY(timer.elapsed() >= 2 * retryMs * 9 / 10);
        QVERIFY(!m_rotation->supported());
        answer(orientSuid);
        QVERIFY(m_rotation->supported());
        QTest::qWait(4 * retryMs);
        QCOMPARE(count(ssc::msgSuidRequest), 3);
    }

    void endMarkerIgnored()
    {
        ready();
        const int sent = int(m_sent.size());
        QSignalSpy spy(m_rotation.get(), &SscRotation::supportedChanged);
        m_link->inject(fakeNode, ssc::controlPort, controlPacket(ssc::newServer, 0, 0, 0));
        m_link->inject(fakeNode, ssc::controlPort, controlPacket(ssc::delServer, 0, 0, 0));
        QCOMPARE(int(m_sent.size()), sent);
        QCOMPARE(spy.count(), 0);
        QVERIFY(m_rotation->supported());
    }

    void enableFlushes()
    {
        ready();
        QCOMPARE(count(ssc::msgOnChange), 1);
        QVERIFY(!m_rotation->claimed());
        QCOMPARE(m_sent.constLast().node, serverNode);
        QCOMPARE(m_sent.constLast().port, serverPort);
        inject(response(lastTxn(ssc::msgOnChange)));
        QVERIFY(m_rotation->claimed());
        QCOMPARE(count(ssc::msgFlush), 1);
        const int sent = int(m_sent.size());
        inject(response(lastTxn(ssc::msgFlush)));
        QCOMPARE(int(m_sent.size()), sent);
        QVERIFY(m_rotation->claimed());
    }

    void staleTxnDoesNotClaim()
    {
        ready();
        const quint16 first = lastTxn(ssc::msgOnChange);
        m_rotation->setWanted(false);
        QCOMPARE(count(ssc::msgDisable), 1);
        const quint16 disable = lastTxn(ssc::msgDisable);
        m_rotation->setWanted(true);
        QCOMPARE(count(ssc::msgOnChange), 2);
        const quint16 second = lastTxn(ssc::msgOnChange);
        QVERIFY(second != first);
        inject(response(disable));
        QVERIFY(!m_rotation->claimed());
        inject(response(first));
        QVERIFY(!m_rotation->claimed());
        QCOMPARE(count(ssc::msgFlush), 0);
        inject(response(second));
        QVERIFY(m_rotation->claimed());
        QCOMPARE(count(ssc::msgFlush), 1);
    }

    void lateEnableAfterRelease()
    {
        ready();
        const quint16 enable = lastTxn(ssc::msgOnChange);
        m_rotation->setWanted(false);
        QCOMPARE(count(ssc::msgDisable), 1);
        inject(response(enable));
        QVERIFY(!m_rotation->claimed());
        QCOMPARE(count(ssc::msgFlush), 0);
        QCOMPARE(m_rotation->turn(), -1);
    }

    void suidRetryResponseDoesNotClaim()
    {
        make();
        m_rotation->setWanted(true);
        serve();
        const quint16 first = lastTxn(ssc::msgSuidRequest);
        answer(std::nullopt);
        QTRY_COMPARE_WITH_TIMEOUT(count(ssc::msgSuidRequest), 2, waitMs);
        const quint16 retry = lastTxn(ssc::msgSuidRequest);
        answer(orientSuid);
        const quint16 enable = lastTxn(ssc::msgOnChange);
        QVERIFY(enable != 0);
        inject(response(retry));
        QVERIFY(!m_rotation->claimed());
        inject(response(first));
        QVERIFY(!m_rotation->claimed());
        QCOMPARE(count(ssc::msgFlush), 0);
        inject(response(enable));
        QVERIFY(m_rotation->claimed());
    }

    void valueBeforeEnableDropped()
    {
        ready();
        inject(orient(1));
        QCOMPARE(m_rotation->raw(), -1);
        QTest::qWait(shortMs);
        QCOMPARE(m_rotation->turn(), -1);
        inject(response(lastTxn(ssc::msgOnChange)));
        QVERIFY(m_rotation->claimed());
        QCOMPARE(m_rotation->turn(), -1);
        QCOMPARE(count(ssc::msgFlush), 1);
        QElapsedTimer timer;
        timer.start();
        inject(orient(1));
        QCOMPARE(m_rotation->raw(), 1);
        QTest::qWait(shortMs);
        QCOMPARE(m_rotation->turn(), -1);
        QTRY_COMPARE_WITH_TIMEOUT(m_rotation->turn(), 1, waitMs);
        QVERIFY(timer.elapsed() >= SscRotation::settleMs * 9 / 10);
    }

    void twoEventsThenUnknown()
    {
        claim();
        QList<int> raws;
        inject(indication(orientSuid, {orientEvent(1)}));
        raws << m_rotation->raw();
        inject(indication(orientSuid, {orientEvent(3), orientEvent(2)}));
        raws << m_rotation->raw();
        QCOMPARE(raws, (QList<int>{1, 2}));
        QTest::qWait(shortMs);
        inject(orient(7));
        QCOMPARE(m_rotation->raw(), 2);
        QTRY_COMPARE_WITH_TIMEOUT(m_rotation->turn(), 2, waitMs);
    }

    void shortValueReverted()
    {
        claimSettled(0);
        QSignalSpy turns(m_rotation.get(), &SscRotation::turnChanged);
        inject(orient(1));
        QTest::qWait(shortMs);
        inject(orient(0));
        QTest::qWait(SscRotation::settleMs);
        QCOMPARE(m_rotation->turn(), 0);
        QCOMPARE(turns.count(), 0);
    }

    void otherSuidOrPortDropped()
    {
        claim();
        inject(orient(1, otherSuid));
        QCOMPARE(m_rotation->raw(), -1);
        inject(orient(1), otherPort);
        QCOMPARE(m_rotation->raw(), -1);
        m_link->inject(serverNode + 1, serverPort, orient(1));
        QCOMPARE(m_rotation->raw(), -1);
        QCOMPARE(m_rotation->turn(), -1);
    }

    void releaseClearsTurn()
    {
        claimSettled(1);
        m_rotation->setWanted(false);
        QCOMPARE(count(ssc::msgDisable), 1);
        QVERIFY(sentOf(ssc::msgDisable).at(0).suid == orientSuid);
        QVERIFY(!m_rotation->claimed());
        QCOMPARE(m_rotation->turn(), -1);
        QVERIFY(m_rotation->supported());
        inject(orient(2));
        QCOMPARE(m_rotation->raw(), 1);
        m_rotation->setWanted(true);
        QCOMPARE(count(ssc::msgOnChange), 2);
        inject(response(lastTxn(ssc::msgOnChange)));
        QVERIFY(m_rotation->claimed());
        QCOMPARE(m_rotation->turn(), -1);
    }

    void delServerHoldsThenLoses()
    {
        claimSettled(1);
        QSignalSpy supported(m_rotation.get(), &SscRotation::supportedChanged);
        m_link->inject(fakeNode, ssc::controlPort, controlPacket(ssc::delServer, ssc::service, serverNode, serverPort));
        QVERIFY(m_rotation->supported());
        QVERIFY(!m_rotation->claimed());
        QCOMPARE(m_rotation->turn(), 1);
        inject(orient(2));
        QCOMPARE(m_rotation->raw(), 1);
        serve(otherPort);
        QCOMPARE(m_sent.constLast().port, otherPort);
        QCOMPARE(count(ssc::msgSuidRequest), 2);
        answer(std::nullopt, otherPort);
        QTRY_COMPARE_WITH_TIMEOUT(count(ssc::msgSuidRequest), 3, waitMs);
        answer(orientSuid, otherPort);
        QCOMPARE(count(ssc::msgOnChange), 2);
        QCOMPARE(m_sent.constLast().port, otherPort);
        m_link->inject(serverNode, otherPort, response(lastTxn(ssc::msgOnChange)));
        QVERIFY(m_rotation->claimed());
        QCOMPARE(m_rotation->turn(), 1);
        QTest::qWait(holdMs);
        QVERIFY(m_rotation->supported());
        QCOMPARE(m_rotation->turn(), 1);
        QCOMPARE(supported.count(), 0);

        QElapsedTimer timer;
        timer.start();
        m_link->inject(fakeNode, ssc::controlPort, controlPacket(ssc::delServer, ssc::service, serverNode, otherPort));
        QVERIFY(m_rotation->supported());
        QCOMPARE(m_rotation->turn(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(!m_rotation->supported(), waitMs);
        QVERIFY(timer.elapsed() >= holdMs * 9 / 10);
        QCOMPARE(m_rotation->turn(), -1);
    }

    void delServerNotWanted()
    {
        ready();
        m_rotation->setWanted(false);
        m_link->inject(fakeNode, ssc::controlPort, controlPacket(ssc::delServer, ssc::service, serverNode, serverPort));
        QVERIFY(!m_rotation->supported());
        QCOMPARE(m_rotation->turn(), -1);
    }

    void enableFailure()
    {
        ready();
        QTest::ignoreMessage(QtWarningMsg, "ssc: enable failed 31");
        QElapsedTimer timer;
        timer.start();
        inject(response(lastTxn(ssc::msgOnChange), 1, 31));
        QVERIFY(!m_rotation->claimed());
        QCOMPARE(count(ssc::msgFlush), 0);
        inject(orient(1));
        QCOMPARE(m_rotation->raw(), -1);
        QCOMPARE(m_rotation->turn(), -1);
        QTRY_COMPARE_WITH_TIMEOUT(count(ssc::msgOnChange), 2, waitMs);
        QVERIFY(timer.elapsed() >= retryMs * 9 / 10);
        QTest::ignoreMessage(QtWarningMsg, "ssc: enable failed 31");
        timer.restart();
        inject(response(lastTxn(ssc::msgOnChange), 1, 31));
        QTRY_COMPARE_WITH_TIMEOUT(count(ssc::msgOnChange), 3, waitMs);
        QVERIFY(timer.elapsed() >= 2 * retryMs * 9 / 10);
        inject(response(lastTxn(ssc::msgOnChange)));
        QVERIFY(m_rotation->claimed());
        QCOMPARE(count(ssc::msgFlush), 1);
    }

    void enableFailureAfterWake()
    {
        Daemon d;
        m_logind = std::make_unique<FakeLogind>(d.h.address());
        QVERIFY(m_logind->start());
        m_sleep = std::make_unique<SleepInhibitor>(d.h.client(), u"stop the rotation sensor"_s);
        m_sleep->take();
        QTRY_VERIFY(m_sleep->held());
        make();
        QCOMPARE(d.settings.store(Orientation, u"landscape"_s), Settings::Changed);
        d.start(*m_rotation);
        QCOMPARE(Daemon::call(d.h.client(), u"Subscribe"_s, Rotation), QString());
        serve();
        answer(orientSuid);
        inject(response(lastTxn(ssc::msgOnChange)));
        inject(orient(0));
        QTRY_COMPARE_WITH_TIMEOUT(d.rotation(), u"portrait"_s, waitMs);
        sleep(true);
        QTRY_COMPARE_WITH_TIMEOUT(count(ssc::msgDisable), 1, waitMs);
        inject(response(lastTxn(ssc::msgDisable)));
        sleep(false);
        QTRY_COMPARE_WITH_TIMEOUT(count(ssc::msgOnChange), 2, waitMs);
        QCOMPARE(d.rotation(), u"portrait"_s);
        QTest::ignoreMessage(QtWarningMsg, "ssc: enable failed 31");
        inject(response(lastTxn(ssc::msgOnChange), 1, 31));
        QCOMPARE(m_rotation->turn(), -1);
        QCOMPARE(d.rotation(), u"landscape"_s);
        QTRY_COMPARE_WITH_TIMEOUT(count(ssc::msgOnChange), 3, waitMs);
        inject(response(lastTxn(ssc::msgOnChange)));
        QVERIFY(m_rotation->claimed());
        inject(orient(2));
        QTRY_COMPARE_WITH_TIMEOUT(d.rotation(), u"portrait-flipped"_s, waitMs);
        d.applier.reset();
        m_rotation.reset();
        m_sleep.reset();
        m_logind.reset();
    }

    void linkFailureReopens()
    {
        claim();
        m_link->fail();
        QVERIFY(m_rotation->supported());
        QVERIFY(!m_rotation->claimed());
        const int sent = int(m_sent.size());
        QTRY_COMPARE_WITH_TIMEOUT(m_link->reopens(), 1, waitMs);
        QCOMPARE(int(m_sent.size()), sent + 1);
        QCOMPARE(m_sent.constLast().port, ssc::controlPort);
        QCOMPARE(m_sent.constLast().bytes, ssc::lookup(ssc::service));
        serve();
        answer(orientSuid);
        QCOMPARE(count(ssc::msgOnChange), 2);
        inject(response(lastTxn(ssc::msgOnChange)));
        QVERIFY(m_rotation->claimed());
    }

    void refusedReopenBacksOff()
    {
        claim();
        m_link->refuseReopen = true;
        QElapsedTimer timer;
        timer.start();
        m_link->fail();
        QTRY_COMPARE_WITH_TIMEOUT(m_link->reopens(), 1, waitMs);
        QTRY_COMPARE_WITH_TIMEOUT(m_link->reopens(), 2, waitMs);
        QVERIFY(timer.elapsed() >= 3 * retryMs * 9 / 10);
        QVERIFY(m_rotation->supported());
        m_link->refuseReopen = false;
        const int sent = int(m_sent.size());
        QTRY_COMPARE_WITH_TIMEOUT(m_link->reopens(), 3, waitMs);
        QVERIFY(timer.elapsed() >= 7 * retryMs * 9 / 10);
        QCOMPARE(int(m_sent.size()), sent + 1);
        QCOMPARE(m_sent.constLast().bytes, ssc::lookup(ssc::service));
        serve();
        answer(orientSuid);
        inject(response(lastTxn(ssc::msgOnChange)));
        QVERIFY(m_rotation->claimed());
        QTest::qWait(8 * retryMs);
        QCOMPARE(m_link->reopens(), 3);
    }

    void failedEnableRetries()
    {
        make();
        m_rotation->setWanted(true);
        serve();
        m_link->failSends(1);
        QElapsedTimer timer;
        timer.start();
        answer(orientSuid);
        QCOMPARE(count(ssc::msgOnChange), 0);
        QTRY_COMPARE_WITH_TIMEOUT(count(ssc::msgOnChange), 1, waitMs);
        QVERIFY(timer.elapsed() >= retryMs * 9 / 10);
        inject(response(lastTxn(ssc::msgOnChange)));
        QVERIFY(m_rotation->claimed());
    }

    void destructorWhileEnabling()
    {
        ready();
        QCOMPARE(count(ssc::msgDisable), 0);
        m_rotation.reset();
        QCOMPARE(count(ssc::msgDisable), 1);
        QVERIFY(decode(m_sent.constLast())->msgId == ssc::msgDisable);
    }

    void destructorWhileClaimed()
    {
        claim();
        m_rotation.reset();
        QCOMPARE(count(ssc::msgDisable), 1);
        QVERIFY(decode(m_sent.constLast())->msgId == ssc::msgDisable);
    }

    void sleepReleases()
    {
        makeSleeping();
        claim();
        QCOMPARE(m_logind->open(), 1);
        sleep(true);
        QTRY_COMPARE_WITH_TIMEOUT(count(ssc::msgDisable), 1, waitMs);
        QVERIFY(!m_rotation->claimed());
        QVERIFY(m_rotation->wanted());
        QVERIFY(m_sleep->held());
        QCOMPARE(m_logind->open(), 1);
        inject(response(lastTxn(ssc::msgDisable)));
        QVERIFY(!m_sleep->held());
        QTRY_COMPARE(m_logind->open(), 0);
        sleep(false);
        QTRY_COMPARE_WITH_TIMEOUT(count(ssc::msgOnChange), 2, waitMs);
        QTRY_VERIFY(m_sleep->held());
        QCOMPARE(m_sleep->takeCount(), 2);
        inject(response(lastTxn(ssc::msgOnChange)));
        QVERIFY(m_rotation->claimed());
        QCOMPARE(m_logind->inhibits.size(), 2);
        QCOMPARE(m_logind->inhibits.at(0), u"sleep|microset|stop the rotation sensor|delay"_s);
    }

    void sleepReleaseTimeout()
    {
        makeSleeping();
        claim();
        sleep(true);
        QTRY_COMPARE_WITH_TIMEOUT(count(ssc::msgDisable), 1, waitMs);
        QElapsedTimer timer;
        timer.start();
        QVERIFY(m_sleep->held());
        QTRY_VERIFY_WITH_TIMEOUT(!m_sleep->held(), waitMs);
        QVERIFY(timer.elapsed() >= SscRotation::sleepReleaseMs * 8 / 10);
    }

    void sleepIdleReleasesAtOnce()
    {
        makeSleeping();
        ready();
        m_rotation->setWanted(false);
        QVERIFY(m_sleep->held());
        sleep(true);
        QTRY_VERIFY_WITH_TIMEOUT(!m_sleep->held(), SscRotation::sleepReleaseMs / 2);
        QCOMPARE(count(ssc::msgDisable), 1);
    }

    void lateEnableAcrossSleep()
    {
        makeSleeping();
        ready();
        const quint16 enable = lastTxn(ssc::msgOnChange);
        sleep(true);
        QTRY_COMPARE_WITH_TIMEOUT(count(ssc::msgDisable), 1, waitMs);
        inject(response(enable));
        QVERIFY(!m_rotation->claimed());
        QCOMPARE(count(ssc::msgFlush), 0);
    }

    void suidDuringSleep()
    {
        makeSleeping();
        make();
        m_rotation->setWanted(true);
        serve();
        sleep(true);
        QTRY_VERIFY_WITH_TIMEOUT(!m_sleep->held(), waitMs);
        answer(orientSuid);
        QVERIFY(m_rotation->supported());
        QCOMPARE(count(ssc::msgOnChange), 0);
        sleep(false);
        QTRY_COMPARE_WITH_TIMEOUT(count(ssc::msgOnChange), 1, waitMs);
    }

    void turnKeptAcrossSleep()
    {
        makeSleeping();
        claimSettled(3);
        sleep(true);
        QTRY_COMPARE_WITH_TIMEOUT(count(ssc::msgDisable), 1, waitMs);
        QCOMPARE(m_rotation->turn(), 3);
        inject(response(lastTxn(ssc::msgDisable)));
        sleep(false);
        QTRY_COMPARE_WITH_TIMEOUT(count(ssc::msgOnChange), 2, waitMs);
        inject(response(lastTxn(ssc::msgOnChange)));
        QCOMPARE(m_rotation->turn(), 3);
    }

    void noStreamWithoutSubscriber()
    {
        Daemon d;
        make();
        d.start(*m_rotation);
        QCOMPARE(d.settings.store(Orientation, u"landscape"_s), Settings::Changed);
        QTest::qWait(shortMs);
        QVERIFY(m_sent.isEmpty());
        QVERIFY(!m_rotation->wanted());
        QCOMPARE(d.rotation(), u"landscape"_s);
    }

    void autoRotateGatesStream()
    {
        Daemon d;
        make();
        QCOMPARE(d.settings.store(AutoRotate, false), Settings::Changed);
        d.start(*m_rotation);
        QCOMPARE(Daemon::call(d.h.client(), u"Subscribe"_s, Rotation), QString());
        QVERIFY(d.service->held(Rotation));
        QTest::qWait(shortMs);
        QVERIFY(m_sent.isEmpty());
        QVERIFY(!m_rotation->wanted());
        QCOMPARE(d.settings.store(AutoRotate, true), Settings::Changed);
        QVERIFY(m_rotation->wanted());
        QCOMPARE(lookups(), 1);
        serve();
        answer(orientSuid);
        QCOMPARE(count(ssc::msgOnChange), 1);
        inject(response(lastTxn(ssc::msgOnChange)));
        QCOMPARE(d.settings.store(AutoRotate, false), Settings::Changed);
        QVERIFY(!m_rotation->wanted());
        QCOMPARE(count(ssc::msgDisable), 1);
    }

    void subscribeStartsUnsubscribeStops()
    {
        Daemon d;
        make();
        d.start(*m_rotation);
        QDBusConnection sub = d.caller(u"sub-1"_s);
        QCOMPARE(Daemon::call(sub, u"Subscribe"_s, Rotation), QString());
        QVERIFY(m_rotation->wanted());
        QCOMPARE(lookups(), 1);
        serve();
        answer(orientSuid);
        inject(response(lastTxn(ssc::msgOnChange)));
        QVERIFY(m_rotation->claimed());
        QCOMPARE(Daemon::call(sub, u"Subscribe"_s, Rotation), QString());
        QCOMPARE(count(ssc::msgOnChange), 1);
        QCOMPARE(Daemon::call(d.h.client(), u"Unsubscribe"_s, Rotation), QString());
        QVERIFY(m_rotation->wanted());
        QCOMPARE(Daemon::call(sub, u"Unsubscribe"_s, Rotation), QString());
        QVERIFY(!m_rotation->wanted());
        QCOMPARE(count(ssc::msgDisable), 1);
        QCOMPARE(Daemon::call(sub, u"Unsubscribe"_s, Rotation), QString());
        QDBusConnection::disconnectFromBus(u"sub-1"_s);
    }

    void nameLossStops()
    {
        Daemon d;
        make();
        d.start(*m_rotation);
        QCOMPARE(Daemon::call(d.caller(u"sub-1"_s), u"Subscribe"_s, Rotation), QString());
        QCOMPARE(Daemon::call(d.caller(u"sub-2"_s), u"Subscribe"_s, Rotation), QString());
        serve();
        answer(orientSuid);
        inject(response(lastTxn(ssc::msgOnChange)));
        QVERIFY(m_rotation->claimed());
        QDBusConnection::disconnectFromBus(u"sub-1"_s);
        QTest::qWait(300);
        QVERIFY(m_rotation->wanted());
        QDBusConnection::disconnectFromBus(u"sub-2"_s);
        QTRY_VERIFY_WITH_TIMEOUT(!m_rotation->wanted(), waitMs);
        QCOMPARE(count(ssc::msgDisable), 1);
        QVERIFY(!d.service->held(Rotation));
    }

    void goneNameNotHeld()
    {
        Daemon d;
        make();
        d.start(*m_rotation);
        {
            QDBusConnection sub = d.caller(u"sub-3"_s);
            QDBusMessage message = QDBusMessage::createMethodCall(u"net.seilent.MicroSet"_s, u"/net/seilent/MicroSet"_s,
                                                                  u"net.seilent.MicroSet1"_s, u"Subscribe"_s);
            message << Rotation;
            QVERIFY(sub.send(message));
            QVERIFY(sub.interface()->isServiceRegistered(u"org.freedesktop.DBus"_s).value());
        }
        QDBusConnection::disconnectFromBus(u"sub-3"_s);
        QTest::qWait(300);
        QVERIFY(!d.service->held(Rotation));
        QVERIFY(!m_rotation->wanted());
        QVERIFY(m_sent.isEmpty());
    }

    void subscribeChecked()
    {
        Daemon d;
        make();
        d.start(*m_rotation);
        QCOMPARE(Daemon::call(d.h.client(), u"Subscribe"_s, u"nope.key"_s),
                 u"net.seilent.MicroSet1.Error.UnknownKey"_s);
        QCOMPARE(Daemon::call(d.h.client(), u"Subscribe"_s, u"audio.output"_s),
                 u"org.freedesktop.DBus.Error.InvalidArgs"_s);
        QCOMPARE(Daemon::call(d.h.client(), u"Unsubscribe"_s, Orientation),
                 u"org.freedesktop.DBus.Error.InvalidArgs"_s);
        d.h.writeCgroup(QCoreApplication::applicationPid(), Harness::GameCgroup);
        QCOMPARE(Daemon::call(d.h.client(), u"Subscribe"_s, Rotation), u"org.freedesktop.DBus.Error.AccessDenied"_s);
        d.h.writeCgroup(QCoreApplication::applicationPid(), Harness::RootCgroup);
        QVERIFY(!d.service->held(Rotation));
    }

    void rotationFollowsFrames()
    {
        Daemon d;
        make();
        QCOMPARE(d.settings.store(Orientation, u"landscape"_s), Settings::Changed);
        d.start(*m_rotation);
        QCOMPARE(d.rotation(), u"landscape"_s);
        QSignalSpy changed(&d.settings, &Settings::changed);
        QCOMPARE(Daemon::call(d.h.client(), u"Subscribe"_s, Rotation), QString());
        serve();
        answer(orientSuid);
        inject(response(lastTxn(ssc::msgOnChange)));
        QCOMPARE(d.rotation(), u"landscape"_s);
        inject(orient(0));
        QTRY_COMPARE_WITH_TIMEOUT(d.rotation(), u"portrait"_s, waitMs);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(changed.at(0).at(0).toString(), Rotation);
        QCOMPARE(changed.at(0).at(1), QVariant(u"portrait"_s));
        inject(orient(3));
        QTRY_COMPARE_WITH_TIMEOUT(d.rotation(), u"landscape-flipped"_s, waitMs);
        inject(orient(2));
        QTRY_COMPARE_WITH_TIMEOUT(d.rotation(), u"portrait-flipped"_s, waitMs);
        QCOMPARE(d.settings.store(Orientation, u"portrait"_s), Settings::Changed);
        QCOMPARE(d.rotation(), u"portrait-flipped"_s);
        QCOMPARE(Daemon::call(d.h.client(), u"Unsubscribe"_s, Rotation), QString());
        QCOMPARE(d.rotation(), u"portrait"_s);
        QCOMPARE(d.settings.store(Orientation, u"landscape-flipped"_s), Settings::Changed);
        QCOMPARE(d.rotation(), u"landscape-flipped"_s);
        QVERIFY(d.h.readState().contains("\"display.orientation\": \"landscape-flipped\""));
        QVERIFY(!d.h.readState().contains("display.rotation"));
    }
};

QTEST_GUILESS_MAIN(SscTest)
#include "tst_ssc.moc"
