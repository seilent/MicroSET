#include "holdhint.h"

#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QRegularExpression>

using namespace Qt::StringLiterals;

HoldHint::HoldHint(const QString &dir, const QString &procRoot, QObject *parent)
    : QObject(parent)
    , m_file(dir + u"/storage"_s)
    , m_proc(procRoot)
{
    if (!m_watcher.addPath(dir))
        qWarning("microset overlay: cannot watch %s", qPrintable(dir));
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &HoldHint::check);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, &HoldHint::check);
    check();
}

bool HoldHint::active() const
{
    return m_active;
}

QString HoldHint::device() const
{
    return m_device;
}

void HoldHint::check()
{
    static const QRegularExpression card(u"^/dev/mmcblk[0-9]$"_s);
    bool now = false;
    QString device;
    QFile file(m_file);
    if (file.open(QIODevice::ReadOnly)) {
        if (!m_watcher.files().contains(m_file))
            m_watcher.addPath(m_file);
        const QList<QByteArray> lines = file.read(64).split('\n');
        bool ok = false;
        const qint64 pid = lines.value(0).trimmed().toLongLong(&ok);
        now = ok && pid > 0 && formatRunning(pid);
        const QString named = QString::fromLatin1(lines.value(1).trimmed());
        if (now && card.match(named).hasMatch())
            device = named;
    }
    if (now == m_active && device == m_device)
        return;
    m_active = now;
    m_device = device;
    emit activeChanged();
}

bool HoldHint::formatRunning(qint64 pid) const
{
    const QString dir = m_proc + u'/' + QString::number(pid);
    if (QFileInfo(dir).ownerId() != 0)
        return false;
    QFile cmdline(dir + u"/cmdline"_s);
    if (!cmdline.open(QIODevice::ReadOnly))
        return false;
    const QList<QByteArray> args = cmdline.read(4096).split('\0');
    for (qsizetype i = 0; i + 1 < args.size(); ++i) {
        if (args.at(i) == Script.toUtf8() && args.at(i + 1) == "format")
            return true;
    }
    return false;
}
