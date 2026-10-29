#include "steamlibrary.h"

#include <QByteArrayList>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QtEndian>

#include <algorithm>
#include <cerrno>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace Qt::StringLiterals;

namespace {

constexpr int MaxDepth = 8;
constexpr int AppInfoDepth = 63;
constexpr quint32 AppInfoMagic = 0x07564429;
constexpr qint64 MaxAppInfo = qint64(64) << 20;
constexpr quint32 MaxNames = 1 << 20;
constexpr qsizetype AppInfoHead = 16;
constexpr qsizetype AppHeader = 60;
const QStringList Excluded = {u"228980"_s, u"1826330"_s, u"1161040"_s, u"993090"_s};
const QStringList NonGames = {u"Music"_s, u"Tool"_s, u"Config"_s, u"DLC"_s, u"Video"_s};
const QRegularExpression AppId(u"^[1-9][0-9]{0,9}$"_s);
const QRegularExpression User(u"^[0-9]{1,10}$"_s);
const QRegularExpression Manifest(u"^appmanifest_([1-9][0-9]{0,9})\\.acf$"_s);

std::optional<QByteArray> readFile(const QString &path, qint64 limit = SteamLibrary::MaxFile)
{
    const int fd = ::open(QFile::encodeName(path).constData(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0)
        return std::nullopt;
    struct stat st;
    bool ok = ::fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_size <= limit;
    QByteArray data;
    while (ok) {
        char buffer[65536];
        const ssize_t n = ::read(fd, buffer, sizeof buffer);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0) {
            ok = n == 0;
            break;
        }
        data.append(buffer, n);
        ok = data.size() <= limit;
    }
    ::close(fd);
    return ok ? std::optional<QByteArray>(data) : std::nullopt;
}

class TextReader
{
public:
    explicit TextReader(const QByteArray &data)
        : m_data(data)
    {
    }

    std::optional<QVariantMap> node(int depth)
    {
        QVariantMap out;
        for (;;) {
            skip();
            if (m_pos >= m_data.size())
                return depth == 0 ? std::optional<QVariantMap>(out) : std::nullopt;
            if (m_data.at(m_pos) == '}') {
                ++m_pos;
                return depth == 0 ? std::nullopt : std::optional<QVariantMap>(out);
            }
            const std::optional<QString> key = token();
            if (!key)
                return std::nullopt;
            skip();
            if (m_pos < m_data.size() && m_data.at(m_pos) == '{') {
                ++m_pos;
                if (depth + 1 > MaxDepth)
                    return std::nullopt;
                const std::optional<QVariantMap> child = node(depth + 1);
                if (!child)
                    return std::nullopt;
                out.insert(key->toLower(), *child);
                continue;
            }
            const std::optional<QString> value = token();
            if (!value)
                return std::nullopt;
            out.insert(key->toLower(), *value);
        }
    }

private:
    void skip()
    {
        while (m_pos < m_data.size()) {
            const char c = m_data.at(m_pos);
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                ++m_pos;
            } else if (c == '/' && m_pos + 1 < m_data.size() && m_data.at(m_pos + 1) == '/') {
                const qsizetype end = m_data.indexOf('\n', m_pos);
                m_pos = end < 0 ? m_data.size() : end + 1;
            } else {
                return;
            }
        }
    }

    std::optional<QString> token()
    {
        if (m_pos >= m_data.size() || m_data.at(m_pos) == '{' || m_data.at(m_pos) == '}')
            return std::nullopt;
        QByteArray out;
        if (m_data.at(m_pos) != '"') {
            while (m_pos < m_data.size() && !QByteArrayView(" \t\r\n{}\"").contains(m_data.at(m_pos)))
                out += m_data.at(m_pos++);
            return QString::fromUtf8(out);
        }
        for (++m_pos; m_pos < m_data.size(); ++m_pos) {
            const char c = m_data.at(m_pos);
            if (c == '"') {
                ++m_pos;
                return QString::fromUtf8(out);
            }
            if (c == '\\' && m_pos + 1 < m_data.size()) {
                const char next = m_data.at(++m_pos);
                out += next == 'n' ? '\n' : next == 't' ? '\t' : next;
                continue;
            }
            out += c;
        }
        return std::nullopt;
    }

