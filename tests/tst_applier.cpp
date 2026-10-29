#include "harness.h"
#include "protonapplier.h"
#include "protonprofiles.h"
#include "protonresolver.h"
#include "schema.h"
#include "settings.h"
#include "store.h"

#include <QDBusVariant>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QTest>

#include <functional>
#include <memory>

#include <sys/stat.h>
#include <unistd.h>

using namespace Qt::StringLiterals;

namespace {

const QByteArray Profiles =
    R"({"version": 1, "profiles": {"292120": {"tool": "proton-11.0-", "set": {"fex.tso": "off", "wine.sync": "fsync"}}}})";

class FakeFeature : public ProtonFeature
{
public:
    QString label = u"fake"_s;
    QMap<QString, QString> table = {{u"FAKE_SET"_s, u"[0-9]"_s},
                                    {u"FAKE_PROFILE"_s, u"[0-9]"_s},
                                    {u"FAKE_RULE"_s, u"[0-9]"_s},
                                    {u"FAKE_OFF"_s, u"1"_s}};
    QMap<QString, QString> keys = {{u"fake.cap"_s, u"[0-9]{1,3}"_s}};
    std::function<ProtonSection(const QString &, bool)> make;

    QString name() const override
    {
        return label;
    }

    QMap<QString, QString> names() const override
    {
        return table;
    }

    QMap<QString, QString> fragments() const override
    {
        return keys;
    }

    ProtonSection section(const QString &appid, bool profile) const override
    {
        return make ? make(appid, profile) : ProtonSection();
    }
};

}

class ApplierTest : public QObject
{
    Q_OBJECT

private:
    struct Fixture
    {
        Harness h;
        Schema schema;
        std::unique_ptr<Store> store;
        std::unique_ptr<Settings> settings;
        ProtonProfiles profiles;
        std::unique_ptr<ProtonResolver> resolver;
        std::unique_ptr<ProtonApplier> applier;

        explicit Fixture(const QByteArray &state = {}, bool lsfg = false)
        {
            for (const QString &applier : {u"power"_s, u"alsa"_s, u"ssc"_s, u"rgb"_s, u"backlight"_s})
                schema.addApplier(applier);
            schema.addProvider(u"sessions"_s, [] { return QStringList{u"steam"_s}; });
            schema.addProvider(u"gpu_undervolts"_s, [] { return QStringList{u"off"_s}; });
            schema.load(QStringLiteral(MICROSET_SOURCE_DIR) + u"/schema"_s);
            if (!state.isEmpty())
                h.writeState(state);
            store = std::make_unique<Store>(h.path(u"state"_s));
            store->load();
            settings = std::make_unique<Settings>(schema, *store);
            h.writeFile(u"profiles.json"_s, Profiles);
            profiles.load(h.path(u"profiles.json"_s), schema);
            resolver = std::make_unique<ProtonResolver>(*settings, profiles);
            applier = std::make_unique<ProtonApplier>(*settings, *resolver, dir());
            applier->addFeature(fexFeature(*resolver));
            applier->addFeature(wineFeature(*resolver));
            if (lsfg)
                applier->addFeature(lsfgFeature(*resolver));
        }

        QString dir() const
        {
            return h.path(u"run/proton"_s);
        }

        QJsonObject read(const QString &name) const
        {
            QFile file(dir() + u'/' + name);
            return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject();
        }

        QStringList files() const
        {
            return QDir(dir()).entryList(QDir::Files | QDir::Hidden | QDir::System, QDir::Name);
        }
    };

    static QJsonObject json(const QByteArray &text)
    {
        return QJsonDocument::fromJson(text).object();
    }

    static ino_t inode(const QString &path)
    {
        struct stat st;
        return ::lstat(QFile::encodeName(path).constData(), &st) == 0 ? st.st_ino : 0;
    }

    static uint mode(const QString &path)
    {
        struct stat st;
        return ::lstat(QFile::encodeName(path).constData(), &st) == 0 ? st.st_mode & 07777 : 0;
    }

    static QJsonObject feature(const QJsonObject &document, const char *name)
    {
        return document.value("features"_L1).toObject().value(QLatin1StringView(name)).toObject();
    }

private slots:
    void profileFile()
    {
        const Fixture f;
        QCOMPARE(f.applier->document(u"292120"_s), json(R"({"version": 2,
            "features": {"fex": {"env": {"FEX_TSOENABLED": "0"}, "fallback": {}},
                         "wine": {"env": {"PROTON_NO_NTSYNC": "1"}, "fallback": {}}},
            "below_local": ["FEX_TSOENABLED", "PROTON_NO_NTSYNC"],
            "profile": {"names": ["FEX_TSOENABLED", "PROTON_NO_NTSYNC"], "yield": ["FEX_TSOENABLED"], "tool": "proton-11.0-"}})"));
        const QJsonObject empty = json(R"({"version": 2, "features": {}, "below_local": []})");
        QCOMPARE(f.applier->document({}), empty);
        QCOMPARE(f.applier->document(u"2238900"_s), empty);
    }

    void fallbackIsTheSectionWithoutTheProfile()
    {
        const Fixture on;
        const Fixture off(R"({"app.292120.profile": "off"})");
        const QJsonObject withProfile = on.applier->document(u"292120"_s);
        const QJsonObject without = off.applier->document(u"292120"_s);
        QCOMPARE(feature(withProfile, "fex").value("fallback"_L1).toObject(), feature(without, "fex").value("env"_L1).toObject());
        QCOMPARE(feature(withProfile, "wine").value("fallback"_L1).toObject(),
                 feature(without, "wine").value("env"_L1).toObject());
        QCOMPARE(without, json(R"({"version": 2, "features": {}, "below_local": []})"));
    }

