#pragma once

#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>

class UndervoltsProvider
{
public:
    UndervoltsProvider(const QString &conf, const QString &frequencies);

    QStringList names() const;
    QString levels(const QString &name) const;
    QJsonObject labels() const;
    static QMap<quint64, uint> parse(const QString &levels);

private:
    struct Row
    {
        QString label;
        QString levels;
    };

    QMap<QString, Row> rows() const;

    QString m_conf;
    QString m_frequencies;
};