    const QByteArray &m_data;
    qsizetype m_pos = 0;
};

class BinaryReader
{
public:
    explicit BinaryReader(const QByteArray &data, const QByteArrayList *names = nullptr, int maxDepth = MaxDepth)
        : m_data(data)
        , m_names(names)
        , m_maxDepth(maxDepth)
    {
    }

    qsizetype pos() const { return m_pos; }

    std::optional<QVariantMap> node(int depth)
    {
        QVariantMap out;
        for (;;) {
            if (m_pos >= m_data.size())
                return std::nullopt;
            const char type = m_data.at(m_pos++);
            if (type == 0x08 || type == 0x0b)
                return out;
            const std::optional<QByteArray> key = m_names ? indexed() : text();
            if (!key)
                return std::nullopt;
            const QString name = QString::fromUtf8(*key).toLower();
            switch (type) {
            case 0x00: {
                if (depth + 1 > m_maxDepth)
                    return std::nullopt;
                const std::optional<QVariantMap> child = node(depth + 1);
                if (!child)
                    return std::nullopt;
                out.insert(name, *child);
                break;
            }
            case 0x01: {
                const std::optional<QByteArray> value = text();
                if (!value)
                    return std::nullopt;
                out.insert(name, QString::fromUtf8(*value));
                break;
            }
            case 0x02:
                if (m_pos + 4 > m_data.size())
                    return std::nullopt;
                out.insert(name, qFromLittleEndian<quint32>(m_data.constData() + m_pos));
                m_pos += 4;
                break;
            case 0x03:
            case 0x04:
            case 0x06:
                if (m_pos + 4 > m_data.size())
                    return std::nullopt;
                m_pos += 4;
                break;
            case 0x07:
            case 0x0a:
                if (m_pos + 8 > m_data.size())
                    return std::nullopt;
                m_pos += 8;
                break;
            default:
                return std::nullopt;
            }
        }
    }

private:
    std::optional<QByteArray> text()
    {
        const qsizetype end = m_data.indexOf('\0', m_pos);
        if (end < 0)
            return std::nullopt;
        const QByteArray out = m_data.mid(m_pos, end - m_pos);
        m_pos = end + 1;
        return out;
    }

    std::optional<QByteArray> indexed()
    {
        if (m_pos + 4 > m_data.size())
            return std::nullopt;
        const quint32 index = qFromLittleEndian<quint32>(m_data.constData() + m_pos);
        m_pos += 4;
        if (index >= quint32(m_names->size()))
            return std::nullopt;
        return m_names->at(index);
    }

    const QByteArray &m_data;
    const QByteArrayList *m_names;
    int m_maxDepth;
    qsizetype m_pos = 0;
};

bool validName(const QString &name)
{
    return !name.isEmpty() && name.size() <= SteamLibrary::MaxName;
}

QHash<QString, QString> appTypes(const QString &path, const QSet<QString> &appids)
{
    const std::optional<QByteArray> file = appids.isEmpty() ? std::nullopt : readFile(path, MaxAppInfo);
    if (!file || file->size() < AppInfoHead || qFromLittleEndian<quint32>(file->constData()) != AppInfoMagic)
        return {};
    const QByteArray &data = *file;
    const qint64 table = qFromLittleEndian<qint64>(data.constData() + 8);
    if (table < AppInfoHead || table > data.size() - 4)
        return {};
    const quint32 count = qFromLittleEndian<quint32>(data.constData() + table);
    if (count > MaxNames || count > data.size() - table)
        return {};
    QByteArrayList names;
    for (qsizetype at = table + 4; names.size() < qsizetype(count);) {
        const qsizetype end = data.indexOf('\0', at);
        if (end < 0)
            return {};
        names << data.mid(at, end - at);
        at = end + 1;
    }
    QHash<QString, QString> out;
    for (qsizetype at = AppInfoHead;;) {
        if (at + 4 > table)
            return {};
        const quint32 appid = qFromLittleEndian<quint32>(data.constData() + at);
        if (appid == 0)
            return out;
        if (at + 8 > table)
            return {};
        const quint32 size = qFromLittleEndian<quint32>(data.constData() + at + 4);
        const qsizetype start = at + 8;
        if (size < AppHeader || qint64(size) > table - start)
            return {};
        at = start + size;
        const QString id = QString::number(appid);
        if (!appids.contains(id))
            continue;
        const QByteArray section = QByteArray::fromRawData(data.constData() + start + AppHeader, size - AppHeader);
        BinaryReader reader(section, &names, AppInfoDepth);
        const std::optional<QVariantMap> root = reader.node(0);
        if (!root || reader.pos() != section.size())
            continue;
        const QVariant type = root->value(u"appinfo"_s).toMap().value(u"common"_s).toMap().value(u"type"_s);
        if (type.typeId() == QMetaType::QString)
            out.insert(id, type.toString());
    }
}

}

