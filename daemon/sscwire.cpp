#include "sscwire.h"

#include <QtEndian>

namespace ssc {

namespace {

constexpr qsizetype headerLen = 7;
constexpr qsizetype controlLen = 20;
constexpr quint64 processorApss = 1;
constexpr quint64 deliveryNoWakeup = 1;

void putKey(QByteArray &buf, quint32 field, WireType type)
{
    putVarint(buf, (quint64(field) << 3) | quint64(type));
}

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

void putLe64(QByteArray &buf, quint64 value)
{
    for (int i = 0; i < 8; ++i)
        buf.append(char((value >> (8 * i)) & 0xff));
}

void putTlv(QByteArray &buf, quint8 id, QByteArrayView value)
{
    buf.append(char(id));
    putLe16(buf, quint16(value.size()));
    buf.append(value);
}

quint16 le16(QByteArrayView buf, qsizetype pos)
{
    return qFromLittleEndian<quint16>(buf.data() + pos);
}

quint32 le32(QByteArrayView buf, qsizetype pos)
{
    return qFromLittleEndian<quint32>(buf.data() + pos);
}

std::optional<quint64> readVarint(QByteArrayView buf, qsizetype &pos)
{
    quint64 value = 0;
    for (int shift = 0; shift < 64; shift += 7) {
        if (pos >= buf.size())
            return std::nullopt;
        const auto byte = quint8(buf.at(pos++));
        value |= quint64(byte & 0x7f) << shift;
        if (!(byte & 0x80))
            return value;
    }
    return std::nullopt;
}

}

bool Suid::operator==(const Suid &other) const
{
    return low == other.low && high == other.high;
}

void putVarint(QByteArray &buf, quint64 value)
{
    while (value >= 0x80) {
        buf.append(char((value & 0x7f) | 0x80));
        value >>= 7;
    }
    buf.append(char(value));
}

void putUint(QByteArray &buf, quint32 field, quint64 value)
{
    putKey(buf, field, WireType::Varint);
    putVarint(buf, value);
}

void putFixed32(QByteArray &buf, quint32 field, quint32 value)
{
    putKey(buf, field, WireType::Fixed32);
    putLe32(buf, value);
}

void putFixed64(QByteArray &buf, quint32 field, quint64 value)
{
    putKey(buf, field, WireType::Fixed64);
    putLe64(buf, value);
}

void putBytes(QByteArray &buf, quint32 field, QByteArrayView value)
{
    putKey(buf, field, WireType::Bytes);
    putVarint(buf, quint64(value.size()));
    buf.append(value);
}

QList<Field> fields(QByteArrayView buf)
{
    QList<Field> out;
    qsizetype pos = 0;
    while (pos < buf.size()) {
        const std::optional<quint64> key = readVarint(buf, pos);
        if (!key)
            break;
        Field field;
        field.id = quint32(*key >> 3);
        const quint64 type = *key & 0x7;
        if (type == quint64(WireType::Varint)) {
            const std::optional<quint64> value = readVarint(buf, pos);
            if (!value)
                break;
            field.type = WireType::Varint;
            field.value = *value;
        } else if (type == quint64(WireType::Fixed64)) {
            if (buf.size() - pos < 8)
                break;
            field.type = WireType::Fixed64;
            field.value = qFromLittleEndian<quint64>(buf.data() + pos);
            pos += 8;
        } else if (type == quint64(WireType::Bytes)) {
            const std::optional<quint64> len = readVarint(buf, pos);
            if (!len || *len > quint64(buf.size() - pos))
                break;
            field.type = WireType::Bytes;
            field.bytes = buf.sliced(pos, qsizetype(*len));
            pos += qsizetype(*len);
        } else if (type == quint64(WireType::Fixed32)) {
            if (buf.size() - pos < 4)
                break;
            field.type = WireType::Fixed32;
            field.value = le32(buf, pos);
            pos += 4;
        } else {
            break;
        }
        out.append(field);
    }
    return out;
}

std::optional<quint64> uintField(QByteArrayView buf, quint32 field)
{
    for (const Field &entry : fields(buf)) {
        if (entry.id == field && entry.type == WireType::Varint)
            return entry.value;
    }
    return std::nullopt;
}

std::optional<QByteArrayView> Message::tlv(quint8 id) const
{
    qsizetype pos = 0;
    while (tlvs.size() - pos >= 3) {
        const auto entry = quint8(tlvs.at(pos));
        const qsizetype len = le16(tlvs, pos + 1);
        if (tlvs.size() - pos - 3 < len)
            return std::nullopt;
        if (entry == id)
            return tlvs.sliced(pos + 3, len);
        pos += 3 + len;
    }
    return std::nullopt;
}

std::optional<Result> Message::result() const
{
    const std::optional<QByteArrayView> value = tlv(0x02);
    if (!value || value->size() < 4)
        return std::nullopt;
    return Result{le16(*value, 0), le16(*value, 2)};
}

std::optional<QByteArrayView> Message::data() const
{
    const std::optional<QByteArrayView> value = tlv(0x02);
    if (!value || value->size() < 2)
        return std::nullopt;
    const qsizetype len = le16(*value, 0);
    if (value->size() - 2 < len)
        return std::nullopt;
    return value->sliced(2, len);
}

QByteArray control(quint16 txn, QByteArrayView request)
{
    QByteArray payload;
    putLe16(payload, quint16(request.size()));
    payload.append(request);

    QByteArray tlvs;
    putTlv(tlvs, 0x01, payload);
    putTlv(tlvs, 0x10, QByteArray(1, char(1)));

    QByteArray buf;
    buf.append(char(typeRequest));
    putLe16(buf, txn);
    putLe16(buf, msgControl);
    putLe16(buf, quint16(tlvs.size()));
    buf.append(tlvs);
    return buf;
}

std::optional<Message> parse(QByteArrayView datagram)
{
    if (datagram.size() < headerLen)
        return std::nullopt;
    const qsizetype len = le16(datagram, 5);
    if (datagram.size() - headerLen < len)
        return std::nullopt;
    Message message;
    message.type = quint8(datagram.at(0));
    message.txn = le16(datagram, 1);
    message.msgId = le16(datagram, 3);
    message.tlvs = datagram.sliced(headerLen, len);
    return message;
}

QByteArray lookup(quint32 service)
{
    QByteArray buf;
    for (const quint32 value : {newLookup, service, 0u, 0u, 0u})
        putLe32(buf, value);
    return buf;
}

std::optional<Control> parseControl(QByteArrayView datagram)
{
    if (datagram.size() < controlLen)
        return std::nullopt;
    return Control{le32(datagram, 0), le32(datagram, 4), le32(datagram, 8), le32(datagram, 12),
                   le32(datagram, 16)};
}

QByteArray suidBytes(const Suid &suid)
{
    QByteArray buf;
    putFixed64(buf, 1, suid.low);
    putFixed64(buf, 2, suid.high);
    return buf;
}

std::optional<Suid> parseSuid(QByteArrayView bytes)
{
    std::optional<quint64> low;
    std::optional<quint64> high;
    for (const Field &field : fields(bytes)) {
        if (field.type != WireType::Fixed64)
            continue;
        if (field.id == 1)
            low = field.value;
        else if (field.id == 2)
            high = field.value;
    }
    if (!low || !high)
        return std::nullopt;
    return Suid{*low, *high};
}

QByteArray request(const Suid &suid, quint32 msgId, const std::optional<QByteArray> &payload)
{
    QByteArray config;
    putUint(config, 1, processorApss);
    putUint(config, 2, deliveryNoWakeup);
    QByteArray body;
    if (payload)
        putBytes(body, 2, *payload);

    QByteArray buf;
    putBytes(buf, 1, suidBytes(suid));
    putFixed32(buf, 2, msgId);
    putBytes(buf, 3, config);
    putBytes(buf, 4, body);
    return buf;
}

QByteArray suidRequest(QByteArrayView dataType)
{
    QByteArray payload;
    putBytes(payload, 1, dataType);
    putUint(payload, 2, 0);
    putUint(payload, 3, 1);
    return request(lookupSuid, msgSuidRequest, payload);
}

std::optional<Report> parseReport(QByteArrayView bytes)
{
    const QList<Field> all = fields(bytes);
    std::optional<Suid> suid;
    for (const Field &field : all) {
        if (field.id == 1 && field.type == WireType::Bytes) {
            suid = parseSuid(field.bytes);
            if (suid)
                break;
        }
    }
    if (!suid)
        return std::nullopt;
    Report report;
    report.suid = *suid;
    for (const Field &field : all) {
        if (field.id != 2 || field.type != WireType::Bytes)
            continue;
        std::optional<quint32> msgId;
        QByteArray payload;
        for (const Field &inner : fields(field.bytes)) {
            if (inner.id == 1 && inner.type == WireType::Fixed32)
                msgId = quint32(inner.value);
            else if (inner.id == 3 && inner.type == WireType::Bytes)
                payload = inner.bytes.toByteArray();
        }
        if (msgId)
            report.events.append(Event{*msgId, payload});
    }
    return report;
}

}
