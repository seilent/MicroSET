#include "KeyboardModel.h"

#include <QStringList>

namespace microdap {

namespace {

const char *const letters[10] = {"@-_/", ".,?!", "abc", "def", "ghi", "jkl", "mno", "pqrs", "tuv", "wxyz"};
const char *const symbolPage[10] = {"", "\"#$", "%&'", "()*", "+:;", "<=>", "[\\]", "^`{", "|}~", ""};
const QStringList dirs{QStringLiteral("tap"), QStringLiteral("left"), QStringLiteral("up"), QStringLiteral("right"),
                       QStringLiteral("down")};

struct Slot
{
    const char *code;
    int col;
    int row;
    int cols = 1;
    int rows = 1;
};

const Slot textSlots[] = {{"1", 0, 0}, {"2", 1, 0}, {"3", 2, 0}, {"delete", 3, 0},
                          {"4", 0, 1}, {"5", 1, 1}, {"6", 2, 1}, {"symbols", 3, 1},
                          {"7", 0, 2}, {"8", 1, 2}, {"9", 2, 2}, {"cancel", 3, 2},
                          {"shift", 0, 3}, {"0", 1, 3}, {"space", 2, 3}, {"done", 3, 3}};

const Slot numberSlots[] = {{"1", 0, 0}, {"2", 1, 0}, {"3", 2, 0}, {"delete", 3, 0},
                            {"4", 0, 1}, {"5", 1, 1}, {"6", 2, 1}, {"cancel", 3, 1},
                            {"7", 0, 2}, {"8", 1, 2}, {"9", 2, 2}, {"done", 3, 2, 1, 2}};

QVariantMap slotMap(const Slot &slot)
{
    return {{QStringLiteral("code"), QString::fromLatin1(slot.code)},
            {QStringLiteral("col"), slot.col},
            {QStringLiteral("row"), slot.row},
            {QStringLiteral("cols"), slot.cols},
            {QStringLiteral("rows"), slot.rows}};
}

bool isLetter(QChar c)
{
    return c >= QLatin1Char('a') && c <= QLatin1Char('z');
}

}

KeyboardModel::KeyboardModel(QObject *parent)
    : QObject(parent)
{
}

QString KeyboardModel::charAt(bool symbols, int digit, const QString &dir)
{
    const int d = dirs.indexOf(dir);
    if (digit < 0 || digit > 9 || d < 0)
        return {};
    if (d == 4)
        return QString(QChar(u'0' + digit));
    const QString s = QString::fromLatin1(symbols ? symbolPage[digit] : letters[digit]);
    if (s.isEmpty())
        return d == 0 ? QString(QChar(u'0' + digit)) : QString();
    return d < s.size() ? QString(s.at(d)) : QString();
}

QString KeyboardModel::title() const
{
    return m_title;
}

QString KeyboardModel::hint() const
{
    return m_hint;
}

QString KeyboardModel::unit() const
{
    return m_unit;
}

bool KeyboardModel::number() const
{
    return m_number;
}

QVariantList KeyboardModel::keys() const
{
    QVariantList out;
    if (!m_number) {
        for (const Slot &slot : textSlots)
            out.append(slotMap(slot));
        return out;
    }
    for (const Slot &slot : numberSlots)
        out.append(slotMap(slot));
    const int minus = m_extras.contains(QLatin1Char('-')) ? 1 : 0;
    const int point = m_extras.contains(QLatin1Char('.')) ? 1 : 0;
    if (minus)
        out.append(slotMap({"minus", 0, 3}));
    out.append(slotMap({"0", minus, 3, 3 - minus - point}));
    if (point)
        out.append(slotMap({"point", 2, 3}));
    return out;
}

QString KeyboardModel::text() const
{
    return m_text;
}

bool KeyboardModel::ready() const
{
    const int n = bytes(m_text);
    if (n < m_minBytes || n > m_maxBytes)
        return false;
    if (!m_number || m_text.isEmpty())
        return true;
    bool ok = false;
    m_text.toDouble(&ok);
    return ok;
}

bool KeyboardModel::edited() const
{
    return !m_text.isEmpty() && m_text != m_initial;
}

int KeyboardModel::shift() const
{
    return m_shift;
}

bool KeyboardModel::symbols() const
{
    return m_symbols;
}

QVariantMap KeyboardModel::labels() const
{
    QVariantMap out;
    for (const QVariant &k : keys()) {
        const QString code = k.toMap().value(QStringLiteral("code")).toString();
        out.insert(code, label(code));
    }
    return out;
}

QVariantList KeyboardModel::chars() const
{
    QVariantList out;
    for (int digit = 0; digit < 10; ++digit) {
        QVariantMap cells;
        for (const QString &dir : dirs)
            cells.insert(dir, pick(digit, dir));
        out.append(cells);
    }
    return out;
}

int KeyboardModel::bytes(const QString &s) const
{
    return int(s.toUtf8().size());
}

void KeyboardModel::setText(const QString &text)
{
    if (text == m_text)
        return;
    m_text = text;
    emit textChanged();
}

void KeyboardModel::open(const QVariantMap &request)
{
    m_title = request.value(QStringLiteral("title")).toString();
    m_hint = request.value(QStringLiteral("hint")).toString();
    m_initial = request.value(QStringLiteral("text")).toString();
    m_unit = request.value(QStringLiteral("unit")).toString();
    m_extras = request.value(QStringLiteral("extras")).toString();
    m_number = request.value(QStringLiteral("kind")).toString() == QLatin1StringView("number");
    m_decimals = request.value(QStringLiteral("decimals"), 0).toInt();
    m_minBytes = request.value(QStringLiteral("min"), m_number ? 1 : 0).toInt();
    m_maxBytes = request.value(QStringLiteral("max"), m_number ? 8 : 64).toInt();
    m_ranged = request.contains(QStringLiteral("minValue")) && request.contains(QStringLiteral("maxValue"));
    m_minValue = request.value(QStringLiteral("minValue")).toDouble();
    m_maxValue = request.value(QStringLiteral("maxValue")).toDouble();
    QString initial = m_initial;
    while (bytes(initial) > m_maxBytes)
        initial.chop(initial.size() >= 2 && initial.at(initial.size() - 1).isLowSurrogate() ? 2 : 1);
    m_initial = initial;
    m_text = initial;
    m_shift = 0;
    m_symbols = false;
    m_shiftAt.invalidate();
    emit requestChanged();
    emit textChanged();
    emit pageChanged();
}

bool KeyboardModel::type(const QString &ch)
{
    if (ch.size() != 1 || bytes(m_text + ch) > m_maxBytes)
        return false;
    QChar c = ch.at(0);
    if (m_number) {
        const qsizetype dot = m_text.indexOf(QLatin1Char('.'));
        if (c == QLatin1Char('-') ? !m_text.isEmpty()
            : c == QLatin1Char('.') ? dot >= 0 || m_decimals <= 0
                                    : dot >= 0 && m_text.size() - dot - 1 >= m_decimals)
            return false;
    }
    const bool letter = isLetter(c);
    if (m_shift != 0 && letter)
        c = c.toUpper();
    if (m_shift == 1 && letter) {
        m_shift = 0;
        emit pageChanged();
    }
    setText(m_text + c);
    return true;
}

bool KeyboardModel::erase()
{
    if (m_text.isEmpty())
        return false;
    const qsizetype n = m_text.size() >= 2 && m_text.at(m_text.size() - 1).isLowSurrogate() ? 2 : 1;
    setText(m_text.left(m_text.size() - n));
    return true;
}

QString KeyboardModel::pick(int digit, const QString &dir) const
{
    const QString c = charAt(m_symbols, digit, dir);
    return m_shift != 0 && c.size() == 1 && isLetter(c.at(0)) ? c.toUpper() : c;
}

bool KeyboardModel::commit(int digit, const QString &dir)
{
    const QString c = charAt(m_symbols, digit, dir);
    return !c.isEmpty() && type(c);
}

void KeyboardModel::tapShift()
{
    if (m_shift == 0) {
        m_shift = 1;
        m_shiftAt.start();
    } else if (m_shift == 1 && m_shiftAt.isValid() && m_shiftAt.elapsed() <= shiftDoubleMs) {
        m_shift = 2;
    } else {
        m_shift = 0;
    }
    emit pageChanged();
}

void KeyboardModel::stepShift()
{
    m_shift = (m_shift + 1) % 3;
    m_shiftAt.invalidate();
    emit pageChanged();
}

void KeyboardModel::toggleSymbols()
{
    m_symbols = !m_symbols;
    emit pageChanged();
}

QString KeyboardModel::label(const QString &code) const
{
    bool digit = false;
    const int n = code.toInt(&digit);
    if (digit && m_number)
        return code;
    if (digit) {
        const QString s = QString::fromLatin1(m_symbols ? symbolPage[n] : letters[n]);
        const bool spaced = m_symbols || n <= 1;
        QString out;
        for (const QChar ch : s) {
            if (spaced && !out.isEmpty())
                out += QLatin1Char(' ');
            out += m_shift != 0 && !m_symbols && isLetter(ch) ? ch.toUpper() : ch;
        }
        return out;
    }
    if (code == QLatin1StringView("shift"))
        return m_shift != 0 ? QStringLiteral("SHIFT") : QStringLiteral("shift");
    if (code == QLatin1StringView("symbols"))
        return m_symbols ? QStringLiteral("abc") : QStringLiteral("#+=");
    if (code == QLatin1StringView("minus"))
        return QStringLiteral("-");
    if (code == QLatin1StringView("point"))
        return QStringLiteral(".");
    return code;
}

QString KeyboardModel::refusal() const
{
    if (!m_number || !m_ranged)
        return {};
    bool ok = false;
    const double v = m_text.toDouble(&ok);
    if (ok && v >= m_minValue && v <= m_maxValue)
        return {};
    QString range = QStringLiteral("%1 to %2").arg(QString::number(m_minValue), QString::number(m_maxValue));
    if (!m_unit.isEmpty())
        range += QLatin1Char(' ') + m_unit;
    return range;
}

}
