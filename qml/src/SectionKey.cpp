#include "SectionKey.h"

namespace microdap {

namespace {

constexpr char16_t kanaRows[] = {u'あ', u'か', u'さ', u'た', u'な', u'は', u'ま', u'や', u'ら', u'わ'};
constexpr char16_t kanaRowStarts[] = {0x3041, 0x304B, 0x3055, 0x305F, 0x306A, 0x306F, 0x307E, 0x3083, 0x3089, 0x308E};
constexpr char16_t smallKatakanaExt[] = {u'く', u'し', u'す', u'と', u'ぬ', u'は', u'ひ', u'ふ',
                                         u'へ', u'ほ', u'む', u'ら', u'り', u'る', u'れ', u'ろ'};
constexpr int hashRank = 1;
constexpr int latinRank = 2;
constexpr int kanaRank = 28;
constexpr int kanjiRank = 38;

bool isKanaMark(char16_t c)
{
    return (c >= 0x3099 && c <= 0x309E) || (c >= 0x30FB && c <= 0x30FE);
}

bool isHiragana(char16_t c)
{
    return c >= 0x3041 && c <= 0x3096;
}

char16_t toHiragana(char16_t c)
{
    if (c >= 0x30A1 && c <= 0x30F6)
        return char16_t(c - 0x60);
    if (c >= 0x30F7 && c <= 0x30FA)
        return char16_t(0x308F + (c - 0x30F7));
    if (c >= 0x31F0 && c <= 0x31FF)
        return smallKatakanaExt[c - 0x31F0];
    return c;
}

char16_t baseHiragana(char16_t c)
{
    if (c <= 0x304A)
        return (c - 0x3041) % 2 == 0 ? char16_t(c + 1) : c;
    if (c <= 0x3062)
        return (c - 0x304B) % 2 == 1 ? char16_t(c - 1) : c;
    if (c <= 0x3065)
        return 0x3064;
    if (c <= 0x3069)
        return (c - 0x3066) % 2 == 1 ? char16_t(c - 1) : c;
    if (c <= 0x306E)
        return c;
    if (c <= 0x307D)
        return char16_t(0x306F + (c - 0x306F) / 3 * 3);
    if (c <= 0x3082)
        return c;
    if (c <= 0x3088)
        return (c - 0x3083) % 2 == 0 ? char16_t(c + 1) : c;
    if (c == 0x308E)
        return 0x308F;
    if (c == 0x3094)
        return 0x3046;
    if (c == 0x3095)
        return 0x304B;
    if (c == 0x3096)
        return 0x3051;
    return c;
}

int kanaRowOf(char16_t base)
{
    for (int i = 9; i > 0; --i) {
        if (base >= kanaRowStarts[i])
            return i;
    }
    return 0;
}

QString hiraganaOf(const QString &text)
{
    QString out = text;
    for (QChar &c : out)
        c = QChar(toHiragana(c.unicode()));
    return out;
}

QString ranked(int rank, const QString &key)
{
    return QString(QChar(char16_t(rank))) + key;
}

char32_t codePointAt(const QString &text, qsizetype i)
{
    const QChar c = text.at(i);
    if (c.isHighSurrogate() && i + 1 < text.size() && text.at(i + 1).isLowSurrogate())
        return QChar::surrogateToUcs4(c, text.at(i + 1));
    return c.unicode();
}

}

QString foldKana(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (const QChar c : text) {
        const char16_t u = c.unicode();
        if (isKanaMark(u))
            continue;
        const char16_t h = toHiragana(u);
        out.append(QChar(isHiragana(h) ? baseHiragana(h) : h));
    }
    return out;
}

SectionKey sectionKey(const QString &title, bool ignoreThe)
{
    QString key = title.trimmed().toLower();
    if (ignoreThe && key.startsWith(QLatin1StringView("the ")))
        key = key.sliced(4).trimmed();

    const char16_t first = key.isEmpty() ? u'\0' : key.at(0).unicode();
    if (first >= u'a' && first <= u'z')
        return {QString(QChar(first)), ranked(latinRank + (first - u'a'), key)};
    if (first < 0x80)
        return {QStringLiteral("#"), ranked(hashRank, key)};

    const QString normalized = key.normalized(QString::NormalizationForm_KC).toLower();
    qsizetype i = 0;
    while (i < normalized.size() && isKanaMark(normalized.at(i).unicode()))
        ++i;
    if (i < normalized.size()) {
        const char16_t c = normalized.at(i).unicode();
        if (i == 0 && c >= u'a' && c <= u'z')
            return {QString(QChar(c)), ranked(latinRank + (c - u'a'), normalized)};
        const char16_t h = toHiragana(c);
        if (isHiragana(h)) {
            const int row = kanaRowOf(baseHiragana(h));
            const QString rest = normalized.sliced(i);
            return {QString(QChar(kanaRows[row])),
                    ranked(kanaRank + row, foldKana(rest) + QChar(u'\u0001') + hiraganaOf(rest))};
        }
        if (QChar::script(codePointAt(normalized, i)) == QChar::Script_Han)
            return {QStringLiteral("漢"), ranked(kanjiRank, normalized.sliced(i))};
    }
    return {QStringLiteral("#"), ranked(hashRank, key)};
}

const QStringList &sectionLetters()
{
    static const QStringList letters = [] {
        QStringList out{QStringLiteral("#")};
        for (char16_t c = u'a'; c <= u'z'; ++c)
            out.append(QString(QChar(c)));
        for (const char16_t row : kanaRows)
            out.append(QString(QChar(row)));
        out.append(QStringLiteral("漢"));
        return out;
    }();
    return letters;
}

}
