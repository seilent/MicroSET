#include "gamerows.h"

#include "SectionKey.h"

#include <algorithm>

using namespace Qt::StringLiterals;

GameRows::GameRows(QObject *parent)
    : QAbstractListModel(parent)
{
    rebuild();
}

int GameRows::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant GameRows::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const Row &row = m_rows.at(index.row());
    switch (role) {
    case KindRole:
        return row.kind;
    case LetterRole:
        return row.letter;
    case TitleRole:
        return row.title;
    case AppidRole:
        return row.appid;
    default:
        return {};
    }
}

QHash<int, QByteArray> GameRows::roleNames() const
{
    return {{KindRole, "kind"}, {LetterRole, "letter"}, {TitleRole, "title"}, {AppidRole, "appid"}};
}

int GameRows::count() const
{
    return m_count;
}

void GameRows::setGames(const QVariantList &games)
{
    QList<Game> out;
    out.reserve(games.size());
    for (const QVariant &item : games) {
        const QVariantMap game = item.toMap();
        const QString title = game.value(u"title"_s).toString();
        out << Game{game.value(u"appid"_s).toString(), title, microdap::sectionKey(title).letter, fold(title)};
    }
    const int before = m_count;
    beginResetModel();
    m_games = out;
    rebuild();
    endResetModel();
    if (m_count != before)
        emit countChanged();
}

void GameRows::setQuery(const QString &text)
{
    const int before = m_count;
    beginResetModel();
    m_query = text;
    m_words = fold(text).split(u' ', Qt::SkipEmptyParts);
    rebuild();
    endResetModel();
    if (m_count != before)
        emit countChanged();
}

QStringList GameRows::letters() const
{
    QStringList out;
    for (const Row &row : m_rows)
        if (row.kind == "divider"_L1)
            out << row.letter;
    return out;
}

int GameRows::rowOfLetter(const QString &letter) const
{
    for (qsizetype i = 0; i < m_rows.size(); ++i)
        if (m_rows.at(i).kind == "divider"_L1 && m_rows.at(i).letter == letter)
            return int(i);
    return -1;
}

QString GameRows::fold(const QString &text)
{
    QList<char32_t> kept;
    for (const char32_t c : text.toUcs4()) {
        const QChar::Category category = QChar::category(c);
        if (category < QChar::Punctuation_Connector || category > QChar::Symbol_Other)
            kept << c;
    }
    const QString plain = QString::fromUcs4(kept.constData(), kept.size());
    QList<char32_t> out;
    bool space = false;
    for (const char32_t c : plain.normalized(QString::NormalizationForm_KD).toUcs4()) {
        const QChar::Category category = QChar::category(c);
        if (category == QChar::Mark_NonSpacing || category == QChar::Mark_SpacingCombining
            || category == QChar::Mark_Enclosing)
            continue;
        if (QChar::isSpace(c)) {
            space = !out.isEmpty();
            continue;
        }
        if (space)
            out << U' ';
        space = false;
        out << QChar::toLower(c);
    }
    return QString::fromUcs4(out.constData(), out.size());
}

void GameRows::rebuild()
{
    m_rows = {Row{u"search"_s, QString(), m_query, QString()}};
    m_count = 0;
    QString letter;
    for (const Game &game : std::as_const(m_games)) {
        const bool match = std::all_of(m_words.cbegin(), m_words.cend(),
                                       [&game](const QString &word) { return game.folded.contains(word); });
        if (!match)
            continue;
        if (game.letter != letter) {
            letter = game.letter;
            m_rows << Row{u"divider"_s, letter, letter, QString()};
        }
        m_rows << Row{u"game"_s, game.letter, game.title, game.appid};
        ++m_count;
    }
}
