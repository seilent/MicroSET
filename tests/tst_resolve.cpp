#include "harness.h"
#include "protonprofiles.h"
#include "protonresolver.h"
#include "schema.h"
#include "settings.h"
#include "store.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include <memory>

using namespace Qt::StringLiterals;

class ResolveTest : public QObject
{
    Q_OBJECT

private:
    static inline const QByteArray Profiles =
        R"({"version": 1, "profiles": {"292120": {"tool": "proton-11.0-", "set": {"fex.tso": "off", "wine.sync": "fsync"}}}})";

    struct Fixture
    {
        Harness h;
        Schema schema;
        std::unique_ptr<Store> store;
        std::unique_ptr<Settings> settings;
        ProtonProfiles profiles;
        std::unique_ptr<ProtonResolver> resolver;

        explicit Fixture(const QByteArray &state = {})
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
        }

        QString resolved(const QString &appid, const QString &option) const
        {
            const ProtonResolver::Resolved out = resolver->resolve(appid, option);
            return out.value + u' ' + out.source;
        }
    };

private slots:
    void chain_data()
    {
        QTest::addColumn<QByteArray>("state");
        QTest::addColumn<QString>("appid");
        QTest::addColumn<QString>("option");
        QTest::addColumn<QString>("expected");
        const QString ff13 = u"292120"_s;
        const QString so2r = u"2238900"_s;
        QTest::newRow("profile tso") << "{}"_ba << ff13 << u"fex.tso"_s << u"off profile"_s;
        QTest::newRow("profile sync") << "{}"_ba << ff13 << u"wine.sync"_s << u"fsync profile"_s;
        QTest::newRow("option outside the profile") << "{}"_ba << ff13 << u"fex.multiblock"_s << u" default"_s;
        QTest::newRow("no profile tso") << "{}"_ba << so2r << u"fex.tso"_s << u" default"_s;
        QTest::newRow("no profile sync") << "{}"_ba << so2r << u"wine.sync"_s << u"ntsync default"_s;
        QTest::newRow("set over profile") << R"({"app.292120.fex.tso": "on"})"_ba << ff13 << u"fex.tso"_s << u"on set"_s;
        QTest::newRow("set sync over profile") << R"({"app.292120.wine.sync": "ntsync"})"_ba << ff13 << u"wine.sync"_s
                                               << u"ntsync set"_s;
        QTest::newRow("game preset") << R"({"app.2238900.fex.preset": "performance"})"_ba << so2r << u"fex.tso"_s
                                     << u"off preset"_s;
        QTest::newRow("game preset without the option") << R"({"app.2238900.fex.preset": "performance"})"_ba << so2r
                                                        << u"fex.multiblock"_s << u" preset"_s;
        QTest::newRow("game preset over profile") << R"({"app.292120.fex.preset": "default"})"_ba << ff13 << u"fex.tso"_s
                                                  << u" preset"_s;
        QTest::newRow("game preset leaves sync") << R"({"app.292120.fex.preset": "default"})"_ba << ff13
                                                 << u"wine.sync"_s << u"fsync profile"_s;
        QTest::newRow("set over game preset") << R"({"app.292120.fex.preset": "performance", "app.292120.fex.tso": "on"})"_ba
                                              << ff13 << u"fex.tso"_s << u"on set"_s;
        QTest::newRow("global preset") << R"({"fex.preset": "compatibility"})"_ba << so2r << u"fex.half_barrier"_s
                                       << u"off global"_s;
        QTest::newRow("global preset over profile") << R"({"fex.preset": "compatibility"})"_ba << ff13 << u"fex.tso"_s
                                                    << u"on global"_s;
        QTest::newRow("global preset leaves profile sync") << R"({"fex.preset": "compatibility"})"_ba << ff13
                                                           << u"wine.sync"_s << u"fsync profile"_s;
        QTest::newRow("global preset without the option") << R"({"fex.preset": "compatibility"})"_ba << ff13
                                                          << u"fex.multiblock"_s << u" default"_s;
        QTest::newRow("game preset over global preset")
            << R"({"fex.preset": "compatibility", "app.2238900.fex.preset": "performance"})"_ba << so2r
            << u"fex.half_barrier"_s << u" preset"_s;
        QTest::newRow("set over global preset") << R"({"fex.preset": "compatibility", "app.2238900.fex.tso": "off"})"_ba
                                                << so2r << u"fex.tso"_s << u"off set"_s;
        QTest::newRow("global sync") << R"({"wine.sync": "fsync"})"_ba << so2r << u"wine.sync"_s << u"fsync global"_s;
        QTest::newRow("global sync over profile") << R"({"wine.sync": "fsync"})"_ba << ff13 << u"wine.sync"_s
                                                  << u"fsync global"_s;
        QTest::newRow("set sync over global") << R"({"wine.sync": "fsync", "app.2238900.wine.sync": "ntsync"})"_ba << so2r
                                              << u"wine.sync"_s << u"ntsync set"_s;
        QTest::newRow("profile off for the game") << R"({"app.292120.profile": "off"})"_ba << ff13 << u"fex.tso"_s
                                                  << u" default"_s;
        QTest::newRow("profile off for the game sync") << R"({"app.292120.profile": "off"})"_ba << ff13 << u"wine.sync"_s
                                                       << u"ntsync default"_s;
        QTest::newRow("profiles off") << R"({"proton.profiles": "off"})"_ba << ff13 << u"fex.tso"_s << u" default"_s;
        QTest::newRow("profiles off sync") << R"({"proton.profiles": "off"})"_ba << ff13 << u"wine.sync"_s
                                           << u"ntsync default"_s;
        QTest::newRow("profiles off keeps set") << R"({"proton.profiles": "off", "app.292120.fex.tso": "off"})"_ba << ff13
                                                << u"fex.tso"_s << u"off set"_s;
        QTest::newRow("stored global defaults") << R"({"fex.preset": "default", "wine.sync": "ntsync"})"_ba << ff13
                                                << u"fex.tso"_s << u"off profile"_s;
        QTest::newRow("default file global preset") << R"({"fex.preset": "compatibility"})"_ba << QString() << u"fex.tso"_s
                                                    << u"on global"_s;
        QTest::newRow("default file global sync") << R"({"wine.sync": "fsync"})"_ba << QString() << u"wine.sync"_s
                                                  << u"fsync global"_s;
        QTest::newRow("default file skips game values") << R"({"app.292120.fex.tso": "on"})"_ba << QString()
                                                        << u"fex.tso"_s << u" default"_s;
        QTest::newRow("default file skips profiles") << "{}"_ba << QString() << u"wine.sync"_s << u"ntsync default"_s;
    }

    void chain()
    {
        QFETCH(QByteArray, state);
        QFETCH(QString, appid);
        QFETCH(QString, option);
        QFETCH(QString, expected);
        const Fixture f(state);
        QCOMPARE(f.resolved(appid, option), expected);
    }

    void globalSyncSetBackKeepsTheProfile()
    {
        Fixture f;
        QCOMPARE(f.settings->store(u"wine.sync"_s, u"fsync"_s), Settings::Changed);
        QCOMPARE(f.resolved(u"292120"_s, u"wine.sync"_s), u"fsync global"_s);
        QCOMPARE(f.resolved(u"2238900"_s, u"wine.sync"_s), u"fsync global"_s);
        QCOMPARE(f.settings->store(u"wine.sync"_s, u"ntsync"_s), Settings::Changed);
        QVERIFY(f.settings->isStored(u"wine.sync"_s));
        QCOMPARE(f.resolved(u"292120"_s, u"wine.sync"_s), u"fsync profile"_s);
        QCOMPARE(f.resolved(u"2238900"_s, u"wine.sync"_s), u"ntsync default"_s);
    }

    void globalPresetSetBackKeepsTheProfile()
    {
        Fixture f;
        QCOMPARE(f.settings->store(u"fex.preset"_s, u"compatibility"_s), Settings::Changed);
        QCOMPARE(f.resolved(u"292120"_s, u"fex.tso"_s), u"on global"_s);
        QCOMPARE(f.settings->store(u"fex.preset"_s, u"default"_s), Settings::Changed);
        QVERIFY(f.settings->isStored(u"fex.preset"_s));
        QCOMPARE(f.resolved(u"292120"_s, u"fex.tso"_s), u"off profile"_s);
    }

    void withoutTheProfile()
    {
        const Fixture f;
        QCOMPARE(f.resolver->resolve(u"292120"_s, u"fex.tso"_s, false).source, u"default"_s);
        QCOMPARE(f.resolver->resolve(u"292120"_s, u"wine.sync"_s, false).value, u"ntsync"_s);
    }

    void profileInForce()
    {
        Fixture f;
        QVERIFY(f.resolver->profileInForce(u"292120"_s));
        QVERIFY(!f.resolver->profileInForce(u"2238900"_s));
        QVERIFY(!f.resolver->profileInForce(QString()));
        QCOMPARE(f.settings->store(u"app.292120.profile"_s, u"off"_s), Settings::Changed);
        QVERIFY(!f.resolver->profileInForce(u"292120"_s));
        QCOMPARE(f.settings->store(u"app.292120.profile"_s, u"on"_s), Settings::Changed);
        QCOMPARE(f.settings->store(u"proton.profiles"_s, u"off"_s), Settings::Changed);
        QVERIFY(!f.resolver->profileInForce(u"292120"_s));
    }

    void presetRow()
    {
        const Fixture plain;
        QCOMPARE(plain.resolver->preset(u"292120"_s).value, u"default"_s);
        QCOMPARE(plain.resolver->preset(u"292120"_s).source, u"default"_s);
        const Fixture global(R"({"fex.preset": "compatibility"})");
        QCOMPARE(global.resolver->preset(u"292120"_s).value, u"compatibility"_s);
        QCOMPARE(global.resolver->preset(u"292120"_s).source, u"global"_s);
        const Fixture game(R"({"fex.preset": "compatibility", "app.292120.fex.preset": "performance"})");
        QCOMPARE(game.resolver->preset(u"292120"_s).value, u"performance"_s);
        QCOMPARE(game.resolver->preset(u"292120"_s).source, u"set"_s);
        QCOMPARE(game.resolver->preset(QString()).source, u"global"_s);
    }

    void options()
    {
        const Fixture f;
        QCOMPARE(f.resolver->options(u"fex"_s),
                 (QStringList{u"fex.half_barrier"_s, u"fex.max_inst"_s, u"fex.memcpy_tso"_s, u"fex.mono_hacks"_s,
                              u"fex.multiblock"_s, u"fex.smc"_s, u"fex.tso"_s, u"fex.vector_tso"_s,
                              u"fex.volatile_metadata"_s, u"fex.x87_reduced"_s}));
        QCOMPARE(f.resolver->options(u"wine"_s), QStringList{u"wine.sync"_s});
    }

    void apps()
    {
        const Fixture f(R"({"app.2238900.fex.tso": "on", "app.7.profile": "on", "app.8.fex.tso": "auto"})");
        QCOMPARE(f.resolver->apps(), (QStringList{u"2238900"_s, u"292120"_s}));
    }

    void effective()
    {
        const Fixture f(R"({"fex.preset": "compatibility", "app.292120.fex.multiblock": "off"})");
        const QJsonObject view = f.resolver->effective(u"292120"_s);
        QCOMPARE(view.value("appid"_L1).toString(), u"292120"_s);
        const QJsonObject options = view.value("options"_L1).toObject();
        QCOMPARE(options.keys().size(), 15);
        QVERIFY(!options.contains("profile"_L1));
        const auto row = [&](const char *option) {
            const QJsonObject entry = options.value(QLatin1StringView(option)).toObject();
            return entry.value("value"_L1).toString() + u' ' + entry.value("source"_L1).toString();
        };
        QCOMPARE(row("fex.preset"), u"compatibility global"_s);
        QCOMPARE(row("fex.tso"), u"on global"_s);
        QCOMPARE(row("fex.multiblock"), u"off set"_s);
        QCOMPARE(row("fex.max_inst"), u" default"_s);
        QCOMPARE(row("wine.sync"), u"fsync profile"_s);
        for (const QString &option : options.keys())
            if (option != "fex.preset"_L1)
                QCOMPARE(row(qPrintable(option)), f.resolved(u"292120"_s, option));
        const QJsonObject profile = view.value("profile"_L1).toObject();
        QCOMPARE(profile.value("on"_L1), QJsonValue(true));
        QCOMPARE(profile.value("tool"_L1).toString(), u"proton-11.0-"_s);
        QCOMPARE(profile.value("set"_L1).toObject(),
                 (QJsonObject{{u"fex.tso"_s, u"off"_s}, {u"wine.sync"_s, u"fsync"_s}}));
        QCOMPARE(profile.value("overridden"_L1).toArray(), QJsonArray{u"fex.tso"_s});
        QVERIFY(!f.resolver->effective(u"2238900"_s).contains("profile"_L1));
        const Fixture off(R"({"app.292120.profile": "off"})");
        const QJsonObject offProfile = off.resolver->effective(u"292120"_s).value("profile"_L1).toObject();
        QCOMPARE(offProfile.value("on"_L1), QJsonValue(false));
        QCOMPARE(offProfile.value("overridden"_L1).toArray(), QJsonArray());
    }
};

QTEST_GUILESS_MAIN(ResolveTest)
#include "tst_resolve.moc"
