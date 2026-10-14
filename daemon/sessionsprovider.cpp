#include "sessionsprovider.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>

using namespace Qt::StringLiterals;

namespace {

QHash<QString, QString> parse(const QString &path)
{
    QHash<QString, QString> out;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return out;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        const qsizetype eq = line.indexOf(u'=');
        if (line.startsWith(u'#') || eq <= 0)
            continue;
        QString value = line.mid(eq + 1).trimmed();
        if (value.size() >= 2 && (value.front() == u'"' || value.front() == u'\'') && value.back() == value.front())
            value = value.mid(1, value.size() - 2);
        out.insert(line.left(eq).trimmed(), value);
    }
    return out;
}

bool plainName(const QString &name, const QString &suffix)
{
    return name.endsWith(suffix) && name.size() > suffix.size() && !name.contains(u'/') && !name.startsWith(u'.');
}

}

SessionsProvider::SessionsProvider(const QString &root)
    : m_root(root)
{
}

bool SessionsProvider::desktopExists(const QString &desktop) const
{
    return plainName(desktop, u".desktop"_s) && QFileInfo(m_root + u"/usr/share/wayland-sessions/"_s + desktop).isFile();
}

bool SessionsProvider::unitExists(const QString &unit) const
{
    if (!plainName(unit, u".service"_s))
        return false;
    const QString dir = m_root + u"/usr/lib/systemd/user/"_s;
    if (QFileInfo(dir + unit).isFile())
        return true;
    const qsizetype at = unit.indexOf(u'@');
    return at > 0 && at + 1 < unit.size() - 8 && QFileInfo(dir + unit.left(at + 1) + u".service"_s).isFile();
}

QStringList SessionsProvider::defaults() const
{
    static const QRegularExpression namePattern(u"^[a-z0-9][a-z0-9_-]*$"_s);
    QStringList out;
    const QDir dir(m_root + u"/usr/lib/microos/sessions.d"_s);
    for (const QFileInfo &info : dir.entryInfoList({u"*.conf"_s}, QDir::Files, QDir::Name)) {
        const QString name = info.completeBaseName();
        if (!namePattern.match(name).hasMatch())
            continue;
        const QHash<QString, QString> conf = parse(info.filePath());
        if (conf.value(u"DEFAULT"_s) == "yes"_L1 && desktopExists(conf.value(u"DESKTOP"_s))
            && unitExists(conf.value(u"UNIT"_s)))
            out << name;
    }
    return out;
}
