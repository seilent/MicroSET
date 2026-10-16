#include "harness.h"
#include "schema.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QTest>

using namespace Qt::StringLiterals;

class SchemaTest : public QObject
{
    Q_OBJECT

private:
    QStringList m_sessions = {u"steam"_s, u"microdap"_s};

    Schema make(const QString &dir) const
    {
        Schema schema;
        schema.addApplier(u"power"_s);
        schema.addApplier(u"alsa"_s);
        schema.addApplier(u"ssc"_s);
        schema.addApplier(u"rgb"_s);
        schema.addProvider(u"sessions"_s, [this] { return m_sessions; });
        schema.load(dir);
        return schema;
    }

    static QByteArray file(const QByteArray &owner, const QByteArray &keys)
    {
        return "{\"owner\": \"" + owner + "\", \"keys\": {" + keys + "}}";
    }

private slots:
    void shippedSchema()
    {
        const Schema schema = make(QStringLiteral(MICROSET_SOURCE_DIR) + u"/schema"_s);
        QCOMPARE(schema.keys({}), (QStringList{u"audio.bt_codec"_s, u"audio.eq.bt.filters"_s, u"audio.eq.bt.name"_s,
                                               u"audio.eq.bt.on"_s, u"audio.eq.headphones.filters"_s,
                                               u"audio.eq.headphones.name"_s, u"audio.eq.headphones.on"_s,
                                               u"audio.eq.speaker.filters"_s, u"audio.eq.speaker.name"_s,
                                               u"audio.eq.speaker.on"_s, u"audio.eq.usb.filters"_s, u"audio.eq.usb.name"_s,
                                               u"audio.eq.usb.on"_s, u"audio.hp_amp"_s, u"audio.hp_gain"_s, u"audio.output"_s,
                                               u"audio.speaker_gain"_s, u"audio.volume_step"_s, u"display.auto_rotate"_s,
                                               u"display.orientation"_s, u"display.rotation"_s, u"meta.imported_power"_s,
                                               u"power.governor"_s, u"power.gpu_clock"_s, u"power.gpu_level"_s,
                                               u"power.profile"_s, u"power.tdp"_s, u"rgb.brightness"_s, u"rgb.color"_s,
                                               u"rgb.enabled"_s, u"session.default"_s}));
        QCOMPARE(schema.keys(u"power"_s).size(), 5);
        QCOMPARE(schema.key(u"power.profile"_s)->defaultValue, QVariant(u"balanced"_s));
        QCOMPARE(schema.key(u"power.gpu_clock"_s)->defaultValue, QVariant(587u));
        QCOMPARE(schema.key(u"power.tdp"_s)->defaultValue, QVariant(10u));
        QCOMPARE(schema.key(u"power.governor"_s)->defaultValue, QVariant(u"schedutil"_s));
        QCOMPARE(schema.key(u"power.gpu_level"_s)->defaultValue, QVariant(u"auto"_s));
        QCOMPARE(schema.key(u"session.default"_s)->defaultValue, QVariant(u"steam"_s));
        QCOMPARE(schema.key(u"session.default"_s)->enumFrom, u"sessions"_s);
        QVERIFY(!schema.key(u"meta.imported_power"_s)->writable);
        QCOMPARE(schema.key(u"meta.imported_power"_s)->defaultValue, QVariant(false));
        for (const QString &key : schema.keys(u"power"_s))
            QCOMPARE(schema.key(key)->applier, u"power"_s);
        QVERIFY(schema.key(u"session.default"_s)->applier.isEmpty());
        for (const QString &key : {u"audio.hp_gain"_s, u"audio.hp_amp"_s, u"audio.speaker_gain"_s})
            QCOMPARE(schema.key(key)->applier, u"alsa"_s);
        QVERIFY(schema.key(u"audio.output"_s)->applier.isEmpty());
        QVERIFY(schema.key(u"audio.volume_step"_s)->applier.isEmpty());
        QCOMPARE(schema.key(u"audio.output"_s)->defaultValue, QVariant(u"auto"_s));
        QCOMPARE(schema.key(u"audio.hp_gain"_s)->defaultValue, QVariant(u"high"_s));
        QCOMPARE(schema.key(u"audio.hp_amp"_s)->defaultValue, QVariant(u"lohifi"_s));
        QCOMPARE(schema.key(u"audio.speaker_gain"_s)->defaultValue, QVariant(102u));
        for (const QString &key : schema.keys(u"display"_s))
            QCOMPARE(schema.key(key)->applier, u"ssc"_s);
        for (const QString &key : schema.keys(u"rgb"_s))
            QCOMPARE(schema.key(key)->applier, u"rgb"_s);
        QCOMPARE(schema.key(u"display.orientation"_s)->defaultValue, QVariant(u"portrait"_s));
        QCOMPARE(schema.key(u"display.auto_rotate"_s)->defaultValue, QVariant(false));
        QCOMPARE(schema.key(u"display.rotation"_s)->defaultValue, QVariant(u"portrait"_s));
        QVERIFY(!schema.key(u"display.rotation"_s)->writable);
        QCOMPARE(schema.key(u"display.rotation"_s)->enumValues, schema.key(u"display.orientation"_s)->enumValues);
        QCOMPARE(schema.key(u"rgb.enabled"_s)->defaultValue, QVariant(false));
        QCOMPARE(schema.key(u"rgb.color"_s)->defaultValue, QVariant(0xffffffu));
        QCOMPARE(schema.key(u"rgb.brightness"_s)->defaultValue, QVariant(64u));
        QVERIFY(schema.accepts(*schema.key(u"rgb.color"_s), 0xffffffu));
        QVERIFY(!schema.accepts(*schema.key(u"rgb.color"_s), 0x1000000u));
        QVERIFY(schema.accepts(*schema.key(u"rgb.brightness"_s), 255u));
        QVERIFY(!schema.accepts(*schema.key(u"rgb.brightness"_s), 256u));
        QVERIFY(!schema.accepts(*schema.key(u"rgb.brightness"_s), 64));
        QCOMPARE(schema.key(u"audio.volume_step"_s)->defaultValue, QVariant(5u));
        QCOMPARE(schema.key(u"audio.speaker_gain"_s)->enumValues,
                 (QVariantList{84u, 90u, 96u, 99u, 102u, 105u, 108u}));
        QVERIFY(!schema.accepts(*schema.key(u"audio.speaker_gain"_s), 100u));
        QCOMPARE(schema.key(u"power.profile"_s)->enumValues,
                 (QVariantList{u"low-power"_s, u"balanced"_s, u"performance"_s}));
        for (const QString &slot : {u"headphones"_s, u"speaker"_s, u"usb"_s, u"bt"_s}) {
            const QString prefix = u"audio.eq."_s + slot;
            QCOMPARE(schema.key(prefix + u".on"_s)->defaultValue, QVariant(false));
            QCOMPARE(schema.key(prefix + u".name"_s)->defaultValue, QVariant(QString()));
            QCOMPARE(schema.key(prefix + u".name"_s)->maxLength, 256);
            const Key &filters = *schema.key(prefix + u".filters"_s);
            QCOMPARE(filters.defaultValue, QVariant(QStringList{u"preamp 0"_s}));
            QCOMPARE(filters.validator, u"eq_filters"_s);
            QCOMPARE(filters.maxItems, std::optional<int>(11));
            for (const QString &key : schema.keys(prefix))
                QVERIFY(schema.key(key)->applier.isEmpty());
        }
        QCOMPARE(schema.key(u"audio.bt_codec"_s)->defaultValue, QVariant(u"auto"_s));
        QCOMPARE(schema.key(u"audio.bt_codec"_s)->enumValues,
                 (QVariantList{u"auto"_s, u"aptx"_s, u"aac"_s, u"ldac"_s, u"sbc"_s}));
        for (const QString &key : schema.keys({}))
            QCOMPARE(schema.key(key)->overridable, key.startsWith("rgb."_L1));
    }

