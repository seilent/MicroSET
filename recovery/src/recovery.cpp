#include "recovery.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>

using namespace Qt::StringLiterals;

namespace {

constexpr int commandTimeoutMs = 5000;
constexpr int rollbackPollMs = 250;
constexpr qint64 rollbackWaitMs = 10000;

QString firstLine(const QByteArray &bytes)
{
    const QStringList lines = QString::fromUtf8(bytes).split(u'\n', Qt::SkipEmptyParts);
    return lines.isEmpty() ? QString() : lines.first().trimmed();
}

}

QHash<QString, QString> parseKeyValues(const QString &text)
{
    QHash<QString, QString> out;
    for (const QString &raw : text.split(u'\n')) {
        const QString line = raw.trimmed();
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

QList<SessionEntry> parseSessionList(const QString &text)
{
    QList<SessionEntry> out;
    for (const QString &line : text.split(u'\n', Qt::SkipEmptyParts)) {
        const QStringList fields = line.split(u'\t');
        if (fields.size() < 3 || !validName(fields.at(0)))
            continue;
        out.append({fields.at(0), fields.at(1), fields.at(2) == u"yes"_s});
    }
    return out;
}

QStringList sessionLine(const QString &text)
{
    QStringList fields = text.trimmed().split(u' ', Qt::SkipEmptyParts);
    if (fields.size() < 3 || fields.size() > 4 || !validName(fields.at(0)) || !validUnit(fields.at(1)))
        return {};
    if (fields.size() == 3)
        return fields << u"fallback"_s;
    if (fields.at(3) != u"requested"_s && fields.at(3) != u"fallback"_s)
        return {};
    return fields;
}

QStringList journalLines(const QString &text, int limit)
{
    QStringList lines = text.split(u'\n');
    while (!lines.isEmpty() && lines.last().trimmed().isEmpty())
        lines.removeLast();
    if (lines.size() > limit)
        lines = lines.mid(lines.size() - limit);
    return lines;
}

bool validName(const QString &name)
{
    static const QRegularExpression pattern(u"^[a-z0-9-]+$"_s);
    return pattern.match(name).hasMatch();
}

bool validUnit(const QString &unit)
{
    static const QRegularExpression pattern(u"^[A-Za-z0-9][A-Za-z0-9@._-]*\\.service$"_s);
    return pattern.match(unit).hasMatch();
}

bool validInvocation(const QString &id)
{
    static const QRegularExpression pattern(u"^[0-9a-f]{32}$"_s);
    return pattern.match(id).hasMatch();
}

QStringList journalArgs(const QString &unit, const QString &invocation, int limit)
{
    QStringList args{u"--user"_s, u"-b"_s, u"-u"_s, unit};
    if (validInvocation(invocation))
        args.append(u"_SYSTEMD_INVOCATION_ID="_s + invocation);
    args << u"-n"_s << QString::number(limit) << u"-o"_s << u"cat"_s << u"--no-pager"_s;
    return args;
}

QVariantList actionList(const QString &failingName, const QString &failingLabel, const QString &steamLabel, bool rollback)
{
    const auto action = [](const QString &key, const QString &title, const QString &name) {
        return QVariantMap{{u"key"_s, key}, {u"title"_s, title}, {u"name"_s, name}};
    };
    QVariantList out;
    if (validName(failingName))
        out.append(action(u"switch"_s, u"try %1 again"_s.arg(failingLabel), failingName));
    if (!failingName.isEmpty() && failingName != u"steam"_s)
        out.append(action(u"switch"_s, steamLabel, u"steam"_s));
    if (rollback)
        out.append(action(u"rollback"_s, u"boot the previous image"_s, {}));
    out.append(action(u"poweroff"_s, u"power off"_s, {}));
    return out;
}

Recovery::Recovery(const QString &root, QObject *parent)
    : QObject(parent)
    , m_root(root)
{
}

QString Recovery::readFile(const QString &path) const
{
    QFile file(m_root + path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(file.read(64 * 1024));
}

void Recovery::runAsync(const QString &program, const QStringList &args, std::function<void(const QString &)> done)
{
    auto *process = new QProcess(this);
    auto *timer = new QTimer(process);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, process, [process] { process->kill(); });
    connect(process, &QProcess::finished, this, [process, done](int code, QProcess::ExitStatus status) {
        done(status == QProcess::NormalExit && code == 0 ? QString::fromUtf8(process->readAllStandardOutput()) : QString());
        process->deleteLater();
    });
    connect(process, &QProcess::errorOccurred, this, [process, done](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        done({});
        process->deleteLater();
    });
    timer->start(commandTimeoutMs);
    process->start(program, args);
}

QString Recovery::unitOf(const QString &name) const
{
    if (!validName(name))
        return {};
    const QString unit = parseKeyValues(readFile(u"/usr/lib/microos/sessions.d/"_s + name + u".conf"_s)).value(u"UNIT"_s);
    return validUnit(unit) ? unit : QString();
}

QString Recovery::labelOf(const QString &name) const
{
    for (const SessionEntry &e : m_sessions) {
        if (e.name == name && !e.label.isEmpty())
            return e.label;
    }
    return name;
}

void Recovery::load()
{
    const QStringList previous = sessionLine(readFile(u"/run/microos/session-previous"_s));
    m_known = !previous.isEmpty() && previous.at(0) != u"recovery"_s;
    m_failed = m_known && previous.at(3) == u"fallback"_s;
    m_failingName = m_known ? previous.at(0) : QString();
    m_failingUnit = m_known ? previous.at(1) : QString();
    m_failingLabel = labelOf(m_failingName);
    m_steamLabel = labelOf(u"steam"_s);
    m_rollback = parseKeyValues(readFile(u"/run/microos/deployments"_s)).value(u"rollback"_s) == u"yes"_s;
    emit changed();

    runAsync(u"microos-session"_s, {u"list"_s}, [this](const QString &out) {
        m_sessions = parseSessionList(out);
        m_failingLabel = labelOf(m_failingName);
        m_steamLabel = labelOf(u"steam"_s);
        emit changed();
    });
    runAsync(u"microos-session"_s, {u"get-default"_s}, [this](const QString &out) {
        QString name = out.trimmed();
        if (!validName(name) || name == u"recovery"_s)
            name = u"steam"_s;
        m_defaultName = name;
        if (!m_known) {
            m_failingName = name;
            m_failingLabel = labelOf(name);
            m_failingUnit = unitOf(name);
            emit changed();
            readInvocation();
            return;
        }
        emit changed();
    });
    if (m_known)
        readInvocation();
}

void Recovery::readInvocation()
{
    if (!validUnit(m_failingUnit)) {
        loadJournal();
        return;
    }
    runAsync(u"systemctl"_s, {u"--user"_s, u"show"_s, u"-p"_s, u"InvocationID"_s, u"--value"_s, m_failingUnit},
             [this](const QString &out) {
                 m_invocation = out.trimmed();
                 loadJournal();
             });
}

void Recovery::loadJournal()
{
    if (!validUnit(m_failingUnit)) {
        m_journal.clear();
        m_journalLoaded = true;
        emit journalChanged();
        return;
    }
    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, this, [this, process](int code, QProcess::ExitStatus status) {
        m_journal = status == QProcess::NormalExit && code == 0
            ? journalLines(QString::fromUtf8(process->readAllStandardOutput()), journalLimit)
            : QStringList();
        m_journalLoaded = true;
        process->deleteLater();
        emit journalChanged();
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        m_journal.clear();
        m_journalLoaded = true;
        process->deleteLater();
        emit journalChanged();
    });
    process->start(u"journalctl"_s, journalArgs(m_failingUnit, m_invocation, journalLimit));
}

void Recovery::setFixture(const QString &failingName, const QString &failingLabel, const QString &failingUnit,
                          bool failed, const QString &defaultName, bool rollback,
                          const QStringList &journal)
{
    m_failingName = failingName;
    m_failingLabel = failingLabel;
    m_failingUnit = failingUnit;
    m_failed = failed;
    m_defaultName = defaultName;
    m_steamLabel = u"Steam"_s;
    m_rollback = rollback;
    m_journal = journal;
    m_journalLoaded = true;
    emit changed();
    emit journalChanged();
}

void Recovery::startUnit(const QString &unit, const QString &action, std::function<void()> started)
{
    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, this, [this, process, action, started](int code, QProcess::ExitStatus status) {
        if (status != QProcess::NormalExit || code != 0) {
            const QString message = firstLine(process->readAllStandardError());
            emit actionFailed(action, message.isEmpty() ? u"systemctl exit %1"_s.arg(code) : message);
        } else if (started) {
            started();
        }
        process->deleteLater();
    });
    connect(process, &QProcess::errorOccurred, this, [this, process, action](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        emit actionFailed(action, process->errorString());
        process->deleteLater();
    });
    process->start(u"systemctl"_s, {u"start"_s, u"--no-block"_s, unit});
}

