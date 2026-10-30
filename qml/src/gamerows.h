#pragma once

#include <QAbstractListModel>
#include <QQmlEngine>
#include <QStringList>

class GameRows : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role { KindRole = Qt::UserRole + 1, LetterRole, TitleRole, AppidRole };

    explicit GameRows(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const;
    Q_INVOKABLE void setGames(const QVariantList &games);
    Q_INVOKABLE void setQuery(const QString &text);
    Q_INVOKABLE QStringList letters() const;
    Q_INVOKABLE int rowOfLetter(const QString &letter) const;

    static QString fold(const QString &text);

signals:
    void countChanged();

private:
    struct Game
    {
        QString appid;
        QString title;
        QString letter;
        QString folded;
    };

    struct Row
    {
        QString kind;
        QString letter;
        QString title;
        QString appid;
    };

    void rebuild();

    QList<Game> m_games;
    QList<Row> m_rows;
    QString m_query;
    QStringList m_words;
    int m_count = 0;
};
