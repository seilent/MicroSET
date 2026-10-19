#include "harness.h"
#include "powerbridge.h"
#include "schema.h"
#include "settings.h"
#include "store.h"

#include <QDir>
#include <QFile>
#include <QTest>

using namespace Qt::StringLiterals;

class ImportTest : public QObject
{
    Q_OBJECT

private:
    struct Fixture
    {
        Harness h;
        Schema schema;
        Store store{h.path(u"state"_s)};
        Settings settings{schema, store};

        Fixture()
        {
            schema.addApplier(u"power"_s);
            schema.addApplier(u"alsa"_s);
            schema.addProvider(u"sessions"_s, [] { return QStringList{u"steam"_s}; });
            schema.load(QStringLiteral(MICROSET_SOURCE_DIR) + u"/schema"_s);
        }

        void run()
        {
            store.load();
            PowerBridge::importState(h.path(u"power.conf"_s), settings);
        }
    };

    static QByteArray fixture()
    {
        QFile file(QStringLiteral(MICROSET_SOURCE_DIR) + u"/tests/fixtures/power.conf"_s);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }

private slots:
    void fixtureFile()
    {
        Fixture f;
        f.h.writeFile(u"power.conf"_s, fixture());
        f.run();
        QCOMPARE(f.settings.value(u"power.profile"_s), QVariant(u"low-power"_s));
        QCOMPARE(f.settings.value(u"power.gpu_level"_s), QVariant(u"manual"_s));
        QCOMPARE(f.settings.value(u"power.gpu_clock"_s), QVariant(441u));
        QCOMPARE(f.settings.value(u"power.governor"_s), QVariant(u"performance"_s));
        QCOMPARE(f.settings.value(u"power.tdp"_s), QVariant(6u));
        QCOMPARE(f.settings.value(u"meta.imported_power"_s), QVariant(true));
        QVERIFY(!f.settings.isStored(u"power.game_floor"_s));
        Store reread(f.h.path(u"state"_s));
        reread.load();
        QCOMPARE(reread.value(u"power.profile"_s).toString(), u"low-power"_s);
        QVERIFY(reread.value(u"meta.imported_power"_s).toBool());
    }

    void tdpNone()
    {
        Fixture f;
        f.h.writeFile(u"power.conf"_s, "profile=performance\ngpu_level=auto\ngpu_clock=587\ngovernor=schedutil\ntdp=\n");
        f.run();
        QCOMPARE(f.settings.value(u"power.profile"_s), QVariant(u"performance"_s));
        QCOMPARE(f.settings.value(u"power.tdp"_s), QVariant(10u));
        QCOMPARE(f.settings.value(u"meta.imported_power"_s), QVariant(true));
    }

    void clamped()
    {
        Fixture f;
        f.h.writeFile(u"power.conf"_s, "gpu_clock=900\ntdp=1\n");
        f.run();
        QCOMPARE(f.settings.value(u"power.gpu_clock"_s), QVariant(587u));
        QCOMPARE(f.settings.value(u"power.tdp"_s), QVariant(3u));
        QCOMPARE(f.settings.value(u"power.profile"_s), QVariant(u"balanced"_s));
        QCOMPARE(f.settings.value(u"meta.imported_power"_s), QVariant(true));
    }

    void badFileNotImported_data()
    {
        QTest::addColumn<QByteArray>("content");
        QTest::newRow("bad profile") << QByteArray("profile=turbo\ntdp=6\n");
        QTest::newRow("unknown key") << QByteArray("profile=balanced\nturbo=1\n");
        QTest::newRow("no equals") << QByteArray("profile\n");
        QTest::newRow("negative") << QByteArray("gpu_clock=-1\n");
        QTest::newRow("not a number") << QByteArray("tdp=six\n");
    }

    void badFileNotImported()
    {
        QFETCH(QByteArray, content);
        Fixture f;
        f.h.writeFile(u"power.conf"_s, content);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"not imported"_s));
        f.run();
        QCOMPARE(f.settings.value(u"meta.imported_power"_s), QVariant(false));
        QVERIFY(!f.store.contains(u"power.tdp"_s));
        QVERIFY(!f.store.contains(u"power.profile"_s));
    }

    void onlyOnce()
    {
        Fixture f;
        f.h.writeFile(u"power.conf"_s, fixture());
        f.h.writeState("{\"meta.imported_power\": true}");
        f.run();
        QCOMPARE(f.settings.value(u"power.profile"_s), QVariant(u"balanced"_s));

        Fixture g;
        g.h.writeFile(u"power.conf"_s, fixture());
        g.h.writeState("{\"power.governor\": \"ondemand\"}");
        g.run();
        QCOMPARE(g.settings.value(u"power.profile"_s), QVariant(u"balanced"_s));
        QCOMPARE(g.settings.value(u"meta.imported_power"_s), QVariant(false));
    }

    void failedWriteImportsNothing()
    {
        Fixture f;
        f.h.writeFile(u"power.conf"_s, fixture());
        f.store.load();
        QVERIFY(QDir(f.h.path(u"state"_s)).removeRecursively());
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"cannot create a temp file"_s));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"cannot write the state file, not imported"_s));
        PowerBridge::importState(f.h.path(u"power.conf"_s), f.settings);
        for (const QString &key : f.schema.keys(u"power"_s))
            QVERIFY(!f.store.contains(key));
        QCOMPARE(f.settings.value(u"meta.imported_power"_s), QVariant(false));
        QCOMPARE(f.settings.value(u"power.profile"_s), QVariant(u"balanced"_s));
    }

    void oneWrite()
    {
        Fixture f;
        f.h.writeFile(u"power.conf"_s, "profile=balanced\ngpu_level=auto\ngpu_clock=587\ngovernor=schedutil\ntdp=\n");
        f.run();
        Store reread(f.h.path(u"state"_s));
        reread.load();
        for (const QString &key : {u"power.profile"_s, u"power.gpu_level"_s, u"power.gpu_clock"_s, u"power.governor"_s,
                                   u"power.tdp"_s})
            QVERIFY(reread.contains(key));
        QVERIFY(!reread.contains(u"power.game_floor"_s));
        QVERIFY(reread.value(u"meta.imported_power"_s).toBool());
    }

    void missingFile()
    {
        Fixture f;
        f.run();
        QCOMPARE(f.settings.value(u"meta.imported_power"_s), QVariant(false));
        QVERIFY(!QFile::exists(f.store.path()));
    }
};

QTEST_GUILESS_MAIN(ImportTest)
#include "tst_import.moc"