void Recovery::switchTo(const QString &name, const QString &action)
{
    if (!validName(name)) {
        emit actionFailed(action, u"bad session name"_s);
        return;
    }
    startUnit(u"microos-session-switch@"_s + name + u".service"_s, action);
}

void Recovery::bootPrevious(const QString &action)
{
    const QString unit = u"microos-session-rollback.service"_s;
    runAsync(u"systemctl"_s, {u"show"_s, u"-p"_s, u"InvocationID"_s, unit}, [this, unit, action](const QString &out) {
        const QString before = parseKeyValues(out).value(u"InvocationID"_s);
        QElapsedTimer clock;
        clock.start();
        startUnit(unit, action, [this, unit, action, before, clock] { watchRollback(unit, action, before, clock); });
    });
}

void Recovery::watchRollback(const QString &unit, const QString &action, const QString &before, QElapsedTimer clock)
{
    runAsync(u"systemctl"_s, {u"show"_s, u"-p"_s, u"ActiveState"_s, u"-p"_s, u"Result"_s, u"-p"_s, u"InvocationID"_s, unit},
             [this, unit, action, before, clock](const QString &out) {
                 const auto kv = parseKeyValues(out);
                 const QString state = kv.value(u"ActiveState"_s);
                 const bool ran = kv.value(u"InvocationID"_s) != before;
                 if (ran && (state == u"inactive"_s || state == u"failed"_s)) {
                     if (kv.value(u"Result"_s) != u"success"_s)
                         emit actionFailed(action, u"Refused, hold A until the reboot"_s);
                     return;
                 }
                 if (clock.elapsed() >= rollbackWaitMs) {
                     emit actionFailed(action, u"No result from the rollback"_s);
                     return;
                 }
                 QTimer::singleShot(rollbackPollMs, this, [this, unit, action, before, clock] {
                     watchRollback(unit, action, before, clock);
                 });
             });
}

void Recovery::powerOff(const QString &action)
{
    QDBusMessage call = QDBusMessage::createMethodCall(u"org.freedesktop.login1"_s, u"/org/freedesktop/login1"_s,
                                                       u"org.freedesktop.login1.Manager"_s, u"PowerOff"_s);
    call << true;
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, action](QDBusPendingCallWatcher *w) {
        const QDBusPendingReply<> reply = *w;
        if (reply.isError())
            emit actionFailed(action, reply.error().message());
        w->deleteLater();
    });
}
