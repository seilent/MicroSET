#include "protonprofiles.h"

#include <QFile>
#include <QJsonDocument>
#include <QRegularExpression>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace Qt::StringLiterals;

namespace {

constexpr qint64 MaxSize = 64 * 1024;
const QStringList EntryFields = {u"tool"_s, u"set"_s};
const QStringList Switches = {u"fex.preset"_s, u"profile"_s};

QByteArray readFile(const QString &path, QString *why)
{
    const int fd = ::open(QFile::encodeName(path).constData(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        *why = errno == ELOOP ? u"a symlink"_s : QString::fromLocal8Bit(strerror(errno));
        return {};
    }
    struct stat st;
    QByteArray data;
    if (::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode))
        *why = u"not a regular file"_s;
    else if (st.st_size > MaxSize)
        *why = u"too large"_s;
    while (why->isEmpty()) {
        char buffer[16384];
        const ssize_t n = ::read(fd, buffer, sizeof buffer);
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0)
            *why = QString::fromLocal8Bit(strerror(errno));
        else if (data.size() + n > MaxSize)
            *why = u"too large"_s;
        if (n <= 0 || !why->isEmpty())
            break;
        data.append(buffer, n);
    }
    ::close(fd);
    return why->isEmpty() ? data : QByteArray();
}

QString check(const QString &appid, const QJsonValue &value, const Schema &schema, ProtonProfiles::Entry *entry)
{
    static const QRegularExpression toolPattern(u"^[A-Za-z0-9][A-Za-z0-9._+-]{0,63}$"_s);
    if (!Schema::isAppId(appid))
        return u"not an appid"_s;
    const QJsonObject object = value.toObject();
    const QStringList fields = object.keys();
    if (!value.isObject()
        || std::any_of(fields.cbegin(), fields.cend(),
                       [](const QString &field) { return !EntryFields.contains(field); }))
        return u"not an object of tool and set"_s;
    const QJsonValue tool = object.value("tool"_L1);
    if (!tool.isUndefined() && (!tool.isString() || !toolPattern.match(tool.toString()).hasMatch()))
        return u"bad tool"_s;
    entry->tool = tool.toString();
    const QJsonObject set = object.value("set"_L1).toObject();
    if (set.isEmpty())
        return u"no set"_s;
    for (auto it = set.begin(); it != set.end(); ++it) {
        const Key *key = Switches.contains(it.key()) ? nullptr : schema.perApp(it.key());
        if (!key)
            return u"%1 is not a game option"_s.arg(it.key().left(64));
        const QString text = it.value().toString();
        if (!it.value().isString() || !schema.accepts(*key, text) || key->defaultValue == text)
            return u"bad value for %1"_s.arg(it.key().left(64));
        entry->set.insert(it.key(), text);
    }
    return {};
}

}

bool ProtonProfiles::load(const QString &path, const Schema &schema)
{
    m_entries.clear();
    QString why;
    const QByteArray data = readFile(path, &why);
    QJsonParseError error{};
    const QJsonDocument document = why.isEmpty() ? QJsonDocument::fromJson(data, &error) : QJsonDocument();
    const QJsonObject root = document.object();
    if (why.isEmpty() && error.error != QJsonParseError::NoError)
        why = error.errorString();
    else if (why.isEmpty()
             && (!document.isObject() || root.value("version"_L1) != QJsonValue(1)
                 || !root.value("profiles"_L1).isObject()))
        why = u"not a version 1 profiles file"_s;
    if (!why.isEmpty()) {
        qWarning("%s: %s, no profiles", qPrintable(path), qPrintable(why));
        return false;
    }
    bool clean = true;
    const QJsonObject profiles = root.value("profiles"_L1).toObject();
    for (auto it = profiles.begin(); it != profiles.end(); ++it) {
        Entry entry;
        const QString dropped = check(it.key(), it.value(), schema, &entry);
        if (!dropped.isEmpty()) {
            qWarning("%s: profile %s: %s, dropped", qPrintable(path), qPrintable(it.key().left(32)),
                     qPrintable(dropped));
            clean = false;
            continue;
        }
        m_entries.insert(it.key(), entry);
    }
    qInfo("%s: %lld profiles", qPrintable(path), qlonglong(m_entries.size()));
    return clean;
}

const ProtonProfiles::Entry *ProtonProfiles::entry(const QString &appid) const
{
    const auto it = m_entries.constFind(appid);
    return it == m_entries.cend() ? nullptr : &*it;
}

QStringList ProtonProfiles::apps() const
{
    return m_entries.keys();
}

QJsonObject ProtonProfiles::toJson() const
{
    QJsonObject out;
    for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it) {
        QJsonObject set;
        for (auto option = it->set.cbegin(); option != it->set.cend(); ++option)
            set.insert(option.key(), option.value());
        QJsonObject entry{{u"set"_s, set}};
        if (!it->tool.isEmpty())
            entry.insert(u"tool"_s, it->tool);
        out.insert(it.key(), entry);
    }
    return out;
}
