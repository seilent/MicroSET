#include "harness.h"
#include "sessionsprovider.h"

#include <QFile>
#include <QTest>

using namespace Qt::StringLiterals;

class SessionsTest : public QObject
{
    Q_OBJECT

private slots:
    void fixture()
    {
        Harness h;
        h.writeSessions();
        QCOMPARE(SessionsProvider(h.path(u"root"_s)).defaults(), (QStringList{u"microdap"_s, u"steam"_s}));
    }

    void filesOnly()
    {
        Harness h;
        h.writeSessions();
        const QString d = u"root/usr/lib/microos/sessions.d/"_s;
        h.writeFile(d + u"nodesktop.conf"_s, "DESKTOP=missing.desktop\nUNIT=gamescope-session-plus@nodesktop.service\nDEFAULT=yes\n");
        h.writeFile(d + u"nounit.conf"_s, "DESKTOP=microdap.desktop\nUNIT=other@nounit.service\nDEFAULT=yes\n");
        h.writeFile(d + u"path.conf"_s, "DESKTOP=../../etc/passwd.desktop\nUNIT=gamescope-session-plus@path.service\nDEFAULT=yes\n");
        h.writeFile(d + u"plainunit.conf"_s, "DESKTOP=microdap.desktop\nUNIT=plain.service\nDEFAULT=yes\n");
        h.writeFile(d + u"Bad.Name.conf"_s, "DESKTOP=microdap.desktop\nUNIT=gamescope-session-plus@x.service\nDEFAULT=yes\n");
        h.writeFile(d + u"comment.conf"_s, "# DEFAULT=yes\nDESKTOP=microdap.desktop\nUNIT=gamescope-session-plus@c.service\n");
        h.writeFile(d + u"emptyinstance.conf"_s, "DESKTOP=microdap.desktop\nUNIT=gamescope-session-plus@.service\nDEFAULT=yes\n");
        h.writeFile(u"root/usr/lib/systemd/user/plain.service"_s, "[Service]\n");
        QCOMPARE(SessionsProvider(h.path(u"root"_s)).defaults(),
                 (QStringList{u"emptyinstance"_s, u"microdap"_s, u"plainunit"_s, u"steam"_s}));
    }

    void rereadEachCall()
    {
        Harness h;
        h.writeSessions();
        const SessionsProvider provider(h.path(u"root"_s));
        QVERIFY(provider.defaults().contains(u"microdap"_s));
        QVERIFY(QFile::remove(h.path(u"root/usr/share/wayland-sessions/microdap.desktop"_s)));
        QCOMPARE(provider.defaults(), QStringList{u"steam"_s});
    }
};

QTEST_GUILESS_MAIN(SessionsTest)
#include "tst_sessions.moc"
