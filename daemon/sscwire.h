#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QList>

#include <optional>

namespace ssc {

constexpr quint32 service = 400;
constexpr quint32 controlPort = 0xfffffffe;
constexpr quint32 newServer = 4;
constexpr quint32 delServer = 5;
constexpr quint32 newLookup = 10;

constexpr quint8 typeRequest = 0;
constexpr quint8 typeResponse = 2;
constexpr quint8 typeIndication = 4;
constexpr quint16 msgControl = 0x20;
constexpr quint16 msgReportSmall = 0x21;
constexpr quint16 msgReportLarge = 0x22;

constexpr quint32 msgFlush = 2;
constexpr quint32 msgError = 130;
constexpr quint32 msgDisable = 10;
constexpr quint32 msgSuidRequest = 512;
constexpr quint32 msgOnChange = 514;
constexpr quint32 msgSuidEvent = 768;
constexpr quint32 msgOrientEvent = 776;

struct Suid
{
    quint64 low = 0;
    quint64 high = 0;

    bool operator==(const Suid &other) const;
};

constexpr Suid lookupSuid{0xABABABABABABABABull, 0xABABABABABABABABull};

enum class WireType { Varint = 0, Fixed64 = 1, Bytes = 2, Fixed32 = 5 };

struct Field
{
    quint32 id = 0;
    WireType type = WireType::Varint;
    quint64 value = 0;
    QByteArrayView bytes;
};

void putVarint(QByteArray &buf, quint64 value);
void putUint(QByteArray &buf, quint32 field, quint64 value);
void putFixed32(QByteArray &buf, quint32 field, quint32 value);
void putFixed64(QByteArray &buf, quint32 field, quint64 value);
void putBytes(QByteArray &buf, quint32 field, QByteArrayView value);
QList<Field> fields(QByteArrayView buf);
std::optional<quint64> uintField(QByteArrayView buf, quint32 field);

struct Result
{
    quint16 result = 0;
    quint16 error = 0;
};

struct Message
{
    quint8 type = 0;
    quint16 txn = 0;
    quint16 msgId = 0;
    QByteArrayView tlvs;

    std::optional<QByteArrayView> tlv(quint8 id) const;
    std::optional<Result> result() const;
    std::optional<QByteArrayView> data() const;
};

QByteArray control(quint16 txn, QByteArrayView request);
std::optional<Message> parse(QByteArrayView datagram);

struct Control
{
    quint32 cmd = 0;
    quint32 service = 0;
    quint32 instance = 0;
    quint32 node = 0;
    quint32 port = 0;
};

QByteArray lookup(quint32 service);
std::optional<Control> parseControl(QByteArrayView datagram);

struct Event
{
    quint32 msgId = 0;
    QByteArray payload;
};

struct Report
{
    Suid suid;
    QList<Event> events;
};

QByteArray suidBytes(const Suid &suid);
std::optional<Suid> parseSuid(QByteArrayView bytes);
QByteArray request(const Suid &suid, quint32 msgId, const std::optional<QByteArray> &payload);
QByteArray suidRequest(QByteArrayView dataType);
std::optional<Report> parseReport(QByteArrayView bytes);

}
