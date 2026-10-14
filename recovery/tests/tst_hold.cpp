#include "recovery.h"

#include <QFile>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>
#include <QtQml/qqmlextensionplugin.h>

#include <memory>

Q_IMPORT_QML_PLUGIN(MicroSetRecoveryPlugin)

using namespace Qt::StringLiterals;

class HoldTest : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    std::unique_ptr<Recovery> m_recovery;
    std::unique_ptr<QQmlApplicationEngine> m_engine;
    QQuickWindow *m_window = nullptr;
    QObject *m_screen = nullptr;
    QObject *m_dialog = nullptr;

    QString calls()
    {
        QFile f(m_dir.filePath(u"calls"_s));
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
    }

    bool dialogShown() { return m_dialog->property("shown").toBool(); }

    void key(Qt::Key k)
    {
        QTest::keyPress(m_window, k);
        QTest::keyRelease(m_window, k);
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        const QString stub = m_dir.filePath(u"systemctl"_s);
        QFile f(stub);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("#!/bin/sh\necho \"systemctl $*\" >> \"" + m_dir.filePath(u"calls"_s).toUtf8() + "\"\n");
        f.close();
        f.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        qputenv("PATH", m_dir.path().toUtf8() + ':' + qgetenv("PATH"));
    }

    void init()
    {
        QFile::remove(m_dir.filePath(u"calls"_s));
        m_recovery = std::make_unique<Recovery>();
        m_recovery->setFixture(u"steam"_s, u"Steam"_s, u"gamescope-session-plus@steam.service"_s, true, u"steam"_s,
                               true, {u"line"_s});
        m_engine = std::make_unique<QQmlApplicationEngine>();
        m_engine->setInitialProperties({
            {u"recovery"_s, QVariant::fromValue(static_cast<QObject *>(m_recovery.get()))},
            {u"screenshot"_s, true},
            {u"shotWidth"_s, 640},
            {u"shotHeight"_s, 960},
            {u"shotFocus"_s, 1},
        });
        m_engine->loadFromModule("MicroSetRecovery", "Main");
        m_window = qobject_cast<QQuickWindow *>(m_engine->rootObjects().value(0));
        QVERIFY(m_window);
        m_window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(m_window));
        m_screen = m_window->findChild<QObject *>(u"recoveryScreen"_s);
        QVERIFY(m_screen);
        m_dialog = qvariant_cast<QObject *>(m_screen->property("dialog"));
        QVERIFY(m_dialog);
        QCOMPARE(m_screen->property("actions").toList().at(1).toMap().value(u"key"_s).toString(), u"rollback"_s);
    }

    void cleanup()
    {
        m_engine.reset();
        m_recovery.reset();
    }

    void rowOpensConfirm()
    {
        key(Qt::Key_Return);
        QTRY_VERIFY(dialogShown());
        QCOMPARE(m_dialog->property("items").toStringList(), (QStringList{u"hold to reboot"_s, u"cancel"_s}));
        QCOMPARE(m_dialog->property("focusIndex").toInt(), m_dialog->property("cancelIndex").toInt());
        QVERIFY(calls().isEmpty());
    }

    void tapOpensConfirm()
    {
        QMetaObject::invokeMethod(m_screen, "activate", Q_ARG(int, 1));
        QTRY_VERIFY(dialogShown());
        QVERIFY(calls().isEmpty());
    }

    void cancelDoesNothing()
    {
        key(Qt::Key_Return);
        QTRY_VERIFY(dialogShown());
        key(Qt::Key_Return);
        QTRY_VERIFY(!dialogShown());
        QTest::qWait(300);
        QVERIFY(calls().isEmpty());
    }

    void rebootStartsOnPress()
    {
        key(Qt::Key_Return);
        QTRY_VERIFY(dialogShown());
        key(Qt::Key_Up);
        QCOMPARE(m_dialog->property("focusIndex").toInt(), m_dialog->property("rebootIndex").toInt());
        QTest::keyPress(m_window, Qt::Key_Return);
        QTRY_VERIFY(calls().contains(u"systemctl start --no-block microos-session-rollback.service"_s));
        QVERIFY(dialogShown());
        QVERIFY(m_dialog->property("pressed").toBool());
        QTest::keyRelease(m_window, Qt::Key_Return);
        QTRY_VERIFY(!dialogShown());
        QVERIFY(!m_dialog->property("pressed").toBool());
        QTest::qWait(300);
        QCOMPARE(calls().count(u"start --no-block microos-session-rollback"_s), 1);
    }

    void touchRebootDoesNothing()
    {
        key(Qt::Key_Return);
        QTRY_VERIFY(dialogShown());
        QTRY_VERIFY(!m_dialog->property("animating").toBool());
        const qreal pitch = m_dialog->property("pitch").toReal();
        const qreal top = m_window->height() * 128.0 / 480.0;
        QTest::mouseClick(m_window, Qt::LeftButton, {}, QPoint(m_window->width() / 4, int(top + pitch / 2)));
        QTest::qWait(300);
        QCOMPARE(m_dialog->property("focusIndex").toInt(), m_dialog->property("rebootIndex").toInt());
        QVERIFY(dialogShown());
        QVERIFY(calls().isEmpty());
    }
};

QTEST_MAIN(HoldTest)
#include "tst_hold.moc"