    void setValuesRankAboveLocal()
    {
        Fixture f(R"({"app.292120.fex.multiblock": "off", "app.292120.fex.max_inst": "5000", "app.292120.fex.smc": "none"})");
        QJsonObject document = f.applier->document(u"292120"_s);
        QCOMPARE(feature(document, "fex"),
                 json(R"({"env": {"FEX_MAXINST": "5000", "FEX_MULTIBLOCK": "0", "FEX_SMCCHECKS": "none", "FEX_TSOENABLED": "0"},
                          "fallback": {}})"));
        QCOMPARE(document.value("below_local"_L1).toArray(), (QJsonArray{u"FEX_TSOENABLED"_s, u"PROTON_NO_NTSYNC"_s}));
        QCOMPARE(f.settings->store(u"app.292120.fex.tso"_s, u"on"_s), Settings::Changed);
        document = f.applier->document(u"292120"_s);
        QCOMPARE(feature(document, "fex").value("env"_L1).toObject().value("FEX_TSOENABLED"_L1).toString(), u"1"_s);
        QVERIFY(!feature(document, "fex").contains("fallback"_L1));
        QCOMPARE(document.value("below_local"_L1).toArray(), QJsonArray{u"PROTON_NO_NTSYNC"_s});
        QCOMPARE(document.value("profile"_L1).toObject(),
                 json(R"({"names": ["PROTON_NO_NTSYNC"], "yield": [], "tool": "proton-11.0-"})"));
        QCOMPARE(f.settings->store(u"wine.sync"_s, u"fsync"_s), Settings::Changed);
        document = f.applier->document(u"292120"_s);
        QCOMPARE(document.value("below_local"_L1).toArray(), QJsonArray());
        QVERIFY(!document.contains("profile"_L1));
        QCOMPARE(feature(document, "wine"), json(R"({"env": {"PROTON_NO_NTSYNC": "1"}})"));
        QCOMPARE(f.applier->document({}), json(R"({"version": 2, "features": {"wine": {"env": {"PROTON_NO_NTSYNC": "1"}}},
                                                   "below_local": []})"));
    }

    void everyFexOptionHasAName()
    {
        Fixture f;
        const QStringList options = f.resolver->options(u"fex"_s);
        QCOMPARE(options.size(), 10);
        for (const QString &option : options)
            QCOMPARE(f.settings->store(u"app.5."_s + option, f.schema.perApp(option)->enumValues.at(1)), Settings::Changed);
        const QJsonObject env = feature(f.applier->document(u"5"_s), "fex").value("env"_L1).toObject();
        QCOMPARE(env, json(R"({"FEX_TSOENABLED": "1", "FEX_HALFBARRIERTSOENABLED": "1", "FEX_VECTORTSOENABLED": "1",
                               "FEX_MEMCPYSETTSOENABLED": "1", "FEX_MULTIBLOCK": "1", "FEX_MAXINST": "500",
                               "FEX_X87REDUCEDPRECISION": "1", "FEX_SMCCHECKS": "none", "FEX_VOLATILEMETADATA": "1",
                               "FEX_MONOHACKS": "1"})"));
    }