    void eqFilters_data()
    {
        QTest::addColumn<QStringList>("filters");
        QTest::addColumn<bool>("accepted");
        QTest::newRow("default") << QStringList{u"preamp 0"_s} << true;
        QTest::newRow("every type") << QStringList{u"preamp -24"_s,         u"peak 20 -24 0.1"_s,  u"lowshelf 105 3.5 0.71"_s,
                                                   u"highshelf 10000 -2 1"_s, u"bandpass 1000 0 2"_s, u"lowpass 20000 12 10"_s,
                                                   u"highpass 30 0 0.5"_s,   u"allpass 500 0 1"_s}
                                    << true;
        QStringList ten{u"preamp -6.5"_s};
        for (int i = 0; i < 10; ++i)
            ten << u"peak %1 1 1"_s.arg(100 * (i + 1));
        QTest::newRow("ten bands") << ten << true;
        QTest::newRow("eleven bands") << (ten + QStringList{u"peak 5000 1 1"_s}) << false;
        QTest::newRow("empty") << QStringList() << false;
        QTest::newRow("no preamp") << QStringList{u"peak 100 1 1"_s} << false;
        QTest::newRow("preamp second") << QStringList{u"peak 100 1 1"_s, u"preamp 0"_s} << false;
        QTest::newRow("two preamps") << QStringList{u"preamp 0"_s, u"preamp -1"_s} << false;
        QTest::newRow("preamp above 0") << QStringList{u"preamp 0.5"_s} << false;
        QTest::newRow("preamp below -24") << QStringList{u"preamp -24.1"_s} << false;
        QTest::newRow("preamp not a number") << QStringList{u"preamp low"_s} << false;
        QTest::newRow("preamp no value") << QStringList{u"preamp"_s} << false;
        QTest::newRow("unknown type") << QStringList{u"preamp 0"_s, u"notch 100 1 1"_s} << false;
        QTest::newRow("type case") << QStringList{u"preamp 0"_s, u"Peak 100 1 1"_s} << false;
        QTest::newRow("hz low") << QStringList{u"preamp 0"_s, u"peak 19.9 1 1"_s} << false;
        QTest::newRow("hz high") << QStringList{u"preamp 0"_s, u"peak 20001 1 1"_s} << false;
        QTest::newRow("hz exponent") << QStringList{u"preamp 0"_s, u"peak 1e3 1 1"_s} << false;
        QTest::newRow("db low") << QStringList{u"preamp 0"_s, u"peak 100 -24.5 1"_s} << false;
        QTest::newRow("db high") << QStringList{u"preamp 0"_s, u"peak 100 12.5 1"_s} << false;
        QTest::newRow("db plus") << QStringList{u"preamp 0"_s, u"peak 100 +3 1"_s} << false;
        QTest::newRow("q low") << QStringList{u"preamp 0"_s, u"peak 100 1 0.09"_s} << false;
        QTest::newRow("q high") << QStringList{u"preamp 0"_s, u"peak 100 1 10.1"_s} << false;
        QTest::newRow("q nan") << QStringList{u"preamp 0"_s, u"peak 100 1 nan"_s} << false;
        QTest::newRow("three fields") << QStringList{u"preamp 0"_s, u"peak 100 1"_s} << false;
        QTest::newRow("five fields") << QStringList{u"preamp 0"_s, u"peak 100 1 1 1"_s} << false;
        QTest::newRow("double space") << QStringList{u"preamp 0"_s, u"peak  100 1 1"_s} << false;
        QTest::newRow("trailing space") << QStringList{u"preamp 0 "_s} << false;
        QTest::newRow("16 char number") << QStringList{u"preamp 0"_s, u"peak 1000.00000000000 1 1"_s} << true;
        QTest::newRow("17 char number") << QStringList{u"preamp 0"_s, u"peak 1000.000000000000 1 1"_s} << false;
        QTest::newRow("long preamp") << QStringList{u"preamp -0.00000000000000"_s} << false;
    }