SteamLibrary::SteamLibrary(const QString &root)
    : m_root(root)
{
}

QStringList SteamLibrary::excluded()
{
    return Excluded;
}

QString SteamLibrary::defaultRoot()
{
    return qEnvironmentVariable("MICROSET_STEAM_DIR", QDir::homePath() + u"/.local/share/Steam"_s);
}

std::optional<QVariantMap> SteamLibrary::parseText(const QByteArray &data)
{
    return TextReader(data).node(0);
}

std::optional<QVariantMap> SteamLibrary::parseBinary(const QByteArray &data)
{
    BinaryReader reader(data);
    return reader.node(0);
}

QStringList SteamLibrary::libraries() const
{
    QStringList paths{m_root};
    const std::optional<QByteArray> data = readFile(m_root + u"/steamapps/libraryfolders.vdf"_s);
    const std::optional<QVariantMap> vdf = data ? parseText(*data) : std::nullopt;
    if (vdf) {
        for (const QVariant &folder : vdf->value(u"libraryfolders"_s).toMap()) {
            const QString path = folder.toMap().value(u"path"_s).toString();
            if (path.startsWith(u'/'))
                paths << path;
        }
    }
    QStringList out;
    QSet<QString> seen;
    for (const QString &path : std::as_const(paths)) {
        const QString canonical = QFileInfo(path).canonicalFilePath();
        const QString key = canonical.isEmpty() ? QDir::cleanPath(path) : canonical;
        if (!seen.contains(key)) {
            seen.insert(key);
            out << path;
        }
    }
    return out;
}

std::optional<SteamLibrary::Game> SteamLibrary::manifest(const QString &library, const QString &appid) const
{
    const QString steamapps = library + u"/steamapps"_s;
    const std::optional<QByteArray> data = readFile(steamapps + u"/appmanifest_"_s + appid + u".acf"_s);
    const std::optional<QVariantMap> vdf = data ? parseText(*data) : std::nullopt;
    if (!vdf)
        return std::nullopt;
    const QVariantMap state = vdf->value(u"appstate"_s).toMap();
    const QString name = state.value(u"name"_s).toString();
    if (state.value(u"appid"_s).toString() != appid || !validName(name))
        return std::nullopt;
    const QString dir = state.value(u"installdir"_s).toString();
    if (dir.isEmpty() || dir.contains(u'/') || dir == "."_L1 || dir == ".."_L1)
        return std::nullopt;
    const QFileInfo tool(steamapps + u"/common/"_s + dir + u"/toolmanifest.vdf"_s);
    if (tool.exists() || tool.isSymLink())
        return std::nullopt;
    return Game{appid, name};
}

QList<SteamLibrary::Game> SteamLibrary::shortcuts() const
{
    QList<Game> out;
    const QString userdata = m_root + u"/userdata"_s;
    for (const QString &user : QDir(userdata).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (!User.match(user).hasMatch())
            continue;
        const std::optional<QByteArray> data = readFile(userdata + u'/' + user + u"/config/shortcuts.vdf"_s);
        const std::optional<QVariantMap> vdf = data ? parseBinary(*data) : std::nullopt;
        if (!vdf)
            continue;
        for (const QVariant &item : vdf->value(u"shortcuts"_s).toMap()) {
            const QVariantMap entry = item.toMap();
            const QVariant id = entry.value(u"appid"_s);
            const QString appid = id.typeId() == QMetaType::UInt ? QString::number(id.toUInt()) : QString();
            const QString name = entry.value(u"appname"_s).toString();
            if (AppId.match(appid).hasMatch() && validName(name))
                out << Game{appid, name};
        }
    }
    return out;
}

