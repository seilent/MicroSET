#include "harness.h"
#include "schema.h"
#include "settings.h"
#include "store.h"

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTest>

using namespace Qt::StringLiterals;

class SettingsTest : public QObject
{
    Q_OBJECT

private:
    struct Fixture
    {
        Harness h;
        Schema schema;
        std::unique_ptr<Store> store;
        std::unique_ptr<Settings> settings;

        Fixture()
        {
            h.writeFile(u"s/20-microdap.json"_s,
                        R"({"owner": "microdap", "keys": {
                               "microdap.level": {"type": "u", "default": 50, "min": 1, "max": 100, "store": "coalesce"},
                               "microdap.plain": {"type": "u", "default": 5, "min": 0, "max": 10}}})");
            schema.load(h.path(u"s"_s));
            store = std::make_unique<Store>(h.path(u"state"_s));
            store->load();
            settings = std::make_unique<Settings>(schema, *store);
        }

        QJsonObject onDisk() const
        {
            return QJsonDocument::fromJson(h.readState()).object().value("values"_L1).toObject();
        }
    };

private slots:
    void coalescedStoreWaits()
    {
        Fixture f;
        QSignalSpy changed(f.settings.get(), &Settings::changed);
        QCOMPARE(f.settings->store(u"microdap.level"_s, 30u), Settings::Changed);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(changed.at(0).at(1), QVariant(30u));
        QCOMPARE(f.settings->value(u"microdap.level"_s), QVariant(30u));
        QCOMPARE(f.settings->stored(u"microdap.level"_s), QVariant(30u));
        QVERIFY(f.settings->isStored(u"microdap.level"_s));
        QVERIFY(!f.onDisk().contains("microdap.level"_L1));
        QCOMPARE(f.settings->store(u"microdap.level"_s, 40u), Settings::Changed);
        QCOMPARE(f.settings->store(u"microdap.level"_s, 40u), Settings::Unchanged);
        QCOMPARE(changed.count(), 2);
        QVERIFY(!f.onDisk().contains("microdap.level"_L1));
        QTRY_COMPARE_WITH_TIMEOUT(f.onDisk().value("microdap.level"_L1).toInt(), 40, 3000);
        QCOMPARE(changed.count(), 2);
        QCOMPARE(f.settings->stored(u"microdap.level"_s), QVariant(40u));
    }

    void flushWritesAtOnce()
    {
        Fixture f;
        f.settings->store(u"microdap.level"_s, 20u);
        QVERIFY(f.settings->flush());
        QCOMPARE(f.onDisk().value("microdap.level"_L1).toInt(), 20);
        QVERIFY(f.settings->flush());
        Store reloaded(f.h.path(u"state"_s));
        reloaded.load();
        QCOMPARE(reloaded.value(u"microdap.level"_s).toInt(), 20);
    }

    void failedFlushRetries()
    {
        Fixture f;
        f.settings->setCoalesceDelay(100);
        f.settings->store(u"microdap.level"_s, 20u);
        QVERIFY(QDir(f.h.path(u"state"_s)).removeRecursively());
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"cannot write 1 coalesced keys"_s));
        QVERIFY(!f.settings->flush());
        QTest::qWait(300);
        QVERIFY(QDir().mkpath(f.h.path(u"state"_s)));
        QTRY_COMPARE_WITH_TIMEOUT(f.onDisk().value("microdap.level"_L1).toInt(), 20, 3000);
        QCOMPARE(f.settings->value(u"microdap.level"_s), QVariant(20u));
    }

    void plainKeyStoresAtOnce()
    {
        Fixture f;
        QCOMPARE(f.settings->store(u"microdap.plain"_s, 7u), Settings::Changed);
        QCOMPARE(f.onDisk().value("microdap.plain"_L1).toInt(), 7);
    }

    void batchReplacesPending()
    {
        Fixture f;
        f.settings->store(u"microdap.level"_s, 20u);
        QSignalSpy changed(f.settings.get(), &Settings::changed);
        QCOMPARE(f.settings->store(QVariantMap{{u"microdap.level"_s, 60u}}), Settings::Changed);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(f.onDisk().value("microdap.level"_L1).toInt(), 60);
        QVERIFY(f.settings->flush());
        QCOMPARE(f.onDisk().value("microdap.level"_L1).toInt(), 60);
        QCOMPARE(f.settings->value(u"microdap.level"_s), QVariant(60u));
    }

    void overrideAbovePending()
    {
        Fixture f;
        f.settings->setOverride(u"microdap.level"_s, u":1.5"_s, 90u);
        f.settings->store(u"microdap.level"_s, 20u);
        QCOMPARE(f.settings->value(u"microdap.level"_s), QVariant(90u));
        QCOMPARE(f.settings->stored(u"microdap.level"_s), QVariant(20u));
        QVERIFY(f.settings->clearOverride(u"microdap.level"_s, u":1.5"_s));
        QCOMPARE(f.settings->value(u"microdap.level"_s), QVariant(20u));
    }

    void phoneCodecOutsideEnumReadsDefault()
    {
        Harness h;
        Schema schema;
        schema.addApplier(u"power"_s);
        schema.addApplier(u"alsa"_s);
        schema.addProvider(u"sessions"_s, [] { return QStringList{u"steam"_s}; });
        schema.load(QStringLiteral(MICROSET_SOURCE_DIR) + u"/schema"_s);
        h.writeState(R"({"audio.bt_codec": "ldac", "meta.moved_bt_codec": true})");
        Store store(h.path(u"state"_s));
        store.load();
        Settings settings(schema, store);
        QCOMPARE(settings.value(u"audio.bt_codec"_s), QVariant(u"auto"_s));
        QVERIFY(!settings.stored(u"audio.bt_codec"_s).isValid());
        QTest::ignoreMessage(QtInfoMsg, "stored audio.bt_codec ldac is not a phone codec offer, now auto");
        settings.moveBtCodec();
        QCOMPARE(settings.value(u"audio.bt_codec_headphones"_s), QVariant(u"auto"_s));
        Store reread(h.path(u"state"_s));
        reread.load();
        QCOMPARE(reread.value(u"audio.bt_codec"_s).toString(), u"auto"_s);
        QCOMPARE(settings.stored(u"audio.bt_codec"_s), QVariant(u"auto"_s));
    }

    void phoneCodecOutsideEnumRewrittenBesideHeadphones()
    {
        Harness h;
        Schema schema;
        schema.addApplier(u"power"_s);
        schema.addApplier(u"alsa"_s);
        schema.addProvider(u"sessions"_s, [] { return QStringList{u"steam"_s}; });
        schema.load(QStringLiteral(MICROSET_SOURCE_DIR) + u"/schema"_s);
        h.writeState(R"({"audio.bt_codec": "ldac", "audio.bt_codec_headphones": "sbc"})");
        Store store(h.path(u"state"_s));
        store.load();
        Settings settings(schema, store);
        settings.moveBtCodec();
        Store reread(h.path(u"state"_s));
        reread.load();
        QCOMPARE(reread.value(u"audio.bt_codec"_s).toString(), u"auto"_s);
        QCOMPARE(reread.value(u"audio.bt_codec_headphones"_s).toString(), u"sbc"_s);
        QVERIFY(reread.value(u"meta.moved_bt_codec"_s).toBool());
    }

    void phoneCodecLdacMovesToHeadphones()
    {
        Harness h;
        Schema schema;
        schema.addApplier(u"power"_s);
        schema.addApplier(u"alsa"_s);
        schema.addProvider(u"sessions"_s, [] { return QStringList{u"steam"_s}; });
        schema.load(QStringLiteral(MICROSET_SOURCE_DIR) + u"/schema"_s);
        h.writeState(R"({"audio.bt_codec": "ldac"})");
        Store store(h.path(u"state"_s));
        store.load();
        Settings settings(schema, store);
        settings.moveBtCodec();
        QCOMPARE(settings.value(u"audio.bt_codec_headphones"_s), QVariant(u"ldac"_s));
        QCOMPARE(settings.value(u"audio.bt_codec"_s), QVariant(u"auto"_s));
    }
};

QTEST_GUILESS_MAIN(SettingsTest)
#include "tst_settings.moc"