    void globalPresetFile()
    {
        const Fixture f(R"({"fex.preset": "compatibility"})");
        QCOMPARE(f.applier->document({}),
                 json(R"({"version": 2, "features": {"fex": {"env": {"FEX_HALFBARRIERTSOENABLED": "0",
                          "FEX_MEMCPYSETTSOENABLED": "1", "FEX_TSOENABLED": "1", "FEX_VECTORTSOENABLED": "1",
                          "FEX_X87REDUCEDPRECISION": "0"}}}, "below_local": []})"));
        const QJsonObject game = f.applier->document(u"292120"_s);
        QCOMPARE(feature(game, "fex").value("env"_L1).toObject().value("FEX_TSOENABLED"_L1).toString(), u"1"_s);
        QCOMPARE(game.value("below_local"_L1).toArray(), QJsonArray{u"PROTON_NO_NTSYNC"_s});
    }

    void belowLocalIsExact()
    {
        Fixture f(R"({"app.292120.fex.multiblock": "off"})");
        auto fake = std::make_unique<FakeFeature>();
        fake->make = [](const QString &appid, bool profile) {
            ProtonSection out;
            if (appid.isEmpty())
                return out;
            out.env = {{u"FAKE_SET"_s, u"1"_s}, {u"FAKE_PROFILE"_s, profile ? u"2"_s : u"1"_s}, {u"FAKE_RULE"_s, u"3"_s}};
            out.dxvk = {u"fake.cap = 30"_s};
            out.rules = {u"FAKE_RULE"_s, u"fake.cap"_s, u"FAKE_MISSING"_s};
            if (profile) {
                out.profile = {u"FAKE_PROFILE"_s, u"FAKE_GONE"_s};
                out.yield = {u"FAKE_PROFILE"_s, u"FAKE_RULE"_s};
            } else {
                out.rules << u"FAKE_PROFILE"_s;
            }
            out.state = u"on"_s;
            return out;
        };
        QVERIFY(f.applier->addFeature(std::move(fake)));
        const QJsonObject document = f.applier->document(u"292120"_s);
        QCOMPARE(document.value("below_local"_L1).toArray(),
                 (QJsonArray{u"FAKE_PROFILE"_s, u"FAKE_RULE"_s, u"FEX_TSOENABLED"_s, u"PROTON_NO_NTSYNC"_s, u"fake.cap"_s}));
        QCOMPARE(document.value("profile"_L1).toObject().value("names"_L1).toArray(),
                 (QJsonArray{u"FAKE_PROFILE"_s, u"FEX_TSOENABLED"_s, u"PROTON_NO_NTSYNC"_s}));
        QCOMPARE(document.value("profile"_L1).toObject().value("yield"_L1).toArray(),
                 (QJsonArray{u"FAKE_PROFILE"_s, u"FEX_TSOENABLED"_s}));
        QCOMPARE(feature(document, "fake"), json(R"({"env": {"FAKE_PROFILE": "2", "FAKE_RULE": "3", "FAKE_SET": "1"},
                                                     "dxvk": ["fake.cap = 30"], "fallback": {"FAKE_PROFILE": "1"},
                                                     "state": "on"})"));
        QCOMPARE(feature(document, "fex").value("env"_L1).toObject().value("FEX_MULTIBLOCK"_L1).toString(), u"0"_s);
        QCOMPARE(f.applier->document({}).value("below_local"_L1).toArray(), QJsonArray());
    }

    void setNtsyncRanksAboveLocal()
    {
        const Fixture f(R"({"app.292120.wine.sync": "ntsync", "app.2238900.wine.sync": "ntsync", "wine.sync": "fsync"})");
        const QJsonObject game = f.applier->document(u"292120"_s);
        QCOMPARE(feature(game, "wine"), json(R"({"env": {"PROTON_NO_NTSYNC": "0"}})"));
        QCOMPARE(game.value("below_local"_L1).toArray(), QJsonArray{u"FEX_TSOENABLED"_s});
        QCOMPARE(game.value("profile"_L1).toObject().value("names"_L1).toArray(), QJsonArray{u"FEX_TSOENABLED"_s});
        const QJsonObject other = f.applier->document(u"2238900"_s);
        QCOMPARE(feature(other, "wine"), json(R"({"env": {"PROTON_NO_NTSYNC": "0"}})"));
        QCOMPARE(other.value("below_local"_L1).toArray(), QJsonArray());
        QCOMPARE(feature(f.applier->document({}), "wine"), json(R"({"env": {"PROTON_NO_NTSYNC": "1"}})"));
        const Fixture plain(R"({"app.2238900.wine.sync": "fsync"})");
        QCOMPARE(plain.settings->store(u"app.2238900.wine.sync"_s, u"auto"_s), Settings::Changed);
        QVERIFY(!plain.applier->document(u"2238900"_s).value("features"_L1).toObject().contains("wine"_L1));
        QVERIFY(!plain.applier->document({}).value("features"_L1).toObject().contains("wine"_L1));
    }

    void oversizedFileKept()
    {
        Fixture f;
        const auto big = std::make_shared<bool>(false);
        auto fake = std::make_unique<FakeFeature>();
        for (int i = 0; i < 300; ++i)
            fake->table.insert(u"FAKE_BIG_%1"_s.arg(i, 3, 10, u'0'), u"1"_s);
        fake->make = [big](const QString &appid, bool) {
            ProtonSection out;
            for (int i = 0; *big && appid == "5"_L1 && i < 300; ++i)
                out.env.insert(u"FAKE_BIG_%1"_s.arg(i, 3, 10, u'0'), u"1"_s);
            return out;
        };
        QVERIFY(f.applier->addFeature(std::move(fake)));
        QVERIFY(f.applier->start());
        QCOMPARE(f.settings->store(u"app.5.fex.tso"_s, u"on"_s), Settings::Changed);
        const QJsonObject before = f.read(u"5.json"_s);
        QCOMPARE(feature(before, "fex"), json(R"({"env": {"FEX_TSOENABLED": "1"}})"));
        *big = true;
        QVERIFY(QJsonDocument(f.applier->document(u"5"_s)).toJson(QJsonDocument::Compact).size() > 4096);
        const QRegularExpression cap(u"run/proton/5\\.json: over the hook's 4 KiB cap$"_s);
        QTest::ignoreMessage(QtWarningMsg, cap);
        QCOMPARE(f.settings->store(u"app.5.fex.multiblock"_s, u"off"_s), Settings::Changed);
        QCOMPARE(f.read(u"5.json"_s), before);
        QTest::ignoreMessage(QtWarningMsg, cap);
        QCOMPARE(f.settings->store(u"app.5.fex.smc"_s, u"none"_s), Settings::Changed);
        QCOMPARE(f.read(u"5.json"_s), before);
        *big = false;
        QCOMPARE(f.settings->store(u"app.5.fex.smc"_s, u"auto"_s), Settings::Changed);
        QCOMPARE(feature(f.read(u"5.json"_s), "fex"), json(R"({"env": {"FEX_MULTIBLOCK": "0", "FEX_TSOENABLED": "1"}})"));
        QCOMPARE(f.files(), (QStringList{u"292120.json"_s, u"5.json"_s, u"default.json"_s}));
    }

    void contractFixture()
    {
        Fixture f({}, true);
        QJsonArray cases;
        const auto add = [&](const QJsonObject &set, const QString &appid) {
            const QJsonObject file = f.applier->document(appid);
            QVERIFY(QJsonDocument(file).toJson(QJsonDocument::Compact).size() <= 4096);
            cases << QJsonObject{{u"set"_s, set}, {u"appid"_s, appid}, {u"file"_s, file}};
        };
        const auto each = [&](const QString &name, const QVariantList &values, const QStringList &appids) {
            const QVariant before = f.settings->value(name);
            for (const QVariant &value : values) {
                if (value == f.schema.key(name)->defaultValue)
                    continue;
                QCOMPARE(f.settings->store(name, value), Settings::Changed);
                for (const QString &appid : appids)
                    add({{name, QJsonValue::fromVariant(value)}}, appid);
                QCOMPARE(f.settings->store(name, before), Settings::Changed);
            }
        };
        const auto together = [&](const QVariantMap &values, const QStringList &appids) {
            QCOMPARE(f.settings->store(values), Settings::Changed);
            for (const QString &appid : appids)
                add(QJsonObject::fromVariantMap(values), appid);
            for (auto it = values.cbegin(); it != values.cend(); ++it)
                QCOMPARE(f.settings->store(it.key(), f.schema.key(it.key())->defaultValue), Settings::Changed);
        };
        add({}, {});
        add({}, u"292120"_s);
        for (const QString &appid : {u"5"_s, u"292120"_s})
            for (const QString &option : f.schema.options())
                each(u"app.%1.%2"_s.arg(appid, option), f.schema.perApp(option)->enumValues, {appid});
        const QStringList all{QString(), u"5"_s, u"292120"_s};
        for (const QString &global : {u"fex.preset"_s, u"wine.sync"_s, u"proton.profiles"_s, u"lsfg.mode"_s})
            each(global, f.schema.key(global)->enumValues, all);
        each(u"lsfg.enabled"_s, {false}, all);
        const QList<QPair<QString, QVariantList>> blacklisted{{u"lsfg.fps_cap"_s, {u"20"_s, u"30"_s, u"off"_s}},
                                                              {u"lsfg.multiplier"_s, {3u}},
                                                              {u"lsfg.flow_scale"_s, {25u, 75u, 100u}},
                                                              {u"lsfg.performance_mode"_s, {false}},
                                                              {u"lsfg.present_wait_offset"_s, {0u, 8u}}};
        for (const auto &[name, values] : blacklisted)
            for (const QVariant &value : values)
                together({{u"lsfg.mode"_s, u"blacklist"_s}, {name, value}}, all);
        together({{u"lsfg.mode"_s, u"blacklist"_s}, {u"app.5.lsfg.state"_s, u"off"_s}}, {u"5"_s});
        together({{u"lsfg.enabled"_s, false}, {u"app.5.lsfg.state"_s, u"on"_s}}, {u"5"_s});
        together({{u"lsfg.mode"_s, u"blacklist"_s}, {u"app.5.lsfg.multiplier"_s, u"3"_s}}, {QString(), u"5"_s});
        together({{u"app.5.lsfg.state"_s, u"on"_s}, {u"app.5.lsfg.multiplier"_s, u"3"_s}}, {u"5"_s});
        together({{u"app.5.lsfg.state"_s, u"on"_s}, {u"lsfg.fps_cap"_s, u"off"_s}}, {u"5"_s});
        together({{u"app.292120.lsfg.state"_s, u"on"_s}, {u"app.292120.fex.tso"_s, u"on"_s}}, {u"292120"_s});
        const QJsonObject actual{{u"profiles"_s, json(Profiles)}, {u"cases"_s, cases}};
        QFile shipped(QStringLiteral(MICROSET_SOURCE_DIR) + u"/tests/fixtures/proton-env-all.json"_s);
        const QJsonObject expected =
            shipped.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(shipped.readAll()).object() : QJsonObject();
        if (actual != expected) {
            QFile out(u"proton-env-all.actual.json"_s);
            QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
            out.write(QJsonDocument(actual).toJson(QJsonDocument::Indented));
        }
        QCOMPARE(actual, expected);
    }

    void stateAndToken()
    {
        Fixture f;
        auto fake = std::make_unique<FakeFeature>();
        fake->make = [](const QString &appid, bool) {
            ProtonSection out;
            out.state = u"off"_s;
            if (appid.isEmpty())
                return out;
            out.env = {{u"FAKE_SET"_s, u"1"_s}};
            out.token = u"fake-ready"_s;
            out.otherwise = {{u"FAKE_OFF"_s, u"1"_s}};
            return out;
        };
        QVERIFY(f.applier->addFeature(std::move(fake)));
        QCOMPARE(feature(f.applier->document({}), "fake"), json(R"({"env": {}, "state": "off"})"));
        QCOMPARE(feature(f.applier->document(u"2238900"_s), "fake"),
                 json(R"({"env": {"FAKE_SET": "1"}, "requires": "fake-ready", "otherwise": {"FAKE_OFF": "1"},
                          "state": "off"})"));
    }

    void lsfgFiles()
    {
        const Fixture f({}, true);
        QCOMPARE(f.applier->document(u"292120"_s), json(R"({"version": 2,
            "features": {"fex": {"env": {"FEX_TSOENABLED": "0"}, "fallback": {}},
                         "wine": {"env": {"PROTON_NO_NTSYNC": "1"}, "fallback": {}},
                         "lsfg": {"env": {"DISABLE_LSFGVK": "1"}, "state": "off"}},
            "below_local": ["DISABLE_LSFGVK", "FEX_TSOENABLED", "PROTON_NO_NTSYNC"],
            "profile": {"names": ["FEX_TSOENABLED", "PROTON_NO_NTSYNC"], "yield": ["FEX_TSOENABLED"], "tool": "proton-11.0-"}})"));
        const QJsonObject whitelist = json(R"({"version": 2, "features": {"lsfg": {"env": {"DISABLE_LSFGVK": "1"}, "state": "off"}},
                                              "below_local": ["DISABLE_LSFGVK"]})");
        QCOMPARE(f.applier->document({}), whitelist);
        QCOMPARE(f.applier->document(u"2238900"_s), whitelist);

        const QJsonObject on = json(R"({"env": {"LSFGVK_ENV": "1", "LSFGVK_FLOW_SCALE": "0.5", "LSFGVK_MULTIPLIER": "2",
                                                "LSFGVK_PERFORMANCE_MODE": "1", "LSFGVK_PRESENT_WAIT_OFFSET": "2",
                                                "VKD3D_FRAME_RATE": "30"},
                                        "dxvk": ["dxgi.maxFrameRate = 30", "d3d9.maxFrameRate = 30"],
                                        "requires": "lsfg-ready", "otherwise": {"DISABLE_LSFGVK": "1"}})");
        const QJsonArray untouched{u"LSFGVK_FLOW_SCALE"_s, u"LSFGVK_MULTIPLIER"_s, u"LSFGVK_PERFORMANCE_MODE"_s,
                                   u"LSFGVK_PRESENT_WAIT_OFFSET"_s, u"VKD3D_FRAME_RATE"_s, u"d3d9.maxFrameRate"_s,
                                   u"dxgi.maxFrameRate"_s};
        const Fixture set(R"({"app.2238900.lsfg.state": "on"})", true);
        QCOMPARE(set.applier->document(u"2238900"_s),
                 (QJsonObject{{u"version"_s, 2}, {u"features"_s, QJsonObject{{u"lsfg"_s, on}}}, {u"below_local"_s, untouched}}));
        QCOMPARE(set.applier->document({}), whitelist);
        const Fixture both(R"({"app.2238900.lsfg.state": "on", "app.2238900.lsfg.multiplier": "3"})", true);
        const QJsonObject picked = both.applier->document(u"2238900"_s);
        QCOMPARE(picked.keys(), (QStringList{u"below_local"_s, u"features"_s, u"version"_s}));
        QCOMPARE(feature(picked, "lsfg").value("env"_L1).toObject().value("LSFGVK_MULTIPLIER"_L1).toString(), u"3"_s);
        QCOMPARE(picked.value("below_local"_L1).toArray(),
                 (QJsonArray{u"LSFGVK_FLOW_SCALE"_s, u"LSFGVK_PERFORMANCE_MODE"_s, u"LSFGVK_PRESENT_WAIT_OFFSET"_s,
                             u"VKD3D_FRAME_RATE"_s, u"d3d9.maxFrameRate"_s, u"dxgi.maxFrameRate"_s}));
        const Fixture chosen(R"({"lsfg.mode": "blacklist", "app.2238900.lsfg.multiplier": "3"})", true);
        const QJsonArray ranked = chosen.applier->document(u"2238900"_s).value("below_local"_L1).toArray();
        QVERIFY(ranked.contains(u"LSFGVK_ENV"_s) && ranked.contains(u"DISABLE_LSFGVK"_s));
        QVERIFY(!ranked.contains(u"LSFGVK_MULTIPLIER"_s));

        const Fixture blacklist(R"({"lsfg.mode": "blacklist"})", true);
        const QJsonArray rules{u"DISABLE_LSFGVK"_s, u"LSFGVK_ENV"_s, u"LSFGVK_FLOW_SCALE"_s, u"LSFGVK_MULTIPLIER"_s,
                               u"LSFGVK_PERFORMANCE_MODE"_s, u"LSFGVK_PRESENT_WAIT_OFFSET"_s, u"VKD3D_FRAME_RATE"_s,
                               u"d3d9.maxFrameRate"_s, u"dxgi.maxFrameRate"_s};
        for (const QString &appid : {QString(), u"2238900"_s}) {
            QCOMPARE(feature(blacklist.applier->document(appid), "lsfg"), on);
            QCOMPARE(blacklist.applier->document(appid).value("below_local"_L1).toArray(), rules);
        }
        QCOMPARE(blacklist.applier->document(u"292120"_s).value("below_local"_L1).toArray(),
                 (QJsonArray{u"DISABLE_LSFGVK"_s, u"FEX_TSOENABLED"_s, u"LSFGVK_ENV"_s, u"LSFGVK_FLOW_SCALE"_s,
                             u"LSFGVK_MULTIPLIER"_s, u"LSFGVK_PERFORMANCE_MODE"_s, u"LSFGVK_PRESENT_WAIT_OFFSET"_s,
                             u"PROTON_NO_NTSYNC"_s, u"VKD3D_FRAME_RATE"_s, u"d3d9.maxFrameRate"_s, u"dxgi.maxFrameRate"_s}));

        const QJsonObject off = json(R"({"env": {"DISABLE_LSFGVK": "1"}, "state": "off"})");
        for (const QByteArray &state : {R"({"lsfg.mode": "blacklist", "app.2238900.lsfg.state": "off"})"_ba,
                                        R"({"lsfg.enabled": false, "app.2238900.lsfg.state": "on"})"_ba}) {
            const Fixture fixed(state, true);
            QCOMPARE(feature(fixed.applier->document(u"2238900"_s), "lsfg"), off);
            QCOMPARE(fixed.applier->document(u"2238900"_s).value("below_local"_L1).toArray(), QJsonArray());
        }
        const Fixture disabled(R"({"lsfg.enabled": false})", true);
        QCOMPARE(disabled.applier->document({}),
                 json(R"({"version": 2, "features": {"lsfg": {"env": {"DISABLE_LSFGVK": "1"}, "state": "off"}}, "below_local": []})"));
    }

    void lsfgGlobalChangeRewritesEveryFile()
    {
        Fixture f(R"({"app.2238900.fex.multiblock": "off"})", true);
        QVERIFY(f.applier->start());
        const QStringList files{u"default.json"_s, u"2238900.json"_s, u"292120.json"_s};
        for (const QString &name : files)
            QCOMPARE(feature(f.read(name), "lsfg").value("state"_L1).toString(), u"off"_s);
        QCOMPARE(f.settings->store(u"lsfg.mode"_s, u"blacklist"_s), Settings::Changed);
        for (const QString &name : files)
            QCOMPARE(feature(f.read(name), "lsfg").value("requires"_L1).toString(), u"lsfg-ready"_s);
        QCOMPARE(f.settings->store(u"lsfg.multiplier"_s, 3u), Settings::Changed);
        for (const QString &name : files)
            QCOMPARE(feature(f.read(name), "lsfg").value("env"_L1).toObject().value("VKD3D_FRAME_RATE"_L1).toString(), u"20"_s);
        QCOMPARE(f.settings->store(u"app.2238900.lsfg.state"_s, u"off"_s), Settings::Changed);
        QCOMPARE(feature(f.read(u"2238900.json"_s), "lsfg"), json(R"({"env": {"DISABLE_LSFGVK": "1"}, "state": "off"})"));
        QCOMPARE(feature(f.read(u"default.json"_s), "lsfg").value("requires"_L1).toString(), u"lsfg-ready"_s);
        QCOMPARE(f.settings->store(u"app.7.lsfg.state"_s, u"off"_s), Settings::Changed);
        QVERIFY(f.files().contains(u"7.json"_s));
        QCOMPARE(f.settings->store(u"app.7.lsfg.state"_s, u"auto"_s), Settings::Changed);
        QVERIFY(!f.files().contains(u"7.json"_s));
    }

    void lsfgNamesClaimedOnce()
    {
        Fixture f({}, true);
        const auto refused = [&](const std::function<void(FakeFeature &)> &change, const char *why) {
            auto fake = std::make_unique<FakeFeature>();
            change(*fake);
            QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QString::fromLatin1(why)));
            return !f.applier->addFeature(std::move(fake));
        };
        QVERIFY(refused([](FakeFeature &fake) { fake.table.insert(u"DISABLE_LSFGVK"_s, u"1"_s); },
                        "proton feature fake: DISABLE_LSFGVK claimed twice, refused"));
        QVERIFY(refused([](FakeFeature &fake) { fake.table.insert(u"VKD3D_FRAME_RATE"_s, u"[0-9]+"_s); },
                        "proton feature fake: VKD3D_FRAME_RATE claimed twice, refused"));
        QVERIFY(refused([](FakeFeature &fake) { fake.keys.insert(u"dxgi.maxFrameRate"_s, u"[0-9]+"_s); },
                        "proton feature fake: dxgi.maxFrameRate claimed twice, refused"));
        QVERIFY(refused([](FakeFeature &fake) { fake.label = u"lsfg"_s; }, "proton feature lsfg: bad or taken feature name"));
        Fixture first;
        auto fake = std::make_unique<FakeFeature>();
        fake->keys.insert(u"d3d9.maxFrameRate"_s, u"[0-9]+"_s);
        QVERIFY(first.applier->addFeature(std::move(fake)));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"proton feature lsfg: d3d9.maxFrameRate claimed twice, refused"_s));
        QVERIFY(!first.applier->addFeature(lsfgFeature(*first.resolver)));
    }

    void allowlist()
    {
        Fixture f;
        auto fake = std::make_unique<FakeFeature>();
        fake->make = [](const QString &, bool) {
            ProtonSection out;
            out.env = {{u"FAKE_SET"_s, u"x"_s}, {u"OTHER"_s, u"1"_s}, {u"FEX_TSOENABLED"_s, u"0"_s}, {u"FAKE_RULE"_s, u"4"_s}};
            out.otherwise = {{u"FAKE_OFF"_s, u"2"_s}};
            out.token = u"fake-ready"_s;
            out.dxvk = {u"fake.cap = 3000"_s, u"other.cap = 1"_s, u"fake.cap=30"_s, u"fake.cap = 30"_s};
            return out;
        };
        QVERIFY(f.applier->addFeature(std::move(fake)));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"proton feature fake: FAKE_SET=x is not in its allowlist"_s));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"proton feature fake: OTHER=1 is not in its allowlist"_s));
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(u"proton feature fake: FEX_TSOENABLED=0 is not in its allowlist"_s));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"proton feature fake: FAKE_OFF=2 is not in its allowlist"_s));
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(u"proton feature fake: fragment fake.cap = 3000 is not in its allowlist"_s));
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(u"proton feature fake: fragment other.cap = 1 is not in its allowlist"_s));
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(u"proton feature fake: fragment fake.cap=30 is not in its allowlist"_s));
        QCOMPARE(feature(f.applier->document(u"2238900"_s), "fake"),
                 json(R"({"env": {"FAKE_RULE": "4"}, "dxvk": ["fake.cap = 30"], "requires": "fake-ready",
                          "otherwise": {}})"));
    }

    void namesClaimedOnce()
    {
        Fixture f;
        const auto refused = [&](const std::function<void(FakeFeature &)> &change, const char *why) {
            auto fake = std::make_unique<FakeFeature>();
            change(*fake);
            QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QString::fromLatin1(why)));
            return !f.applier->addFeature(std::move(fake));
        };
        QVERIFY(refused([](FakeFeature &fake) { fake.table.insert(u"FEX_TSOENABLED"_s, u"0"_s); },
                        "proton feature fake: FEX_TSOENABLED claimed twice, refused"));
        QVERIFY(refused([](FakeFeature &fake) { fake.label = u"fex"_s; }, "proton feature fex: bad or taken feature name"));
        QVERIFY(refused([](FakeFeature &fake) { fake.label = u"proton"_s; },
                        "proton feature proton: bad or taken feature name"));
        QVERIFY(refused([](FakeFeature &fake) { fake.label = u"Fake"_s; }, "proton feature Fake: bad or taken feature name"));
        QVERIFY(refused([](FakeFeature &fake) { fake.table.insert(u"MICROOS_PROTON_HOOK"_s, u"ok"_s); },
                        "proton feature fake: bad name MICROOS_PROTON_HOOK, refused"));
        QVERIFY(refused([](FakeFeature &fake) { fake.table.insert(u"lower"_s, u"1"_s); },
                        "proton feature fake: bad name lower, refused"));
        QVERIFY(refused([](FakeFeature &fake) { fake.table.insert(u"FAKE_BAD"_s, u"("_s); },
                        "proton feature fake: bad name FAKE_BAD, refused"));
        QVERIFY(refused([](FakeFeature &fake) { fake.keys.insert(u"FAKE_SET"_s, u"1"_s); },
                        "proton feature fake: FAKE_SET claimed twice, refused"));
        QVERIFY(f.applier->addFeature(std::make_unique<FakeFeature>()));
        QVERIFY(refused(
            [](FakeFeature &fake) {
                fake.label = u"other"_s;
                fake.table = {{u"OTHER_SET"_s, u"1"_s}};
                fake.keys = {{u"fake.cap"_s, u"1"_s}};
            },
            "proton feature other: fake.cap claimed twice, refused"));
        QVERIFY(refused(
            [](FakeFeature &fake) {
                fake.label = u"other"_s;
                fake.table = {{u"FAKE_RULE"_s, u"1"_s}};
                fake.keys = {};
            },
            "proton feature other: FAKE_RULE claimed twice, refused"));
        auto other = std::make_unique<FakeFeature>();
        other->label = u"other"_s;
        other->table = {{u"OTHER_SET"_s, u"1"_s}};
        other->keys = {{u"other.cap"_s, u"1"_s}};
        QVERIFY(f.applier->addFeature(std::move(other)));
    }

    void filesAndModes()
    {
        Fixture f(R"({"app.2238900.fex.multiblock": "off"})");
        QDir().mkpath(f.dir());
        f.h.writeFile(u"run/proton/77.json"_s, "{}");
        f.h.writeFile(u"run/proton/.77.json.Ab12Cd"_s, "{}");
        f.h.writeFile(u"run/proton/2238900.json"_s, "{}");
        f.h.writeFile(u"run/proton/notes.txt"_s, "x");
        const mode_t old = ::umask(077);
        const auto restore = qScopeGuard([old] { ::umask(old); });
        QVERIFY(f.applier->start());
        QCOMPARE(mode(f.dir()), 0755u);
        QCOMPARE(f.files(), (QStringList{u"2238900.json"_s, u"292120.json"_s, u"default.json"_s, u"notes.txt"_s}));
        for (const QString &name : {u"2238900.json"_s, u"292120.json"_s, u"default.json"_s})
            QCOMPARE(mode(f.dir() + u'/' + name), 0644u);
        QCOMPARE(f.read(u"292120.json"_s), f.applier->document(u"292120"_s));
        QCOMPARE(f.read(u"2238900.json"_s), f.applier->document(u"2238900"_s));
        QCOMPARE(f.read(u"default.json"_s), f.applier->document({}));
        QCOMPARE(feature(f.read(u"2238900.json"_s), "fex"), json(R"({"env": {"FEX_MULTIBLOCK": "0"}})"));
        QCOMPARE(f.settings->store(u"app.7.wine.sync"_s, u"fsync"_s), Settings::Changed);
        QCOMPARE(feature(f.read(u"7.json"_s), "wine"), json(R"({"env": {"PROTON_NO_NTSYNC": "1"}})"));
        QCOMPARE(mode(f.dir() + u"/7.json"_s), 0644u);
        QCOMPARE(f.settings->store(u"app.7.wine.sync"_s, u"auto"_s), Settings::Changed);
        QVERIFY(!f.files().contains(u"7.json"_s));
        QCOMPARE(f.settings->store(u"app.2238900.fex.multiblock"_s, u"auto"_s), Settings::Changed);
        QVERIFY(!f.files().contains(u"2238900.json"_s));
        QCOMPARE(f.settings->store(u"app.292120.profile"_s, u"off"_s), Settings::Changed);
        QVERIFY(f.files().contains(u"292120.json"_s));
        QCOMPARE(f.read(u"292120.json"_s), json(R"({"version": 2, "features": {}, "below_local": []})"));
        QCOMPARE(f.files(), (QStringList{u"292120.json"_s, u"default.json"_s, u"notes.txt"_s}));
    }

    void globalChangeRewritesEveryFile()
    {
        Fixture f(R"({"app.2238900.fex.multiblock": "off"})");
        QVERIFY(f.applier->start());
        QCOMPARE(f.settings->store(u"wine.sync"_s, u"fsync"_s), Settings::Changed);
        for (const QString &name : {u"default.json"_s, u"2238900.json"_s, u"292120.json"_s})
            QCOMPARE(feature(f.read(name), "wine").value("env"_L1).toObject().value("PROTON_NO_NTSYNC"_L1).toString(), u"1"_s);
        QCOMPARE(f.settings->store(u"proton.profiles"_s, u"off"_s), Settings::Changed);
        QVERIFY(!f.read(u"292120.json"_s).contains("profile"_L1));
        QCOMPARE(f.settings->store(u"fex.preset"_s, u"compatibility"_s), Settings::Changed);
        for (const QString &name : {u"default.json"_s, u"2238900.json"_s, u"292120.json"_s})
            QCOMPARE(feature(f.read(name), "fex").value("env"_L1).toObject().value("FEX_TSOENABLED"_L1).toString(), u"1"_s);
    }

    void unchangedFilesNotRewritten()
    {
        Fixture f(R"({"app.2238900.fex.multiblock": "off"})");
        QVERIFY(f.applier->start());
        const ino_t game = inode(f.dir() + u"/292120.json"_s);
        const ino_t fallback = inode(f.dir() + u"/default.json"_s);
        const ino_t other = inode(f.dir() + u"/2238900.json"_s);
        QCOMPARE(f.settings->store(u"app.2238900.fex.tso"_s, u"off"_s), Settings::Changed);
        QCOMPARE(inode(f.dir() + u"/292120.json"_s), game);
        QCOMPARE(inode(f.dir() + u"/default.json"_s), fallback);
        QVERIFY(inode(f.dir() + u"/2238900.json"_s) != other);
        QCOMPARE(f.settings->store(u"audio.volume_step"_s, 10u), Settings::Changed);
        QCOMPARE(f.settings->store(u"proton.profiles"_s, u"off"_s), Settings::Changed);
        QCOMPARE(inode(f.dir() + u"/default.json"_s), fallback);
        QVERIFY(inode(f.dir() + u"/292120.json"_s) != game);
    }

    void directoryKept()
    {
        Fixture f;
        QDir().mkpath(f.dir());
        ::chmod(QFile::encodeName(f.dir()).constData(), 0700);
        const ino_t before = inode(f.dir());
        QVERIFY(f.applier->start());
        QCOMPARE(inode(f.dir()), before);
        QCOMPARE(mode(f.dir()), 0755u);
        QCOMPARE(f.settings->store(u"wine.sync"_s, u"fsync"_s), Settings::Changed);
        QCOMPARE(inode(f.dir()), before);
    }

    void directoryNotFollowed()
    {
        Fixture f;
        QDir().mkpath(f.h.path(u"elsewhere"_s));
        QVERIFY(QFile::link(f.h.path(u"elsewhere"_s), f.dir()));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"run/proton: not a directory, no proton env files"_s));
        QVERIFY(!f.applier->start());
        QVERIFY(QDir(f.h.path(u"elsewhere"_s)).isEmpty());
        QCOMPARE(f.settings->store(u"wine.sync"_s, u"fsync"_s), Settings::Changed);
        QVERIFY(QDir(f.h.path(u"elsewhere"_s)).isEmpty());
        Fixture plain;
        plain.h.writeFile(u"run/proton"_s, "x");
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"run/proton: not a directory, no proton env files"_s));
        QVERIFY(!plain.applier->start());
    }

    void daemonWritesBeforeTheName()
    {
        Harness h;
        h.writeSessions();
        h.writeFile(u"profiles.json"_s, Profiles);
        h.writeState(R"({"app.2238900.fex.multiblock": "off"})");
        QVERIFY(h.startBus());
        QDBusConnection squatter = QDBusConnection::connectToBus(h.address(), u"squatter"_s);
        QVERIFY(squatter.registerService(u"net.seilent.MicroSet"_s));
        QProcess daemon;
        daemon.setProcessEnvironment(h.environment());
        daemon.setProcessChannelMode(QProcess::MergedChannels);
        daemon.start(QStringLiteral(MICROSET_DAEMON), {});
        QVERIFY(daemon.waitForFinished(10000));
        QCOMPARE(daemon.exitCode(), 1);
        QVERIFY(QString::fromUtf8(daemon.readAll()).contains("cannot own net.seilent.MicroSet"_L1));
        const QString dir = h.path(u"run/proton"_s);
        QCOMPARE(QDir(dir).entryList(QDir::Files | QDir::Hidden, QDir::Name),
                 (QStringList{u"2238900.json"_s, u"292120.json"_s, u"default.json"_s}));
        QFile game(dir + u"/292120.json"_s);
        QVERIFY(game.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(game.readAll()).object().value("below_local"_L1).toArray(),
                 (QJsonArray{u"DISABLE_LSFGVK"_s, u"FEX_TSOENABLED"_s, u"PROTON_NO_NTSYNC"_s}));
        QDBusConnection::disconnectFromBus(u"squatter"_s);
    }

    void daemonFollowsSets()
    {
        Harness h;
        h.writeSessions();
        h.writeFile(u"profiles.json"_s, Profiles);
        QVERIFY(h.startBus());
        QVERIFY(h.startDaemon());
        const QString dir = h.path(u"run/proton"_s);
        const ino_t before = inode(dir);
        QCOMPARE(mode(dir), 0755u);
        const auto read = [&](const QString &name) {
            QFile file(dir + u'/' + name);
            return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject();
        };
        QCOMPARE(feature(read(u"292120.json"_s), "fex").value("env"_L1).toObject().value("FEX_TSOENABLED"_L1).toString(),
                 u"0"_s);
        const auto set = [&](const QString &key, const QString &value) {
            const QDBusMessage reply = h.call(u"Set"_s, {key, QVariant::fromValue(QDBusVariant(value))});
            return reply.type() == QDBusMessage::ReplyMessage;
        };
        QVERIFY(set(u"app.292120.fex.tso"_s, u"on"_s));
        QCOMPARE(feature(read(u"292120.json"_s), "fex").value("env"_L1).toObject().value("FEX_TSOENABLED"_L1).toString(),
                 u"1"_s);
        QVERIFY(set(u"app.2238900.wine.sync"_s, u"fsync"_s));
        QCOMPARE(feature(read(u"2238900.json"_s), "wine"), json(R"({"env": {"PROTON_NO_NTSYNC": "1"}})"));
        h.stopDaemon();
        QVERIFY(h.startDaemon());
        QCOMPARE(inode(dir), before);
        QCOMPARE(feature(read(u"2238900.json"_s), "wine"), json(R"({"env": {"PROTON_NO_NTSYNC": "1"}})"));
        QVERIFY(set(u"app.2238900.wine.sync"_s, u"auto"_s));
        QVERIFY(!QFile::exists(dir + u"/2238900.json"_s));
        QCOMPARE(inode(dir), before);
    }
};

QTEST_GUILESS_MAIN(ApplierTest)
#include "tst_applier.moc"
