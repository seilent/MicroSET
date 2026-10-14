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
        QCOMPARE(schema.keys({}), (QStringList{u"meta.imported_power"_s, u"power.governor"_s, u"power.gpu_clock"_s,
                                               u"power.gpu_level"_s, u"power.profile"_s, u"power.tdp"_s,
                                               u"session.default"_s}));
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
        QCOMPARE(schema.key(u"power.profile"_s)->enumValues,
                 (QVariantList{u"low-power"_s, u"balanced"_s, u"performance"_s}));
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
        QCOMPARE(schema.describe({}).size(), 7);
        QVERIFY(schema.describe(u"meta"_s).value("meta.imported_power"_L1).toObject().value("group"_L1).isNull());
    }
};

QTEST_GUILESS_MAIN(SchemaTest)
#include "tst_schema.moc"