QList<SteamLibrary::Game> SteamLibrary::games(QSet<QString> *dropped) const
{
    QList<Game> out;
    QSet<QString> seen;
    int examined = 0;
    for (const QString &library : libraries()) {
        const QStringList files = QDir(library + u"/steamapps"_s)
                                      .entryList({u"appmanifest_*.acf"_s}, QDir::Files | QDir::System | QDir::Hidden,
                                                 QDir::Name);
        for (const QString &file : files) {
            const QString appid = Manifest.match(file).captured(1);
            if (appid.isEmpty() || seen.contains(appid) || Excluded.contains(appid))
                continue;
            if (examined >= MaxApps)
                break;
            ++examined;
            if (const std::optional<Game> game = manifest(library, appid)) {
                seen.insert(appid);
                out << *game;
            }
        }
    }
    if (dropped)
        dropped->clear();
    const QHash<QString, QString> types = appTypes(m_root + u"/appcache/appinfo.vdf"_s, seen);
    out.removeIf([&types, dropped](const Game &game) {
        if (!NonGames.contains(types.value(game.appid), Qt::CaseInsensitive))
            return false;
        if (dropped)
            dropped->insert(game.appid);
        return true;
    });
    for (const Game &game : shortcuts()) {
        if (seen.contains(game.appid) || seen.size() >= MaxApps)
            continue;
        seen.insert(game.appid);
        out << game;
    }
    std::stable_sort(out.begin(), out.end(), [](const Game &a, const Game &b) {
        const int order = a.name.compare(b.name, Qt::CaseInsensitive);
        return order != 0 ? order < 0 : a.appid.toULongLong() < b.appid.toULongLong();
    });
    return out;
}

QString SteamLibrary::name(const QString &appid) const
{
    if (!AppId.match(appid).hasMatch())
        return {};
    for (const QString &library : libraries())
        if (const std::optional<Game> game = manifest(library, appid))
            return game->name;
    for (const Game &game : shortcuts())
        if (game.appid == appid)
            return game.name;
    return {};
}

QByteArray SteamLibrary::stamp() const
{
    QByteArray out;
    const auto add = [&out](const QString &path, bool link = false) {
        const QByteArray name = QFile::encodeName(path);
        struct stat st;
        out += name;
        if ((link ? ::lstat(name.constData(), &st) : ::stat(name.constData(), &st)) == 0)
            out += ' ' + QByteArray::number(qulonglong(st.st_ino)) + ' ' + QByteArray::number(qlonglong(st.st_size))
                + ' ' + QByteArray::number(qlonglong(st.st_mtim.tv_sec)) + '.'
                + QByteArray::number(qlonglong(st.st_mtim.tv_nsec));
        out += '\0';
    };
    add(m_root + u"/steamapps/libraryfolders.vdf"_s);
    add(m_root + u"/appcache/appinfo.vdf"_s);
    int manifests = 0;
    int dirs = 0;
    for (const QString &library : libraries()) {
        const QString steamapps = library + u"/steamapps"_s;
        add(steamapps);
        const QStringList files =
            QDir(steamapps).entryList({u"appmanifest_*.acf"_s}, QDir::Files | QDir::System | QDir::Hidden, QDir::Name);
        for (const QString &file : files) {
            const QString appid = Manifest.match(file).captured(1);
            if (!appid.isEmpty() && !Excluded.contains(appid) && manifests++ < MaxApps)
                add(steamapps + u'/' + file);
        }
        const QString common = steamapps + u"/common/"_s;
        const QStringList installs =
            QDir(common).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System | QDir::Hidden, QDir::Name);
        for (const QString &dir : installs)
            if (dirs++ < MaxApps)
                add(common + dir + u"/toolmanifest.vdf"_s, true);
    }
    const QString userdata = m_root + u"/userdata"_s;
    add(userdata);
    for (const QString &user : QDir(userdata).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
        if (User.match(user).hasMatch())
            add(userdata + u'/' + user + u"/config/shortcuts.vdf"_s);
    return out;
}
