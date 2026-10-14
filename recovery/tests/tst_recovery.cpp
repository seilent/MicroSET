#include "recovery.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

class RecoveryTest : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QByteArray m_path;

    QString path(const QString &rel) const { return m_dir.filePath(rel); }

    void write(const QString &rel, const QByteArray &data, bool exec = false)
    {
        QDir().mkpath(QFileInfo(path(rel)).absolutePath());
        QFile f(path(rel));
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(data);
        f.close();
        if (exec)
            f.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    }

    QString calls()
    {
        QFile f(path(u"calls"_s));
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
    }

    void stubs(const QByteArray &systemctlExit)
    {
        write(u"bin/microos-session"_s,
              "#!/bin/sh\n"
              "case $1 in\n"
              "list) printf 'steam\\tSteam\\tyes\\nmicrodap\\tMicroDAP\\tyes\\nrecovery\\tRecovery\\tno\\n' ;;\n"
              "get-default) cat \"$STUB_DIR/default\" ;;\n"
              "esac\n", true);
        write(u"bin/journalctl"_s, "#!/bin/sh\necho \"journalctl $*\" >> \"$STUB_DIR/calls\"\nseq 1 50\n", true);
        write(u"bin/systemctl"_s,
              "#!/bin/sh\necho \"systemctl $*\" >> \"$STUB_DIR/calls\"\n"
              "if [ \"$1\" = --user ]; then cat \"$STUB_DIR/invocation\" 2>/dev/null; exit 0; fi\n"
              "if [ \"$1\" = show ]; then\n"
              "  if [ -f \"$STUB_DIR/started\" ]; then cat \"$STUB_DIR/after\"; else printf 'ActiveState=inactive\\nResult=exit-code\\nInvocationID=aaaa\\n'; fi\n"
              "  exit 0\n"
              "fi\n"
              "[ " + systemctlExit + " = 0 ] && touch \"$STUB_DIR/started\"\n"
              "echo 'Access denied' >&2\nexit "
                  + systemctlExit + "\n", true);
        QFile::remove(path(u"calls"_s));
        QFile::remove(path(u"started"_s));
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        m_path = qgetenv("PATH");
        qputenv("PATH", path(u"bin"_s).toUtf8() + ':' + m_path);
        qputenv("STUB_DIR", m_dir.path().toUtf8());
        write(u"root/usr/lib/microos/sessions.d/steam.conf"_s, "LABEL=Steam\nUNIT=gamescope-session-plus@steam.service\n");
        write(u"root/usr/lib/microos/sessions.d/microdap.conf"_s,
              "LABEL=MicroDAP\nUNIT=\"gamescope-session-plus@microdap.service\"\n");
    }

    void cleanupTestCase()
    {
        qputenv("PATH", m_path);
    }

    void keyValues()
    {
        const auto kv = parseKeyValues(u"booted=abc.0\n# rollback=yes\nconfirmed = no\nrollback='yes'\n=x\n"_s);
        QCOMPARE(kv.value(u"booted"_s), u"abc.0"_s);
        QCOMPARE(kv.value(u"confirmed"_s), u"no"_s);
        QCOMPARE(kv.value(u"rollback"_s), u"yes"_s);
        QCOMPARE(kv.size(), 3);
    }

    void sessionList()
    {
        const auto list = parseSessionList(u"steam\tSteam\tyes\nBad Name\tX\tyes\nshort\tS\nrecovery\tRecovery\tno\n"_s);
        QCOMPARE(list.size(), 2);
        QCOMPARE(list.at(0).label, u"Steam"_s);
        QVERIFY(list.at(0).defaultOk);
        QVERIFY(!list.at(1).defaultOk);
    }

    void current()
    {
        QCOMPARE(sessionLine(u"steam gamescope-session-plus@steam.service 7\n"_s),
                 (QStringList{u"steam"_s, u"gamescope-session-plus@steam.service"_s, u"7"_s, u"fallback"_s}));
        QCOMPARE(sessionLine(u"steam gamescope-session-plus@steam.service 7 requested\n"_s).value(3), u"requested"_s);
        QCOMPARE(sessionLine(u"steam gamescope-session-plus@steam.service 7 fallback"_s).value(3), u"fallback"_s);
        QVERIFY(sessionLine(u"steam gamescope-session-plus@steam.service 7 other"_s).isEmpty());
        QVERIFY(sessionLine(u"steam gamescope-session-plus@steam.service 7 fallback x"_s).isEmpty());
        QVERIFY(sessionLine(u"steam -x.service 7"_s).isEmpty());
        QVERIFY(sessionLine(u"../steam a.service 7"_s).isEmpty());
        QVERIFY(sessionLine(u"steam a.service"_s).isEmpty());
        QVERIFY(sessionLine({}).isEmpty());
    }

    void journalTail()
    {
        QCOMPARE(journalLines(u"a\nb\nc\n\n"_s, 2), (QStringList{u"b"_s, u"c"_s}));
        QCOMPARE(journalLines(u""_s, 40), QStringList());
        QCOMPARE(journalLines(u"  x\n"_s, 40), QStringList{u"  x"_s});
    }

    void actionsSteamFailed()
    {
        const QVariantList a = actionList(u"steam"_s, u"Steam"_s, u"Steam"_s, false);
        QCOMPARE(a.size(), 2);
        QCOMPARE(a.at(0).toMap().value(u"title"_s).toString(), u"try Steam again"_s);
        QCOMPARE(a.at(0).toMap().value(u"name"_s).toString(), u"steam"_s);
        QCOMPARE(a.at(1).toMap().value(u"key"_s).toString(), u"poweroff"_s);
    }

    void actionsOtherFailed()
    {
        const QVariantList a = actionList(u"microdap"_s, u"MicroDAP"_s, u"Steam"_s, true);
        QStringList keys;
        for (const QVariant &v : a)
            keys << v.toMap().value(u"key"_s).toString() + u':' + v.toMap().value(u"name"_s).toString();
        QCOMPARE(keys, (QStringList{u"switch:microdap"_s, u"switch:steam"_s, u"rollback:"_s, u"poweroff:"_s}));
        QCOMPARE(a.at(0).toMap().value(u"title"_s).toString(), u"try MicroDAP again"_s);
        QCOMPARE(a.at(1).toMap().value(u"title"_s).toString(), u"Steam"_s);
    }

    void loadPrevious()
    {
        stubs("0");
        write(u"default"_s, "steam\n");
        write(u"invocation"_s, "0123456789abcdef0123456789abcdef\n");
        write(u"root/run/microos/session-previous"_s, "microdap gamescope-session-plus@microdap.service 4 fallback\n");
        write(u"root/run/microos/deployments"_s, "booted=abc.0\nconfirmed=no\nrollback=yes\n");
        Recovery r(path(u"root"_s));
        QSignalSpy journal(&r, &Recovery::journalChanged);
        r.load();
        QVERIFY(r.failed());
        QCOMPARE(r.failingName(), u"microdap"_s);
        QVERIFY(journal.wait(5000));
        QTRY_COMPARE(r.failingLabel(), u"MicroDAP"_s);
        QCOMPARE(r.failingUnit(), u"gamescope-session-plus@microdap.service"_s);
        QVERIFY(r.rollbackAvailable());
        QCOMPARE(r.journal().size(), Recovery::journalLimit);
        QCOMPARE(r.journal().last(), u"50"_s);
        QCOMPARE(r.actions().size(), 4);
        QCOMPARE(r.actions().at(0).toMap().value(u"title"_s).toString(), u"try MicroDAP again"_s);
        QVERIFY(calls().contains(u"systemctl --user show -p InvocationID --value gamescope-session-plus@microdap.service"_s));
        QVERIFY(calls().contains(u"journalctl --user -b -u gamescope-session-plus@microdap.service "
                                 "_SYSTEMD_INVOCATION_ID=0123456789abcdef0123456789abcdef -n 40 -o cat"_s));
    }

    void loadRequested()
    {
        stubs("0");
        write(u"default"_s, "steam\n");
        write(u"root/run/microos/session-previous"_s, "microdap gamescope-session-plus@microdap.service 4 requested\n");
        Recovery r(path(u"root"_s));
        QSignalSpy journal(&r, &Recovery::journalChanged);
        r.load();
        QVERIFY(!r.failed());
        QCOMPARE(r.failingName(), u"microdap"_s);
        QCOMPARE(r.failingUnit(), u"gamescope-session-plus@microdap.service"_s);
        QCOMPARE(r.actions().at(0).toMap().value(u"name"_s).toString(), u"microdap"_s);
        QVERIFY(journal.wait(5000));
        QVERIFY(calls().contains(u"-u gamescope-session-plus@microdap.service"_s));
    }

    void loadThreeFields()
    {
        stubs("0");
        write(u"default"_s, "steam\n");
        write(u"root/run/microos/session-previous"_s, "microdap gamescope-session-plus@microdap.service 4\n");
        Recovery r(path(u"root"_s));
        r.load();
        QVERIFY(r.failed());
        QCOMPARE(r.failingName(), u"microdap"_s);
    }

    void loadWithoutPrevious()
    {
        stubs("0");
        write(u"default"_s, "recovery\n");
        QFile::remove(path(u"invocation"_s));
        QFile::remove(path(u"root/run/microos/session-previous"_s));
        write(u"root/run/microos/deployments"_s, "rollback=no\n");
        Recovery r(path(u"root"_s));
        QSignalSpy journal(&r, &Recovery::journalChanged);
        r.load();
        QVERIFY(!r.failed());
        QCOMPARE(r.actions().size(), 1);
        QVERIFY(journal.wait(5000));
        QCOMPARE(r.defaultName(), u"steam"_s);
        QCOMPARE(r.failingName(), u"steam"_s);
        QCOMPARE(r.actions().size(), 2);
        QCOMPARE(r.actions().at(0).toMap().value(u"name"_s).toString(), u"steam"_s);
        QCOMPARE(r.failingUnit(), u"gamescope-session-plus@steam.service"_s);
        QVERIFY(!r.rollbackAvailable());
        QVERIFY(calls().contains(u"journalctl --user -b -u gamescope-session-plus@steam.service -n 40 -o cat"_s));
    }

    void invocationArgs()
    {
        const QString u = u"a.service"_s;
        QVERIFY(!journalArgs(u, u"xyz"_s, 40).join(u' ').contains(u"_SYSTEMD_INVOCATION_ID"_s));
        QVERIFY(!journalArgs(u, u"0123456789abcdef0123456789abcdeF"_s, 40).join(u' ').contains(u"INVOCATION"_s));
        QCOMPARE(journalArgs(u, {}, 40),
                 (QStringList{u"--user"_s, u"-b"_s, u"-u"_s, u, u"-n"_s, u"40"_s, u"-o"_s, u"cat"_s, u"--no-pager"_s}));
    }

    void defaultFailedWithoutPrevious()
    {
        stubs("0");
        write(u"default"_s, "microdap\n");
        QFile::remove(path(u"root/run/microos/session-previous"_s));
        Recovery r(path(u"root"_s));
        QSignalSpy journal(&r, &Recovery::journalChanged);
        r.load();
        QVERIFY(journal.wait(5000));
        QCOMPARE(r.failingName(), u"microdap"_s);
        QCOMPARE(r.failingUnit(), u"gamescope-session-plus@microdap.service"_s);
        QCOMPARE(r.actions().at(0).toMap().value(u"title"_s).toString(), u"try MicroDAP again"_s);
        QCOMPARE(r.actions().at(1).toMap().value(u"name"_s).toString(), u"steam"_s);
    }

    void loadDoesNotBlock()
    {
        stubs("0");
        write(u"bin/microos-session"_s, "#!/bin/sh\nsleep 3\necho steam\n", true);
        QFile::remove(path(u"root/run/microos/session-previous"_s));
        Recovery r(path(u"root"_s));
        QSignalSpy journal(&r, &Recovery::journalChanged);
        QElapsedTimer t;
        t.start();
        r.load();
        QVERIFY(t.elapsed() < 500);
        QCOMPARE(r.actions().size(), 1);
        QVERIFY(journal.wait(6000));
        QCOMPARE(r.failingName(), u"steam"_s);
    }

    void noDeploymentsFile()
    {
        stubs("0");
        write(u"default"_s, "steam\n");
        QFile::remove(path(u"root/run/microos/deployments"_s));
        Recovery r(path(u"root"_s));
        r.load();
        QVERIFY(!r.rollbackAvailable());
    }

    void switchStartsUnit()
    {
        stubs("0");
        Recovery r(path(u"root"_s));
        QSignalSpy failed(&r, &Recovery::actionFailed);
        r.switchTo(u"steam"_s, u"steam"_s);
        QTRY_VERIFY(calls().contains(u"systemctl start --no-block microos-session-switch@steam.service"_s));
        QTest::qWait(200);
        QCOMPARE(failed.size(), 0);
    }

    void switchRefusesBadName()
    {
        stubs("0");
        Recovery r(path(u"root"_s));
        QSignalSpy failed(&r, &Recovery::actionFailed);
        r.switchTo(u"../x"_s, u"x"_s);
        QCOMPARE(failed.size(), 1);
        QVERIFY(calls().isEmpty());
    }

    void rollbackRefusedToast()
    {
        stubs("0");
        write(u"after"_s, "ActiveState=failed\nResult=exit-code\nInvocationID=bbbb\n");
        Recovery r(path(u"root"_s));
        QSignalSpy failed(&r, &Recovery::actionFailed);
        r.bootPrevious(u"boot the previous image"_s);
        QVERIFY(failed.wait(5000));
        QCOMPARE(failed.at(0).at(1).toString(), u"Refused, hold A until the reboot"_s);
        const QString c = calls();
        QVERIFY(c.indexOf(u"show -p InvocationID microos-session-rollback.service"_s)
                < c.indexOf(u"start --no-block microos-session-rollback.service"_s));
    }

    void rollbackRunningThenSuccess()
    {
        stubs("0");
        write(u"after"_s, "ActiveState=activating\nResult=success\nInvocationID=bbbb\n");
        Recovery r(path(u"root"_s));
        QSignalSpy failed(&r, &Recovery::actionFailed);
        r.bootPrevious(u"boot the previous image"_s);
        QTRY_VERIFY(calls().count(u"-p ActiveState"_s) >= 2);
        write(u"after"_s, "ActiveState=inactive\nResult=success\nInvocationID=bbbb\n");
        const int polls = calls().count(u"-p ActiveState"_s);
        QTRY_VERIFY(calls().count(u"-p ActiveState"_s) > polls);
        QTest::qWait(800);
        const int settled = calls().count(u"-p ActiveState"_s);
        QTest::qWait(600);
        QCOMPARE(calls().count(u"-p ActiveState"_s), settled);
        QCOMPARE(failed.size(), 0);
    }

    void rollbackStaleResultIgnored()
    {
        stubs("0");
        write(u"after"_s, "ActiveState=inactive\nResult=exit-code\nInvocationID=aaaa\n");
        Recovery r(path(u"root"_s));
        QSignalSpy failed(&r, &Recovery::actionFailed);
        r.bootPrevious(u"boot the previous image"_s);
        QTRY_VERIFY(calls().count(u"-p ActiveState"_s) >= 3);
        QCOMPARE(failed.size(), 0);
        write(u"after"_s, "ActiveState=failed\nResult=exit-code\nInvocationID=cccc\n");
        QVERIFY(failed.wait(5000));
    }

    void rollbackFailureReported()
    {
        stubs("4");
        Recovery r(path(u"root"_s));
        QSignalSpy failed(&r, &Recovery::actionFailed);
        r.bootPrevious(u"boot the previous image"_s);
        QVERIFY(failed.wait(5000));
        QCOMPARE(failed.at(0).at(0).toString(), u"boot the previous image"_s);
        QCOMPARE(failed.at(0).at(1).toString(), u"Access denied"_s);
        QVERIFY(calls().contains(u"systemctl start --no-block microos-session-rollback.service"_s));
    }
};

QTEST_GUILESS_MAIN(RecoveryTest)
#include "tst_recovery.moc"
