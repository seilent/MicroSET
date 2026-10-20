#include "undervoltsprovider.h"

#include <QFile>
#include <QRegularExpression>

#include <algorithm>

using namespace Qt::StringLiterals;

UndervoltsProvider::UndervoltsProvider(const QString &conf, const QString &frequencies)
    : m_conf(conf)
    , m_frequencies(frequencies)
{
}

QMap<quint64, uint> UndervoltsProvider::parse(const QString &levels)
{
    static const QRegularExpression pair(u"^([0-9]{1,10}):([0-9]{1,5})$"_s);
    QMap<quint64, uint> out;
    for (const QString &item : levels.split(u',')) {
        const QRegularExpressionMatch match = pair.match(item);
        if (!match.hasMatch() || out.contains(match.captured(1).toULongLong()))
            return {};
        out.insert(match.captured(1).toULongLong(), match.captured(2).toUInt());
    }
    return out;
}

QMap<QString, UndervoltsProvider::Row> UndervoltsProvider::rows() const
{
    static const QRegularExpression name(u"^[a-z][a-z0-9-]{0,31}$"_s);
    QMap<QString, Row> out;
    QFile file(m_conf);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return out;
    while (!file.atEnd()) {
        const QStringList fields = QString::fromUtf8(file.readLine()).simplified().split(u' ', Qt::SkipEmptyParts);
        if (fields.isEmpty())
            continue;
        if (fields.size() != 3 || !name.match(fields.at(0)).hasMatch() || fields.at(0) == "off"_L1
            || out.contains(fields.at(0)) || parse(fields.at(2)).isEmpty())
            return {};
        out.insert(fields.at(0), {fields.at(1), fields.at(2)});
    }
    return out;
}

QStringList UndervoltsProvider::names() const
{
    QFile file(m_frequencies);
    QList<quint64> live;
    if (file.open(QIODevice::ReadOnly | QIODevice::Text))
        for (const QString &item : QString::fromLatin1(file.read(4096)).simplified().split(u' ', Qt::SkipEmptyParts))
            live << item.toULongLong();
    std::sort(live.begin(), live.end());
    QStringList out{u"off"_s};
    const QMap<QString, Row> all = rows();
    for (auto it = all.cbegin(); it != all.cend(); ++it)
        if (!live.isEmpty() && parse(it.value().levels).keys() == live)
            out << it.key();
    return out;
}

QString UndervoltsProvider::levels(const QString &name) const
{
    return rows().value(name).levels;
}

QJsonObject UndervoltsProvider::labels() const
{
    QJsonObject out{{u"off"_s, u"Off"_s}};
    const QMap<QString, Row> all = rows();
    for (auto it = all.cbegin(); it != all.cend(); ++it)
        out.insert(it.key(), it.value().label);
    return out;
}
