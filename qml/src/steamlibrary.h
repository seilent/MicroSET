#pragma once

#include <QByteArray>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <optional>

class SteamLibrary
{
public:
    struct Game
    {
        QString appid;
        QString name;
    };

    static constexpr int MaxApps = 2000;
    static constexpr qint64 MaxFile = 1 << 20;
    static constexpr int MaxName = 128;

    explicit SteamLibrary(const QString &root);

    static QString defaultRoot();
    static QStringList excluded();
    static std::optional<QVariantMap> parseText(const QByteArray &data);
    static std::optional<QVariantMap> parseBinary(const QByteArray &data);

    QStringList libraries() const;
    QList<Game> games(QSet<QString> *dropped = nullptr) const;
    QString name(const QString &appid) const;
    QByteArray stamp() const;

private:
    std::optional<Game> manifest(const QString &library, const QString &appid) const;
    QList<Game> shortcuts() const;

    QString m_root;
};
