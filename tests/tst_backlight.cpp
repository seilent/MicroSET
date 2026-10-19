#include "backlightapplier.h"
#include "harness.h"
#include "schema.h"
#include "settings.h"
#include "store.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include <memory>

using namespace Qt::StringLiterals;

namespace {

const QByteArray Untouched = "untouched\n";

}

class BacklightTest : public QObject
{
    Q_OBJECT

private:
    struct Fixture
    {
        Harness h;
        Schema schema;
        Store store{h.path(u"state"_s)};
        Settings settings{schema, store};
        std::unique_ptr<BacklightApplier> applier;

        Fixture()
        {
            for (const QString &name : {u"power"_s, u"alsa"_s, u"ssc"_s, u"rgb"_s, u"backlight"_s})
                schema.addApplier(name);
            schema.addProvider(u"sessions"_s, [] { return QStringList{u"steam"_s}; });
            schema.load(QStringLiteral(MICROSET_SOURCE_DIR) + u"/schema"_s);
            h.writeFile(u"backlight/brightness"_s, Untouched);
        }

        void start()
        {
            store.load();
            applier = std::make_unique<BacklightApplier>(settings, h.path(u"backlight"_s), h.path(u"run"_s));
            applier->start();
        }

        QByteArray node() const
        {
            QFile file(h.path(u"backlight/brightness"_s));
            return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
        }

        void screen(int raw)
        {
            h.writeFile(u"backlight/actual_brightness"_s, QByteArray::number(raw) + '\n');
            applier->refresh();
        }

        QVariant level() const
        {
            return settings.value(BacklightApplier::Name);
        }

        QVariant stored() const
        {
            return settings.stored(BacklightApplier::Name);
        }

        int onDisk() const
        {
            const QJsonObject values = QJsonDocument::fromJson(h.readState()).object().value("values"_L1).toObject();
            return values.value(BacklightApplier::Name).toInt(-1);
        }
    };

private slots:
    void mapping()
    {
        for (quint64 raw = 1; raw <= 4095; ++raw)
            QCOMPARE(BacklightApplier::raw(BacklightApplier::basisPoints(raw, 4095), 4095), raw);
        for (uint bp = 1; bp <= 10000; ++bp)
            QVERIFY(BacklightApplier::raw(bp, 4095) >= 1);
        QCOMPARE(BacklightApplier::raw(1000, 4095), 410u);
        QCOMPARE(BacklightApplier::raw(2442, 4095), 1000u);
        QCOMPARE(BacklightApplier::raw(10000, 4095), 4095u);
        QCOMPARE(BacklightApplier::basisPoints(0, 4095), 1u);
        QCOMPARE(BacklightApplier::basisPoints(4095, 4095), 10000u);
    }

    void seedRecordsAndWritesNothing()
    {
        Fixture f;
        f.h.writeFile(u"backlight/actual_brightness"_s, "2048\n");
        f.start();
        QCOMPARE(f.stored(), QVariant(BacklightApplier::basisPoints(2048, 4095)));
        QCOMPARE(f.node(), Untouched);
        QVERIFY(QFileInfo::exists(f.h.path(u"run/backlight"_s)));
    }

    void seedSkipsZero()
    {
        Fixture f;
        f.h.writeFile(u"backlight/actual_brightness"_s, "0\n");
        f.start();
        QVERIFY(!f.stored().isValid());
        QCOMPARE(f.node(), Untouched);
    }

    void bootWritesStored()
    {
        Fixture f;
        f.h.writeState(R"({"display.brightness": 5000})");
        f.start();
        QCOMPARE(f.node(), QByteArray("2048\n"));
        QCOMPARE(f.level(), QVariant(5000u));
    }

    void bootFloor()
    {
        Fixture f;
        f.h.writeState(R"({"display.brightness": 300})");
        f.start();
        QCOMPARE(f.node(), QByteArray("410\n"));
        QCOMPARE(f.level(), QVariant(1000u));
        QCOMPARE(f.stored(), QVariant(1000u));
        QVERIFY(f.settings.flush());
        QCOMPARE(f.onDisk(), 1000);
    }

