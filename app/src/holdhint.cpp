#include "holdhint.h"

#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QRegularExpression>

using namespace Qt::StringLiterals;

HoldHint::HoldHint(const QString &dir, const QString &procRoot, QObject *parent)
    : QObject(parent)
    , m_file(dir + u"/storage"_s)
    , m_undervolt(dir + u"/gpu-undervolt"_s)
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

QString HoldHint::kind() const
{
    return m_kind;
}

QList<QByteArray> HoldHint::lines(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    if (!m_watcher.files().contains(path))
        m_watcher.addPath(path);
    return file.read(64).split('\n');
}

void HoldHint::check()
{
    static const QRegularExpression card(u"^/dev/mmcblk[0-9]$"_s);
    bool now = false;
    QString device;
    QString kind;
    const QList<QByteArray> storage = lines(m_file);
    const QList<QByteArray> undervolt = lines(m_undervolt);
    if (running(storage, Script, "format")) {
        now = true;
        kind = u"format"_s;
        const QString named = QString::fromLatin1(storage.value(1).trimmed());
        if (card.match(named).hasMatch())
            device = named;
    } else if (undervolt.value(1).trimmed() == "gpu-undervolt" && running(undervolt, Powerd, {})) {
        now = true;
        kind = u"undervolt"_s;
    }
    if (now == m_active && device == m_device && kind == m_kind)
        return;
    m_active = now;
    m_device = device;
    m_kind = kind;
    emit activeChanged();
}

bool HoldHint::running(const QList<QByteArray> &hint, const QString &script, const QByteArray &verb) const
{
    bool ok = false;
    const qint64 pid = hint.value(0).trimmed().toLongLong(&ok);
    if (!ok || pid <= 0)
        return false;
    const QString dir = m_proc + u'/' + QString::number(pid);
    if (QFileInfo(dir).ownerId() != 0)
        return false;
    QFile cmdline(dir + u"/cmdline"_s);
    if (!cmdline.open(QIODevice::ReadOnly))
        return false;
    const QList<QByteArray> args = cmdline.read(4096).split('\0');
    for (qsizetype i = 0; i + 1 < args.size(); ++i) {
        if (args.at(i) == script.toUtf8() && (verb.isEmpty() || args.at(i + 1) == verb))
            return true;
    }
    return false;
}