    void eqFilters()
    {
        QFETCH(QStringList, filters);
        QFETCH(bool, accepted);
        const Schema schema = make(QStringLiteral(MICROSET_SOURCE_DIR) + u"/schema"_s);
        QCOMPARE(schema.accepts(*schema.key(u"audio.eq.usb.filters"_s), filters), accepted);
    }

    void itemsAndOverrides()
    {
        Harness h;
        h.writeFile(u"s/20-microdap.json"_s,
                    file("microdap", R"("microdap.roots": {"type": "as", "default": [], "max_items": 2},
                                        "microdap.eq": {"type": "as", "default": ["preamp 0"], "validator": "eq_filters"},
                                        "microdap.accent": {"type": "u", "default": 1, "override": true},
                                        "microdap.a": {"type": "s", "default": "x", "max_items": 2},
                                        "microdap.b": {"type": "as", "default": [], "max_items": 0},
                                        "microdap.c": {"type": "as", "default": [], "max_items": 2000},
                                        "microdap.d": {"type": "as", "default": [], "max_items": 1.5},
                                        "microdap.e": {"type": "as", "default": [], "validator": "nope"},
                                        "microdap.f": {"type": "s", "default": "x", "validator": "eq_filters"},
                                        "microdap.g": {"type": "as", "default": [], "validator": "eq_filters"},
                                        "microdap.h": {"type": "b", "default": false, "writable": false, "override": true},
                                        "microdap.i": {"type": "b", "default": false, "override": "yes"},
                                        "microdap.j": {"type": "s", "default": "x", "applier": "power", "override": true},
                                        "microdap.k": {"type": "as", "default": ["a", "b", "c"], "max_items": 2},
                                        "microdap.tags": {"type": "as", "default": ["abcd"], "max_len": 4},
                                        "microdap.l": {"type": "as", "default": ["abcde"], "max_len": 4})"));
        const Schema schema = make(h.path(u"s"_s));
        QCOMPARE(schema.keys({}),
                 (QStringList{u"microdap.accent"_s, u"microdap.eq"_s, u"microdap.roots"_s, u"microdap.tags"_s}));
        const Key &roots = *schema.key(u"microdap.roots"_s);
        QVERIFY(schema.accepts(roots, QStringList{QString(256, u'x')}));
        QVERIFY(!schema.accepts(roots, QStringList{QString(257, u'x')}));
        const Key &tags = *schema.key(u"microdap.tags"_s);
        QVERIFY(schema.accepts(tags, QStringList{u"abcd"_s, u"ab"_s}));
        QVERIFY(!schema.accepts(tags, QStringList{u"ab"_s, u"abcde"_s}));
        const QJsonObject tagsDescribed = schema.describe(u"microdap.tags"_s).value("microdap.tags"_L1).toObject();
        QCOMPARE(tagsDescribed.value("max_len"_L1).toInt(), 4);
        QVERIFY(schema.accepts(roots, QStringList{u"a"_s, u"b"_s}));
        QVERIFY(!schema.accepts(roots, QStringList{u"a"_s, u"b"_s, u"c"_s}));
        QVERIFY(schema.accepts(*schema.key(u"microdap.eq"_s), QStringList{u"preamp 0"_s, u"peak 100 1 1"_s}));
        QVERIFY(!schema.accepts(*schema.key(u"microdap.eq"_s), QStringList{u"peak 100 1 1"_s}));
        QVERIFY(schema.key(u"microdap.accent"_s)->overridable);
        QVERIFY(!schema.key(u"microdap.roots"_s)->overridable);
        const QJsonObject described = schema.describe(u"microdap"_s);
        QCOMPARE(described.value("microdap.roots"_L1).toObject().value("max_items"_L1).toInt(), 2);
        QVERIFY(!described.value("microdap.eq"_L1).toObject().contains("max_items"_L1));
        QCOMPARE(described.value("microdap.eq"_L1).toObject().value("validator"_L1).toString(), u"eq_filters"_s);
        QCOMPARE(described.value("microdap.accent"_L1).toObject().value("override"_L1), QJsonValue(true));
        QCOMPARE(described.value("microdap.roots"_L1).toObject().value("override"_L1), QJsonValue(false));
    }

    void merge()
    {
        Harness h;
        h.writeFile(u"s/10-microset.json"_s, file("microset", R"("power.tdp": {"type": "u", "default": 10, "min": 3, "max": 10})"));
        h.writeFile(u"s/20-microdap.json"_s, file("microdap", R"("microdap.volume_step": {"type": "u", "default": 5, "enum": [2, 5, 10]})"));
        h.writeFile(u"s/30-microemu.json"_s, file("microemu", R"("microemu.shaders": {"type": "as", "default": []})"));
        const Schema schema = make(h.path(u"s"_s));
        QCOMPARE(schema.keys({}), (QStringList{u"microdap.volume_step"_s, u"microemu.shaders"_s, u"power.tdp"_s}));
        QCOMPARE(schema.key(u"microdap.volume_step"_s)->owner, u"microdap"_s);
        QCOMPARE(schema.key(u"microemu.shaders"_s)->defaultValue, QVariant(QStringList()));
    }

    void prefixRefusal()
    {
        Harness h;
        h.writeFile(u"s/10-microset.json"_s, file("microset", R"("power.tdp": {"type": "u", "default": 10})"));
        h.writeFile(u"s/00-microdap.json"_s,
                    file("microdap", R"("power.tdp": {"type": "u", "default": 3}, "power.boost": {"type": "b", "default": true},
                                        "microdapx.a": {"type": "b", "default": true}, "microdap.ok": {"type": "b", "default": true})"));
        h.writeFile(u"s/20-power.json"_s, file("power", R"("power.extra": {"type": "b", "default": true})"));
        h.writeFile(u"s/30-fake.json"_s, file("microset", R"("audio.hp_gain": {"type": "s", "default": "low"})"));
        const Schema schema = make(h.path(u"s"_s));
        QCOMPARE(schema.keys({}), (QStringList{u"microdap.ok"_s, u"power.tdp"_s}));
        QCOMPARE(schema.key(u"power.tdp"_s)->defaultValue, QVariant(10u));
        QCOMPARE(schema.key(u"power.tdp"_s)->owner, u"microset"_s);
    }

    void reservedOwners()
    {
        Harness h;
        for (const char *owner : {"microset", "power", "meta", "session", "audio", "display", "rgb", "system"})
            h.writeFile(u"s/20-%1.json"_s.arg(QLatin1StringView(owner)),
                        file(owner, QByteArray("\"") + owner + ".x\": {\"type\": \"b\", \"default\": true}"));
        h.writeFile(u"s/30-microdap.json"_s, file("microdap", R"("microdap.x": {"type": "b", "default": true})"));
        const Schema schema = make(h.path(u"s"_s));
        QCOMPARE(schema.keys({}), QStringList{u"microdap.x"_s});
    }

    void maxLength()
    {
        Harness h;
        h.writeFile(u"s/20-microdap.json"_s,
                    file("microdap", R"("microdap.name": {"type": "s", "default": "a"},
                                        "microdap.path": {"type": "s", "default": "a", "max_len": 300},
                                        "microdap.bad": {"type": "u", "default": 1, "max_len": 3},
                                        "microdap.long": {"type": "s", "default": "abcd", "max_len": 3})"));
        const Schema schema = make(h.path(u"s"_s));
        QCOMPARE(schema.keys({}), (QStringList{u"microdap.name"_s, u"microdap.path"_s}));
        const Key &name = *schema.key(u"microdap.name"_s);
        QVERIFY(schema.accepts(name, QString(256, u'x')));
        QVERIFY(!schema.accepts(name, QString(257, u'x')));
        QVERIFY(schema.accepts(*schema.key(u"microdap.path"_s), QString(300, u'x')));
        QVERIFY(!schema.accepts(*schema.key(u"microdap.path"_s), QString(301, u'x')));
        QCOMPARE(schema.describe(u"microdap.path"_s).value("microdap.path"_L1).toObject().value("max_len"_L1).toInt(), 300);
        const Schema shipped = make(QStringLiteral(MICROSET_SOURCE_DIR) + u"/schema"_s);
        QVERIFY(!shipped.accepts(*shipped.key(u"session.default"_s), QString(257, u's')));
    }

    void conflictKeepsFirst()
    {
        Harness h;
        h.writeFile(u"s/20-a.json"_s, file("microdap", R"("microdap.touch": {"type": "b", "default": true})"));
        h.writeFile(u"s/30-b.json"_s, file("microdap", R"("microdap.touch": {"type": "b", "default": false})"));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"30-b.json: microdap.touch already defined in 20-a.json"_s));
        const Schema schema = make(h.path(u"s"_s));
        QCOMPARE(schema.key(u"microdap.touch"_s)->defaultValue, QVariant(true));
        QCOMPARE(schema.key(u"microdap.touch"_s)->file, u"20-a.json"_s);
    }

    void badFilesAndKeys()
    {
        Harness h;
        h.writeFile(u"s/10-microset.json"_s, "{\"owner\": \"microset\", \"keys\": {");
        h.writeFile(u"s/20-microdap.json"_s,
                    file("microdap", R"("microdap.a": {"type": "s", "default": "x", "applier": "nope"},
                                        "microdap.b": {"type": "s", "default": "x", "enum_from": "nope"},
                                        "microdap.c": {"type": "u", "default": 50, "min": 1, "max": 10},
                                        "microdap.d": {"type": "q", "default": 1},
                                        "microdap.e": {"type": "s", "default": 1},
                                        "microdap.f": {"type": "u", "default": 1, "enum": [1, 2], "min": 1},
                                        "microdap.g": {"type": "s", "default": "x", "enum": ["y"]},
                                        "microdap.h": {"type": "b", "default": false, "writable": "no"},
                                        "microdap.ok": {"type": "s", "default": "steam", "enum_from": "sessions"})"));
        h.writeFile(u"s/30-x.json"_s, "{\"keys\": {}}");
        const Schema schema = make(h.path(u"s"_s));
        QCOMPARE(schema.keys({}), QStringList{u"microdap.ok"_s});
    }

    void typeAndRange()
    {
        const Schema schema = make(QStringLiteral(MICROSET_SOURCE_DIR) + u"/schema"_s);
        const Key &clock = *schema.key(u"power.gpu_clock"_s);
        QVERIFY(schema.accepts(clock, 305u));
        QVERIFY(schema.accepts(clock, 587u));
        QVERIFY(!schema.accepts(clock, 304u));
        QVERIFY(!schema.accepts(clock, 588u));
        QVERIFY(!schema.accepts(clock, 400));
        QVERIFY(!schema.accepts(clock, u"400"_s));
        const Key &profile = *schema.key(u"power.profile"_s);
        QVERIFY(schema.accepts(profile, u"low-power"_s));
        QVERIFY(!schema.accepts(profile, u"turbo"_s));
        QVERIFY(!schema.accepts(profile, 1u));
        const Key &imported = *schema.key(u"meta.imported_power"_s);
        QVERIFY(schema.accepts(imported, true));
        QVERIFY(!schema.accepts(imported, 1u));
    }

    void clamp()
    {
        const Schema schema = make(QStringLiteral(MICROSET_SOURCE_DIR) + u"/schema"_s);
        QCOMPARE(schema.clamp(*schema.key(u"power.gpu_clock"_s), 900u), QVariant(587u));
        QCOMPARE(schema.clamp(*schema.key(u"power.gpu_clock"_s), 0u), QVariant(305u));
        QCOMPARE(schema.clamp(*schema.key(u"power.tdp"_s), 1u), QVariant(3u));
        QCOMPARE(schema.clamp(*schema.key(u"power.tdp"_s), 4294967295u), QVariant(10u));
        QCOMPARE(schema.clamp(*schema.key(u"power.tdp"_s), 6u), QVariant(6u));
        QVERIFY(!schema.clamp(*schema.key(u"power.profile"_s), u"turbo"_s).isValid());
        QVERIFY(!schema.clamp(*schema.key(u"power.tdp"_s), 6).isValid());
    }

    void enumFromSessions()
    {
        const Schema schema = make(QStringLiteral(MICROSET_SOURCE_DIR) + u"/schema"_s);
        const Key &key = *schema.key(u"session.default"_s);
        QVERIFY(schema.accepts(key, u"microdap"_s));
        QVERIFY(!schema.accepts(key, u"recovery"_s));
        m_sessions = {u"steam"_s};
        QVERIFY(!schema.accepts(key, u"microdap"_s));
        const QJsonObject described = schema.describe(u"session"_s).value("session.default"_L1).toObject();
        QCOMPARE(described.value("enum"_L1).toArray(), QJsonArray{u"steam"_s});
        QCOMPARE(described.value("enum_from"_L1).toString(), u"sessions"_s);
        m_sessions = {u"steam"_s, u"microdap"_s};
    }

    void describeAndPrefix()
    {
        const Schema schema = make(QStringLiteral(MICROSET_SOURCE_DIR) + u"/schema"_s);
        const QJsonObject power = schema.describe(u"power"_s);
        QCOMPARE(power.size(), 5);
        const QJsonObject clock = power.value("power.gpu_clock"_L1).toObject();
        QCOMPARE(clock.value("min"_L1).toInt(), 305);
        QCOMPARE(clock.value("max"_L1).toInt(), 587);
        QCOMPARE(clock.value("type"_L1).toString(), u"u"_s);
        QCOMPARE(schema.describe(u"power.tdp"_s).size(), 1);
        QCOMPARE(schema.describe(u"powe"_s).size(), 0);
        QCOMPARE(schema.describe({}).size(), 31);
        QCOMPARE(schema.describe(u"audio"_s).size(), 18);
        QVERIFY(schema.describe(u"meta"_s).value("meta.imported_power"_L1).toObject().value("group"_L1).isNull());
    }
};

QTEST_GUILESS_MAIN(SchemaTest)
#include "tst_schema.moc"