    void restartKeepsLevel()
    {
        Fixture f;
        f.h.writeFile(u"run/backlight"_s, {});
        f.h.writeState(R"({"display.brightness": 300})");
        f.start();
        QCOMPARE(f.node(), QByteArray("123\n"));
        QCOMPARE(f.level(), QVariant(300u));
    }

    void changeWrites()
    {
        Fixture f;
        f.start();
        QCOMPARE(f.settings.store(BacklightApplier::Name, 7000u), Settings::Changed);
        QCOMPARE(f.node(), QByteArray("2867\n"));
        QVERIFY(f.applier->apply(1u));
        QCOMPARE(f.node(), QByteArray("1\n"));
    }

    void foreignRecorded()
    {
        Fixture f;
        f.h.writeState(R"({"display.brightness": 5000})");
        f.start();
        f.screen(2048);
        QCOMPARE(f.stored(), QVariant(5000u));
        f.screen(3000);
        QCOMPARE(f.stored(), QVariant(BacklightApplier::basisPoints(3000, 4095)));
        QCOMPARE(f.node(), QByteArray("2048\n"));
    }

    void foreignZeroWrittenBack()
    {
        Fixture f;
        f.h.writeState(R"({"display.brightness": 5000})");
        f.start();
        f.h.writeFile(u"backlight/brightness"_s, "0\n");
        f.screen(0);
        QCOMPARE(f.node(), QByteArray("2048\n"));
        QCOMPARE(f.stored(), QVariant(5000u));
    }

    void failedWriteNotRecorded()
    {
        Fixture f;
        f.h.writeState(R"({"display.brightness": 5000})");
        f.start();
        QVERIFY(QFile::remove(f.h.path(u"backlight/brightness"_s)));
        QVERIFY(QDir().mkpath(f.h.path(u"backlight/brightness"_s)));
        QVERIFY(!f.applier->apply(8000u));
        f.screen(int(BacklightApplier::raw(8000, 4095)));
        QCOMPARE(f.stored(), QVariant(5000u));
        QVERIFY(QDir(f.h.path(u"backlight/brightness"_s)).removeRecursively());
        f.h.writeFile(u"backlight/brightness"_s, Untouched);
        f.screen(2000);
        QCOMPARE(f.node(), QByteArray("2048\n"));
        QCOMPARE(f.stored(), QVariant(5000u));
    }

    void unwritableRecordsOnly()
    {
        Fixture f;
        QVERIFY(QFile::remove(f.h.path(u"backlight/brightness"_s)));
        QVERIFY(QDir().mkpath(f.h.path(u"backlight/brightness"_s)));
        f.h.writeState(R"({"display.brightness": 5000})");
        f.start();
        f.screen(3000);
        QCOMPARE(f.stored(), QVariant(BacklightApplier::basisPoints(3000, 4095)));
    }

    void overrideLandsAndClears()
    {
        Fixture f;
        f.h.writeState(R"({"display.brightness": 5000})");
        f.start();
        f.settings.setOverride(BacklightApplier::Name, u":1.9"_s, 9000u);
        QCOMPARE(f.node(), QByteArray("3686\n"));
        QCOMPARE(f.settings.store(BacklightApplier::Name, 300u), Settings::Changed);
        QCOMPARE(f.node(), QByteArray("3686\n"));
        QVERIFY(f.settings.clearOverride(BacklightApplier::Name, u":1.9"_s));
        QCOMPARE(f.node(), QByteArray("123\n"));
    }

    void missingBacklight()
    {
        Fixture f;
        QFile::remove(f.h.path(u"backlight/max_brightness"_s));
        f.start();
        QVERIFY(!f.applier->apply(5000u));
        QCOMPARE(f.node(), Untouched);
    }
};

QTEST_GUILESS_MAIN(BacklightTest)
#include "tst_backlight.moc"
