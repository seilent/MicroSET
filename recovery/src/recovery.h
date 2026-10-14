#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QStringList>
#include <QVariantList>

#include <functional>

class QProcess;

struct SessionEntry
{
    QString name;
    QString label;
    bool defaultOk = false;
};

QHash<QString, QString> parseKeyValues(const QString &text);
QList<SessionEntry> parseSessionList(const QString &text);
QStringList sessionLine(const QString &text);
QStringList journalLines(const QString &text, int limit);
bool validName(const QString &name);
bool validUnit(const QString &unit);
bool validInvocation(const QString &id);
QStringList journalArgs(const QString &unit, const QString &invocation, int limit);
QVariantList actionList(const QString &failingName, const QString &failingLabel, const QString &steamLabel, bool rollback);

class Recovery : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString failingName READ failingName NOTIFY changed)
    Q_PROPERTY(QString failingLabel READ failingLabel NOTIFY changed)
    Q_PROPERTY(QString failingUnit READ failingUnit NOTIFY changed)
    Q_PROPERTY(bool failed READ failed NOTIFY changed)
    Q_PROPERTY(QString defaultName READ defaultName NOTIFY changed)
    Q_PROPERTY(QString steamLabel READ steamLabel NOTIFY changed)
    Q_PROPERTY(bool rollbackAvailable READ rollbackAvailable NOTIFY changed)
    Q_PROPERTY(QVariantList actions READ actions NOTIFY changed)
    Q_PROPERTY(QStringList journal READ journal NOTIFY journalChanged)
    Q_PROPERTY(bool journalLoaded READ journalLoaded NOTIFY journalChanged)

public:
    static constexpr int journalLimit = 40;

    explicit Recovery(const QString &root = {}, QObject *parent = nullptr);

    void load();
    void loadJournal();
    void setFixture(const QString &failingName, const QString &failingLabel, const QString &failingUnit, bool failed,
                    const QString &defaultName, bool rollback, const QStringList &journal);

    QString failingName() const { return m_failingName; }
    QString failingLabel() const { return m_failingLabel; }
    QString failingUnit() const { return m_failingUnit; }
    bool failed() const { return m_failed; }
    QString defaultName() const { return m_defaultName; }
    QString steamLabel() const { return m_steamLabel; }
    bool rollbackAvailable() const { return m_rollback; }
    QVariantList actions() const { return actionList(m_failingName, m_failingLabel, m_steamLabel, m_rollback); }
    QStringList journal() const { return m_journal; }
    bool journalLoaded() const { return m_journalLoaded; }

    Q_INVOKABLE void switchTo(const QString &name, const QString &action);
    Q_INVOKABLE void bootPrevious(const QString &action);
    Q_INVOKABLE void powerOff(const QString &action);

signals:
    void changed();
    void journalChanged();
    void actionFailed(const QString &action, const QString &message);

private:
    QString readFile(const QString &path) const;
    void runAsync(const QString &program, const QStringList &args, std::function<void(const QString &)> done);
    QString labelOf(const QString &name) const;
    void readInvocation();
    QString unitOf(const QString &name) const;
    void startUnit(const QString &unit, const QString &action, std::function<void()> started = {});
    void watchRollback(const QString &unit, const QString &action, const QString &before, QElapsedTimer clock);

    QString m_root;
    QList<SessionEntry> m_sessions;
    bool m_known = false;
    QString m_failingName;
    QString m_failingLabel;
    QString m_failingUnit;
    QString m_invocation;
    bool m_failed = false;
    QString m_defaultName;
    QString m_steamLabel;
    bool m_rollback = false;
    QStringList m_journal;
    bool m_journalLoaded = false;
};
