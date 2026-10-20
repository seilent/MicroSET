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
        for (const QString &key : f.schema.keys(u"power"_s))
            QVERIFY(reread.contains(key));
        QVERIFY(reread.value(u"meta.imported_power"_s).toBool());
    }

    void btCodecMoved()
    {
        Fixture f;
        f.h.writeState(R"({"audio.bt_codec": "ldac", "audio.output": "bt"})");
        f.store.load();
        f.settings.moveBtCodec();
        QCOMPARE(f.settings.value(u"audio.bt_codec_headphones"_s), QVariant(u"ldac"_s));
        QCOMPARE(f.settings.value(u"audio.bt_codec"_s), QVariant(u"auto"_s));
        QCOMPARE(f.settings.value(u"meta.moved_bt_codec"_s), QVariant(true));
        Store reread(f.h.path(u"state"_s));
        reread.load();
        QCOMPARE(reread.value(u"audio.bt_codec_headphones"_s).toString(), u"ldac"_s);
        QCOMPARE(reread.value(u"audio.bt_codec"_s).toString(), u"auto"_s);
        QVERIFY(reread.value(u"meta.moved_bt_codec"_s).toBool());
        QCOMPARE(reread.value(u"audio.output"_s).toString(), u"bt"_s);
    }

    void btCodecNotMoved_data()
    {
        QTest::addColumn<QByteArray>("state");
        QTest::addColumn<QString>("receiver");
        QTest::addColumn<QString>("headphones");
        QTest::newRow("nothing stored") << QByteArray("{}") << u"auto"_s << u"auto"_s;
        QTest::newRow("auto") << QByteArray(R"({"audio.bt_codec": "auto"})") << u"auto"_s << u"auto"_s;
        QTest::newRow("headphones stored")
            << QByteArray(R"({"audio.bt_codec": "aac", "audio.bt_codec_headphones": "sbc"})") << u"aac"_s << u"sbc"_s;
        QTest::newRow("headphones stored auto")
            << QByteArray(R"({"audio.bt_codec": "aac", "audio.bt_codec_headphones": "auto"})") << u"aac"_s
            << u"auto"_s;
        QTest::newRow("invalid receiver")
            << QByteArray(R"({"audio.bt_codec": "foo"})") << u"auto"_s << u"auto"_s;
        QTest::newRow("invalid headphones")
            << QByteArray(R"({"audio.bt_codec": "aac", "audio.bt_codec_headphones": "foo"})") << u"aac"_s
            << u"auto"_s;
        QTest::newRow("moved before")
            << QByteArray(R"({"audio.bt_codec": "aac", "meta.moved_bt_codec": true})") << u"aac"_s << u"auto"_s;
    }

    void btCodecNotMoved()
    {
        QFETCH(QByteArray, state);
        QFETCH(QString, receiver);
        QFETCH(QString, headphones);
        Fixture f;
        f.h.writeState(state);
        f.store.load();
        f.settings.moveBtCodec();
        QCOMPARE(f.settings.value(u"audio.bt_codec"_s), QVariant(receiver));
        QCOMPARE(f.settings.value(u"audio.bt_codec_headphones"_s), QVariant(headphones));
        Store reread(f.h.path(u"state"_s));
        reread.load();
        QVERIFY(reread.value(u"meta.moved_bt_codec"_s).toBool());
    }

    void btCodecFailedWriteMovesNothing()
    {
        Fixture f;
        f.h.writeState(R"({"audio.bt_codec": "aptx"})");
        f.store.load();
        QVERIFY(QDir(f.h.path(u"state"_s)).removeRecursively());
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"cannot create a temp file"_s));
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(u"cannot write the state file, audio.bt_codec not moved"_s));
        f.settings.moveBtCodec();
        QCOMPARE(f.settings.value(u"audio.bt_codec"_s), QVariant(u"aptx"_s));
        QCOMPARE(f.settings.value(u"audio.bt_codec_headphones"_s), QVariant(u"auto"_s));
        QCOMPARE(f.settings.value(u"meta.moved_bt_codec"_s), QVariant(false));
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
