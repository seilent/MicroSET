#include "fakemicrosetd.h"
#include "harness.h"
#include "settingsmodel.h"

#include <QFile>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <memory>

#include <fcntl.h>
#include <sys/stat.h>

using namespace Qt::StringLiterals;

class ModelTest : public QObject
{
    Q_OBJECT

    std::unique_ptr<Harness> h;
    std::unique_ptr<FakeMicroSetd> fake;

    static QStringList keysOf(const QVariantList &rows)
    {
        QStringList out;
        for (const QVariant &row : rows)
            out << row.toMap().value(u"key"_s).toString();
        return out;
    }

    static QVariantMap rowOf(const QVariantList &rows, const QString &key)
    {
        for (const QVariant &row : rows)
            if (row.toMap().value(u"key"_s).toString() == key)
                return row.toMap();
        return {};
    }

    std::unique_ptr<SettingsModel> model()
    {
        auto out = std::make_unique<SettingsModel>(h->client());
        out->load();
        if (!QTest::qWaitFor([&] { return out->available(); }))
            qWarning("model never became available");
        return out;
    }

    static QString value(const QVariantList &rows, const QString &key)
    {
        return rowOf(rows, key).value(u"value"_s).toString();
    }

    static QStringList words(const QVariantMap &sheet)
    {
        return sheet.value(u"words"_s).toStringList();
    }

    static QStringList allFiles()
    {
        return {FakeMicroSetd::shipped(), FakeMicroSetd::fixture(MicroDapSchema), FakeMicroSetd::fixture(MicroEmuSchema)};
    }

    static inline const QString MicroDapSchema = u"50-microdap.json"_s;
    static inline const QString MicroDapCommit = u"MicroDAP d80c538"_s;
    static inline const QString MicroEmuSchema = u"50-microemu.json"_s;
    static inline const QString MicroEmuCommit = u"MicroEMU 6f179c6"_s;

private slots:
    void init()
    {
        h = std::make_unique<Harness>();
        QVERIFY(h->startBus());
        fake = std::make_unique<FakeMicroSetd>(h->address());
        fake->setSchema(FakeMicroSetd::describe({FakeMicroSetd::shipped(), FakeMicroSetd::fixture(u"50-fixture.json"_s)}));
        QVERIFY(fake->start());
        h->writeFile(u"os-release"_s, "NAME=MicroOS\nIMAGE_VERSION=\"testing-38784f1\"\n");
        qputenv("MICROSET_OS_RELEASE", h->path(u"os-release"_s).toUtf8());
        qputenv("MICROSET_UPDATE_FILE", h->path(u"run/update-available"_s).toUtf8());
    }

    void cleanup()
    {
        fake.reset();
        h.reset();
    }

    void rowsInHintOrder()
    {
        fake->setValues({{u"audio.eq.headphones.name"_s, u"HD 600"_s}, {u"audio.eq.speaker.name"_s, u""_s}});
        const auto m = model();
        QVERIFY(m->available());
        QCOMPARE(keysOf(m->rows(u"audio"_s, 0)),
                 (QStringList{u"label-microset"_s, u"audio.output"_s, u"audio.hp_gain"_s, u"audio.hp_amp"_s,
                              u"audio.speaker_gain"_s, u"audio.volume_step"_s, u"audio.max_volume"_s,
                              u"audio.power_on_volume"_s, u"audio.bt_codec_headphones"_s, u"audio.eq.headphones.on"_s,
                              u"audio.eq.headphones.name"_s, u"audio.balance.headphones"_s,
                              u"audio.crossfeed.headphones"_s, u"audio.eq.speaker.on"_s, u"audio.balance.speaker"_s,
                              u"audio.eq.usb.on"_s, u"audio.balance.usb"_s, u"audio.crossfeed.usb"_s,
                              u"audio.eq.bt.on"_s, u"audio.balance.bt"_s}));
        QCOMPARE(keysOf(m->rows(u"power"_s, 0)),
                 (QStringList{u"label-microset"_s, u"power.profile"_s, u"power.tdp"_s, u"power.governor"_s,
                              u"power.gpu_level"_s, u"power.gpu_clock"_s, u"power.game_floor"_s,
                              u"power.gpu_undervolt"_s}));
        QCOMPARE(keysOf(m->rows(u"display"_s, 0)),
                 (QStringList{u"label-microset"_s, u"display.brightness"_s, u"display.orientation"_s,
                              u"display.auto_rotate"_s}));
        QCOMPARE(keysOf(m->rows(u"stick leds"_s, 0)),
                 (QStringList{u"label-microset"_s, u"rgb.enabled"_s, u"rgb.hue"_s, u"rgb.saturation"_s,
                              u"rgb.brightness"_s}));
        QCOMPARE(keysOf(m->rows(u"system"_s, 0)), (QStringList{u"label-microset"_s, u"session.default"_s}));
        QCOMPARE(keysOf(m->rows(u"group with a long title"_s, 0)),
                 (QStringList{u"label-fixture"_s, u"fixture.long"_s, u"fixture.locked"_s, u"fixture.count"_s,
                              u"fixture.mode"_s}));
        QCOMPARE(rowOf(m->rows(u"audio"_s, 0), u"label-microset"_s).value(u"kind"_s).toString(), u"label"_s);
        QVERIFY(m->rows(u"empty"_s, 0).isEmpty());
        QVERIFY(!keysOf(m->groups(0)).contains(u"group-empty"_s));
    }

    void valuesAndKinds()
    {
        fake->setValues({{u"audio.eq.headphones.on"_s, true}, {u"audio.eq.headphones.name"_s, u"HD 600"_s}});
        const auto m = model();
        const QVariantList audio = m->rows(u"audio"_s, 0);
        QCOMPARE(value(audio, u"audio.speaker_gain"_s), u"+18 db"_s);
        QCOMPARE(value(audio, u"audio.volume_step"_s), u"5%"_s);
        QCOMPARE(value(audio, u"audio.max_volume"_s), u"100%"_s);
        QCOMPARE(value(audio, u"audio.power_on_volume"_s), u"last"_s);
        QCOMPARE(value(audio, u"audio.balance.headphones"_s), u"center"_s);
        QCOMPARE(value(audio, u"audio.crossfeed.usb"_s), u"off"_s);
        QCOMPARE(m->text(u"audio.balance.speaker"_s, -3), u"l 1.5 db"_s);
        QCOMPARE(m->text(u"audio.balance.speaker"_s, 20), u"r 10 db"_s);
        QCOMPARE(m->text(u"audio.crossfeed.headphones"_s, u"chuMoy"_s), u"chu moy"_s);
        QCOMPARE(m->text(u"audio.power_on_volume"_s, 40u), u"40%"_s);
        QCOMPARE(value(audio, u"audio.output"_s), u"auto"_s);
        QCOMPARE(rowOf(audio, u"audio.output"_s).value(u"title"_s).toString(), u"Output"_s);
        QCOMPARE(value(audio, u"audio.eq.headphones.on"_s), u"on"_s);
        QCOMPARE(value(audio, u"audio.eq.speaker.on"_s), u"off"_s);
        const QVariantMap name = rowOf(audio, u"audio.eq.headphones.name"_s);
        QCOMPARE(name.value(u"kind"_s).toString(), u"info"_s);
        QVERIFY(name.value(u"readOnly"_s).toBool());
        QCOMPARE(name.value(u"value"_s).toString(), u"HD 600"_s);
        const QVariantList power = m->rows(u"power"_s, 0);
        QCOMPARE(value(power, u"power.tdp"_s), u"10 w"_s);
        QCOMPARE(rowOf(power, u"power.tdp"_s).value(u"title"_s).toString(), u"TDP limit"_s);
        QCOMPARE(value(power, u"power.profile"_s), u"balanced"_s);
        const QVariantList fixture = m->rows(u"group with a long title"_s, 0);
        const QVariantMap locked = rowOf(fixture, u"fixture.locked"_s);
        QCOMPARE(locked.value(u"kind"_s).toString(), u"row"_s);
        QVERIFY(locked.value(u"readOnly"_s).toBool());
        QCOMPARE(locked.value(u"value"_s).toString(), u"on"_s);
        QCOMPARE(rowOf(fixture, u"fixture.count"_s).value(u"kind"_s).toString(), u"info"_s);
        QCOMPARE(value(fixture, u"fixture.count"_s), u"3"_s);
        QVERIFY(!m->beginEdit(u"fixture.locked"_s));
        QVERIFY(!m->set(u"fixture.locked"_s, false));
        QVERIFY(!m->beginEdit(u"fixture.count"_s));
        QVERIFY(!m->beginEdit(u"audio.eq.headphones.name"_s));
        QCOMPARE(m->text(u"display.orientation"_s, u"portrait-flipped"_s), u"portrait flipped"_s);
        QCOMPARE(m->text(u"power.gpu_clock"_s, 442u), u"441.6 mhz"_s);
        QCOMPARE(m->text(u"power.gpu_clock"_s, 490u), u"490 mhz"_s);
    }

    void undervoltStates()
    {
        fake->setValues({{u"power.gpu_undervolt"_s, u"low"_s}, {u"power.gpu_undervolt_active"_s, u"low"_s}});
        const auto m = model();
        QSignalSpy refused(m.get(), &SettingsModel::refused);
        const auto power = [&m] { return m->rows(u"power"_s, m->revision()); };
        QCOMPARE(rowOf(power(), u"power.gpu_undervolt"_s).value(u"kind"_s).toString(), u"row"_s);
        QCOMPARE(rowOf(power(), u"power.gpu_undervolt"_s).value(u"title"_s).toString(), u"GPU undervolt"_s);
        QCOMPARE(value(power(), u"power.gpu_undervolt"_s), u"Low"_s);
        QCOMPARE(m->text(u"power.gpu_undervolt"_s, u"off"_s), u"Off"_s);
        QVERIFY(!keysOf(power()).contains(u"power.gpu_tuning_fallback"_s));
        fake->emitChanged(u"power.gpu_undervolt_active"_s, u"waiting"_s);
        QTRY_COMPARE(value(power(), u"power.gpu_undervolt"_s), u"Stock until GPU restart"_s);
        fake->emitChanged(u"power.gpu_undervolt_active"_s, u"stock"_s);
        QTRY_COMPARE(value(power(), u"power.gpu_undervolt"_s), u"Low, next boot"_s);
        fake->emitChanged(u"power.gpu_undervolt"_s, u"off"_s);
        QTRY_COMPARE(value(power(), u"power.gpu_undervolt"_s), u"Off"_s);
        fake->emitChanged(u"power.gpu_undervolt_active"_s, u"waiting"_s);
        QTRY_COMPARE(value(power(), u"power.gpu_undervolt"_s), u"Off, next boot"_s);
        fake->emitChanged(u"power.gpu_undervolt_active"_s, u"low"_s);
        QTRY_COMPARE(value(power(), u"power.gpu_undervolt"_s), u"Off, next boot"_s);
        fake->emitChanged(u"power.gpu_tuning_fallback"_s, u"unclean"_s);
        QTRY_COMPARE(value(power(), u"power.gpu_tuning_fallback"_s), u"Unclean shutdown"_s);
        const QVariantMap reason = rowOf(power(), u"power.gpu_tuning_fallback"_s);
        QCOMPARE(reason.value(u"kind"_s).toString(), u"info"_s);
        QCOMPARE(reason.value(u"title"_s).toString(), u"Undervolt turned off"_s);
        QVERIFY(reason.value(u"readOnly"_s).toBool());
        QCOMPARE(keysOf(power()).indexOf(u"power.gpu_tuning_fallback"_s),
                 keysOf(power()).indexOf(u"power.gpu_undervolt"_s) + 1);
        QCOMPARE(m->text(u"power.gpu_tuning_fallback"_s, u"hfi"_s), u"GMU timeout"_s);
        fake->emitChanged(u"power.gpu_tuning_fallback"_s, QString());
        QTRY_VERIFY(!keysOf(power()).contains(u"power.gpu_tuning_fallback"_s));
        QVERIFY(!keysOf(power()).contains(u"power.gpu_tuning_trip"_s));
        fake->emitChanged(u"power.gpu_tuning_trip"_s, u"hang"_s);
        QTRY_COMPARE(value(power(), u"power.gpu_tuning_trip"_s), u"GPU hang"_s);
        const QVariantMap trip = rowOf(power(), u"power.gpu_tuning_trip"_s);
        QCOMPARE(trip.value(u"kind"_s).toString(), u"info"_s);
        QCOMPARE(trip.value(u"title"_s).toString(), u"Stock until reboot"_s);
        QVERIFY(trip.value(u"readOnly"_s).toBool());
        QCOMPARE(keysOf(power()).indexOf(u"power.gpu_tuning_trip"_s),
                 keysOf(power()).indexOf(u"power.gpu_undervolt"_s) + 1);
        QCOMPARE(m->text(u"power.gpu_tuning_trip"_s, u"start"_s), u"GPU failed to start"_s);
        fake->emitChanged(u"power.gpu_tuning_fallback"_s, u"start"_s);
        QTRY_COMPARE(value(power(), u"power.gpu_tuning_fallback"_s), u"GPU failed to start"_s);
        QCOMPARE(keysOf(power()).indexOf(u"power.gpu_tuning_fallback"_s),
                 keysOf(power()).indexOf(u"power.gpu_undervolt"_s) + 1);
        QVERIFY(!keysOf(power()).contains(u"power.gpu_tuning_trip"_s));
        QCOMPARE(keysOf(power()).last(), u"power.gpu_tuning_fallback"_s);
        fake->emitChanged(u"power.gpu_tuning_fallback"_s, QString());
        QTRY_COMPARE(value(power(), u"power.gpu_tuning_trip"_s), u"GPU hang"_s);
        fake->emitChanged(u"power.gpu_tuning_trip"_s, QString());
        QTRY_VERIFY(!keysOf(power()).contains(u"power.gpu_tuning_trip"_s));
        fake->refuse(u"power.gpu_undervolt"_s, u"net.seilent.MicroSet1.Error.HoldPending"_s);
        QVERIFY(m->set(u"power.gpu_undervolt"_s, u"low"_s));
        QTRY_COMPARE(value(power(), u"power.gpu_undervolt"_s), u"Off, next boot"_s);
        QTest::qWait(100);
        QCOMPARE(refused.count(), 0);
        fake->emitHoldRefused(u"power.gpu_undervolt"_s, u"A not held"_s);
        fake->emitHoldRefused(u"power.gpu_undervolt"_s, u"A not held"_s);
        fake->emitHoldRefused(u"nope.key"_s, u"A not held"_s);
        QTRY_COMPARE(refused.count(), 2);
        QCOMPARE(refused.at(0).at(0).toString(), u"GPU undervolt"_s);
        QCOMPARE(refused.at(0).at(1).toString(), u"A not held"_s);
        fake->refuse(u"power.gpu_undervolt"_s, u"org.freedesktop.DBus.Error.LimitsExceeded"_s);
        QVERIFY(m->set(u"power.gpu_undervolt"_s, u"low"_s));
        QTRY_COMPARE(refused.count(), 3);
        QCOMPARE(refused.at(2).at(1).toString(), u"try again shortly"_s);
    }

    void locks()
    {
        fake->setValues({{u"display.rotation"_s, u"landscape-flipped"_s}, {u"display.orientation"_s, u"portrait"_s}});
        const auto m = model();
        QVariantMap orientation = rowOf(m->rows(u"display"_s, 0), u"display.orientation"_s);
        QVERIFY(orientation.value(u"readOnly"_s).toBool());
        QCOMPARE(orientation.value(u"value"_s).toString(), u"landscape flipped"_s);
        QVERIFY(!m->beginEdit(u"display.orientation"_s));
        fake->emitChanged(u"display.rotation"_s, u"landscape"_s);
        QTRY_COMPARE(value(m->rows(u"display"_s, 0), u"display.orientation"_s), u"landscape"_s);
        fake->emitChanged(u"display.auto_rotate"_s, false);
        QTRY_VERIFY(!rowOf(m->rows(u"display"_s, 0), u"display.orientation"_s).value(u"readOnly"_s).toBool());
        QCOMPARE(value(m->rows(u"display"_s, 0), u"display.orientation"_s), u"portrait"_s);
        QVERIFY(m->beginEdit(u"display.orientation"_s));
        QVERIFY(rowOf(m->rows(u"display"_s, 0), u"display.orientation"_s).value(u"confirm"_s).toBool());
        const QVariantList power = m->rows(u"power"_s, 0);
        QCOMPARE(value(power, u"power.gpu_clock"_s), u"auto"_s);
        QVERIFY(rowOf(power, u"power.gpu_clock"_s).value(u"readOnly"_s).toBool());
        QVERIFY(!m->next(u"power.gpu_clock"_s, {}, 1).isValid());
        const QVariantList leds = m->rows(u"stick leds"_s, 0);
        for (const QString &key : {u"rgb.hue"_s, u"rgb.saturation"_s, u"rgb.brightness"_s}) {
            QCOMPARE(value(leds, key), u"off"_s);
            QVERIFY(rowOf(leds, key).value(u"readOnly"_s).toBool());
        }
        QCOMPARE(rowOf(leds, u"rgb.hue"_s).value(u"widget"_s).toString(), QString());
        QCOMPARE(rowOf(leds, u"rgb.hue"_s).value(u"swatch"_s).toString(), QString());
    }

    void liveWhileEntered()
    {
        const auto m = model();
        m->enter(u"display"_s);
        m->enter(u"display"_s);
        QTRY_COMPARE(fake->subscriptions(), QStringList{u"Subscribe display.rotation"_s});
        m->leave(u"display"_s);
        QTest::qWait(50);
        QCOMPARE(fake->subscriptions().size(), 1);
        m->leave(u"display"_s);
        QTRY_COMPARE(fake->subscriptions().size(), 2);
        QCOMPARE(fake->subscriptions().at(1), u"Unsubscribe display.rotation"_s);
        m->enter(u"audio"_s);
        m->leave(u"power"_s);
        QTest::qWait(50);
        QCOMPARE(fake->subscriptions().size(), 2);
    }

    void heldKeyFollowed()
    {
        const auto m = model();
        m->hold(u"display.rotation"_s);
        m->enter(u"display"_s);
        QTRY_COMPARE(fake->subscriptions(), QStringList{u"Subscribe display.rotation"_s});
        m->leave(u"display"_s);
        const int revision = m->revision();
        fake->emitChanged(u"display.rotation"_s, u"landscape"_s);
        QTRY_VERIFY(m->revision() > revision);
        QCOMPARE(m->value(u"display.rotation"_s, m->revision()).toString(), u"landscape"_s);
        QTest::qWait(50);
        QCOMPARE(fake->subscriptions().size(), 1);
        m->drop(u"display.rotation"_s);
        QTRY_COMPARE(fake->subscriptions().size(), 2);
        QCOMPARE(fake->subscriptions().at(1), u"Unsubscribe display.rotation"_s);
        m->drop(u"display.rotation"_s);
        QTest::qWait(50);
        QCOMPARE(fake->subscriptions().size(), 2);
    }

    void heldUnreferencedKeyBumps()
    {
        const auto m = model();
        int revision = m->revision();
        fake->emitChanged(u"other.unheld"_s, u"a"_s);
        QTest::qWait(100);
        QCOMPARE(m->revision(), revision);
        QCOMPARE(m->value(u"other.unheld"_s, m->revision()).toString(), u"a"_s);
        m->hold(u"other.unheld"_s);
        revision = m->revision();
        fake->emitChanged(u"other.unheld"_s, u"b"_s);
        QTRY_VERIFY(m->revision() > revision);
        QCOMPARE(m->value(u"other.unheld"_s, m->revision()).toString(), u"b"_s);
        m->drop(u"other.unheld"_s);
        revision = m->revision();
        fake->emitChanged(u"other.unheld"_s, u"c"_s);
        QTest::qWait(100);
        QCOMPARE(m->revision(), revision);
    }

    void storedUnderOverride()
    {
        fake->setValues({{u"rgb.hue"_s, 120u}, {u"rgb.saturation"_s, 60u}, {u"rgb.enabled"_s, true}});
        fake->setStored({{u"rgb.enabled"_s, true}});
        auto m = model();
        QVariantMap hue = rowOf(m->rows(u"stick leds"_s, 0), u"rgb.hue"_s);
        QCOMPARE(hue.value(u"widget"_s).toString(), u"hue"_s);
        QCOMPARE(hue.value(u"value"_s).toString(), QString());
        QCOMPARE(hue.value(u"swatch"_s).toString(), u"#ff0000"_s);
        QCOMPARE(hue.value(u"hue"_s).toInt(), 0);
        QCOMPARE(value(m->rows(u"stick leds"_s, 0), u"rgb.saturation"_s), u"0 %"_s);
        QCOMPARE(value(m->rows(u"stick leds"_s, 0), u"rgb.brightness"_s), u"100 %"_s);
        fake->setStored({{u"rgb.enabled"_s, true}, {u"rgb.hue"_s, 150u}});
        m = model();
        hue = rowOf(m->rows(u"stick leds"_s, 0), u"rgb.hue"_s);
        QCOMPARE(hue.value(u"swatch"_s).toString(), u"#00ff80"_s);
        QCOMPARE(hue.value(u"hue"_s).toInt(), 150);
        QCOMPARE(hue.value(u"current"_s).toUInt(), 150u);
        fake->setStored({{u"rgb.enabled"_s, true}, {u"rgb.hue"_s, 203u}});
        fake->emitChanged(u"rgb.hue"_s, 300u);
        QTRY_COMPARE(rowOf(m->rows(u"stick leds"_s, 0), u"rgb.hue"_s).value(u"swatch"_s).toString(), u"#009dff"_s);
        QCOMPARE(rowOf(m->rows(u"stick leds"_s, 0), u"rgb.hue"_s).value(u"hue"_s).toInt(), 200);
        QVERIFY(m->set(u"rgb.hue"_s, 0));
        QTRY_COMPARE(fake->sets().size(), 1);
        QCOMPARE(fake->sets().at(0).second, QVariant(0u));
        QTRY_COMPARE(rowOf(m->rows(u"stick leds"_s, 0), u"rgb.hue"_s).value(u"swatch"_s).toString(), u"#ff0000"_s);
    }

    void toastTexts()
    {
        QCOMPARE(SettingsModel::toastText(u"net.seilent.MicroSet1.Error.InvalidValue"_s), u"set failed"_s);
        QCOMPARE(SettingsModel::toastText(u"org.freedesktop.DBus.Error.AccessDenied"_s), u"set failed"_s);
        QCOMPARE(SettingsModel::toastText(u"org.freedesktop.DBus.Error.ServiceUnknown"_s), u"not available"_s);
        QCOMPARE(SettingsModel::toastText(u"org.freedesktop.DBus.Error.NoReply"_s), u"not available"_s);
        QCOMPARE(SettingsModel::toastText(u"org.freedesktop.DBus.Error.LimitsExceeded"_s), u"try again shortly"_s);
    }

    void groupsMerged()
    {
        fake->setSchema(FakeMicroSetd::describe({FakeMicroSetd::shipped(), FakeMicroSetd::fixture(u"50-fixture.json"_s),
                                                 FakeMicroSetd::fixture(u"60-microdap.json"_s)}));
        fake->setValues({{u"microdap.accent_hue"_s, 120u}});
        const auto m = model();
        QCOMPARE(keysOf(m->groups(0)),
                 (QStringList{u"label-microset"_s, u"group-audio"_s, u"group-display"_s, u"group-stick leds"_s,
                              u"group-power"_s, u"group-games"_s, u"group-system"_s, u"group-updates"_s, u"label-fixture"_s,
                              u"group-group with a long title"_s}));
        const QVariantMap leds = rowOf(m->groups(0), u"group-stick leds"_s);
        QCOMPARE(leds.value(u"title"_s).toString(), u"stick leds"_s);
        QCOMPARE(leds.value(u"group"_s).toString(), u"stick leds"_s);
        QCOMPARE(keysOf(m->rows(u"display"_s, 0)),
                 (QStringList{u"label-microset"_s, u"display.brightness"_s, u"display.orientation"_s,
                              u"display.auto_rotate"_s, u"label-microdap"_s, u"microdap.accent_hue"_s}));
        QCOMPARE(m->accentHue(), 120);
        const QVariantMap accent = rowOf(m->rows(u"display"_s, 0), u"microdap.accent_hue"_s);
        QCOMPARE(accent.value(u"hue"_s).toInt(), 120);
        QCOMPARE(accent.value(u"swatch"_s).toString(), QString());
    }

    void gameList()
    {
        QTemporaryDir steam;
        qputenv("MICROSET_STEAM_DIR", steam.path().toUtf8());
        FakeMicroSetd::writeSteam(steam.path(),
                                  {{u"292120"_s, u"FINAL FANTASY XIII"_s}, {u"2238900"_s, u"STAR OCEAN THE SECOND STORY R"_s},
                                   {u"993090"_s, u"Lossless Scaling"_s}, {u"5"_s, u"<b>bold</b>"_s}},
                                  {{2763258687u, u"Octopath Traveler"_s}});
        const QStringList files{FakeMicroSetd::shipped()};
        fake->setSchema(FakeMicroSetd::describe(files));
        fake->setProton(files, FakeMicroSetd::Profiles);
        const QVariantMap stored{{u"app.2238900.fex.preset"_s, u"performance"_s}, {u"app.77.fex.tso"_s, u"on"_s},
                                 {u"app.78.fex.tso"_s, u"auto"_s}, {u"app.993090.fex.tso"_s, u"on"_s}};
        const QString apps = steam.path() + u"/steamapps"_s;
        const QString acf = apps + u"/appmanifest_292120.acf"_s;
        const auto pin = [](const QString &path, long nsec = 0) {
            const timespec times[2] = {{1000, nsec}, {1000, nsec}};
            QCOMPARE(::utimensat(AT_FDCWD, QFile::encodeName(path).constData(), times, 0), 0);
        };
        pin(apps);
        pin(acf);
        fake->setValues(stored);
        fake->setStored(stored);
        const auto m = model();
        const QStringList globals{u"label-microset"_s, u"fex.preset"_s, u"wine.sync"_s, u"proton.profiles"_s};
        QCOMPARE(keysOf(m->rows(u"games"_s, 0)), globals);
        m->enter(u"games"_s);
        QTRY_COMPARE(value(m->rows(u"games"_s, 0), u"game-2238900"_s), u"performance"_s);
        QTRY_COMPARE(value(m->rows(u"games"_s, 0), u"game-292120"_s), u"default"_s);
        const QVariantList rows = m->rows(u"games"_s, 0);
        QCOMPARE(keysOf(rows), (globals + QStringList{u"label-games"_s, u"game-5"_s, u"game-292120"_s, u"game-2763258687"_s,
                                                      u"game-2238900"_s, u"game-77"_s}));
        QCOMPARE(rowOf(rows, u"label-games"_s).value(u"kind"_s).toString(), u"label"_s);
        QCOMPARE(rowOf(rows, u"label-games"_s).value(u"title"_s).toString(), u"games"_s);
        const QVariantMap ff13 = rowOf(rows, u"game-292120"_s);
        QCOMPARE(ff13.value(u"kind"_s).toString(), u"row"_s);
        QCOMPARE(ff13.value(u"group"_s).toString(), u"app.292120"_s);
        QCOMPARE(ff13.value(u"title"_s).toString(), u"FINAL FANTASY XIII"_s);
        QVERIFY(ff13.value(u"readOnly"_s).toBool());
        QCOMPARE(rowOf(rows, u"game-5"_s).value(u"title"_s).toString(), u"<b>bold</b>"_s);
        const QVariantMap gone = rowOf(rows, u"game-77"_s);
        QCOMPARE(gone.value(u"kind"_s).toString(), u"info"_s);
        QCOMPARE(gone.value(u"title"_s).toString(), u"77"_s);
        QCOMPARE(gone.value(u"value"_s).toString(), u"not installed"_s);
        QVERIFY(!gone.contains(u"group"_s));
        QCOMPARE(m->pageTitle(u"app.292120"_s, 0), u"FINAL FANTASY XIII"_s);
        QCOMPARE(m->pageTitle(u"app.2763258687"_s, 0), u"Octopath Traveler"_s);
        QCOMPARE(m->pageTitle(u"app.6"_s, 0), u"6"_s);
        QCOMPARE(m->pageTitle(u"audio"_s, 0), u"audio"_s);
        QVERIFY(m->set(u"fex.preset"_s, u"compatibility"_s));
        QTRY_COMPARE(value(m->rows(u"games"_s, 0), u"game-292120"_s), u"compatibility"_s);
        QCOMPARE(value(m->rows(u"games"_s, 0), u"game-2238900"_s), u"performance"_s);
        fake->emitChanged(u"app.78.fex.tso"_s, u"off"_s);
        QTRY_COMPARE(keysOf(m->rows(u"games"_s, 0)).last(), u"game-78"_s);
        fake->setValues(stored);
        fake->setStored({{u"app.2238900.fex.preset"_s, u"compatibility"_s}, {u"fex.preset"_s, u"compatibility"_s}});
        m->load();
        QTRY_COMPARE(value(m->rows(u"games"_s, 0), u"game-2238900"_s), u"compatibility"_s);
        QFile renamed(acf);
        QVERIFY(renamed.open(QIODevice::ReadWrite));
        const QByteArray same = renamed.readAll().replace("FINAL FANTASY XIII", "FF XIII same bytes");
        QVERIFY(renamed.seek(0));
        QCOMPARE(renamed.write(same), same.size());
        renamed.close();
        pin(acf);
        m->enter(u"games"_s);
        QCOMPARE(rowOf(m->rows(u"games"_s, 0), u"game-292120"_s).value(u"title"_s).toString(), u"FINAL FANTASY XIII"_s);
        pin(acf, 1);
        m->enter(u"games"_s);
        QCOMPARE(rowOf(m->rows(u"games"_s, 0), u"game-292120"_s).value(u"title"_s).toString(), u"FF XIII same bytes"_s);
        FakeMicroSetd::writeSteam(steam.path(), {{u"2161700"_s, u"Persona 3 Reload"_s}});
        m->enter(u"games"_s);
        QVERIFY(keysOf(m->rows(u"games"_s, 0)).contains(u"game-2161700"_s));
        qunsetenv("MICROSET_STEAM_DIR");
    }

    void gamePage()
    {
        QTemporaryDir steam;
        qputenv("MICROSET_STEAM_DIR", steam.path().toUtf8());
        FakeMicroSetd::writeSteam(steam.path(), {{u"292120"_s, u"FINAL FANTASY XIII"_s}});
        const QStringList files{FakeMicroSetd::shipped()};
        fake->setSchema(FakeMicroSetd::describe(files));
        fake->setProton(files, FakeMicroSetd::Profiles);
        const auto m = model();
        const QString ff13 = u"app.292120"_s;
        const QString so2r = u"app.2238900"_s;
        QVERIFY(m->rows(ff13, 0).isEmpty());
        m->enter(ff13);
        QVERIFY(m->rows(ff13, 0).isEmpty());
        m->enter(so2r);
        const auto key = [](const QString &page, const QString &option) { return page + u'.' + option; };
        QTRY_VERIFY(keysOf(m->rows(ff13, 0)).contains(key(ff13, u"profile"_s)));
        QCOMPARE(m->pageTitle(ff13, 0), u"FINAL FANTASY XIII"_s);
        QCOMPARE(m->pageTitle(so2r, 0), u"2238900"_s);
        FakeMicroSetd::writeSteam(steam.path(), {{u"2238900"_s, u"STAR OCEAN THE SECOND STORY R"_s}});
        m->enter(so2r);
        QCOMPARE(m->pageTitle(so2r, 0), u"STAR OCEAN THE SECOND STORY R"_s);
        const QStringList options{u"fex.preset"_s, u"fex.tso"_s, u"fex.half_barrier"_s, u"fex.vector_tso"_s,
                                  u"fex.memcpy_tso"_s, u"fex.multiblock"_s, u"fex.max_inst"_s, u"fex.x87_reduced"_s,
                                  u"fex.smc"_s, u"fex.volatile_metadata"_s, u"fex.mono_hacks"_s, u"wine.sync"_s};
        QStringList wanted{u"label-microset"_s, key(ff13, u"profile"_s)};
        for (const QString &option : options)
            wanted << key(ff13, option);
        QCOMPARE(keysOf(m->rows(ff13, 0)), wanted + QStringList{u"game-next"_s});
        QVERIFY(!keysOf(m->rows(so2r, 0)).contains(key(so2r, u"profile"_s)));
        QCOMPARE(rowOf(m->rows(ff13, 0), u"game-next"_s).value(u"title"_s).toString(), u"Applies at next launch"_s);
        QCOMPARE(rowOf(m->rows(ff13, 0), u"game-next"_s).value(u"kind"_s).toString(), u"info"_s);
        const QVariantMap tso = rowOf(m->rows(ff13, 0), key(ff13, u"fex.tso"_s));
        QCOMPARE(tso.value(u"title"_s).toString(), u"TSO"_s);
        QCOMPARE(tso.value(u"pick"_s).toString(), u"sheet"_s);
        QVERIFY(tso.value(u"confirm"_s).toBool());
        const auto values = [&](const QString &page) {
            QStringList out;
            for (const QString &option : QStringList{u"profile"_s} + options)
                out << value(m->rows(page, 0), key(page, option));
            return out;
        };
        const QString none = u"auto (default)"_s;
        QCOMPARE(values(ff13), (QStringList{u"on (TSO off, fsync)"_s, none, u"auto (off, profile)"_s, none, none, none, none,
                                            none, none, none, none, none, u"auto (fsync, profile)"_s}));
        QCOMPARE(values(so2r), (QStringList{QString(), none, none, none, none, none, none, none, none, none, none, none,
                                            u"auto (ntsync, default)"_s}));
        QCOMPARE(words(m->sheet(key(ff13, u"fex.tso"_s))), (QStringList{u"auto (off, profile)"_s, u"on"_s, u"off"_s}));
        QCOMPARE(m->sheet(key(ff13, u"fex.tso"_s)).value(u"mark"_s).toInt(), 0);
        QCOMPARE(words(m->sheet(key(ff13, u"profile"_s))), (QStringList{u"on"_s, u"off"_s}));
        QCOMPARE(m->text(key(ff13, u"profile"_s), u"off"_s), u"off"_s);

        QVERIFY(m->set(key(so2r, u"fex.preset"_s), u"performance"_s));
        QTRY_COMPARE(value(m->rows(so2r, 0), key(so2r, u"fex.tso"_s)), u"auto (off, preset)"_s);
        QCOMPARE(value(m->rows(so2r, 0), key(so2r, u"fex.multiblock"_s)), u"auto (default, preset)"_s);
        QCOMPARE(value(m->rows(so2r, 0), key(so2r, u"fex.preset"_s)), u"performance"_s);
        QCOMPARE(words(m->sheet(key(so2r, u"fex.preset"_s))),
                 (QStringList{u"auto"_s, u"default"_s, u"compatibility"_s, u"performance"_s}));

        QVERIFY(m->set(key(ff13, u"fex.tso"_s), u"on"_s));
        QTRY_COMPARE(value(m->rows(ff13, 0), key(ff13, u"profile"_s)), u"on (TSO off overridden, fsync)"_s);
        QCOMPARE(value(m->rows(ff13, 0), key(ff13, u"fex.tso"_s)), u"on"_s);
        QCOMPARE(words(m->sheet(key(ff13, u"fex.tso"_s))), (QStringList{u"auto"_s, u"on"_s, u"off"_s}));

        QVERIFY(m->set(u"fex.preset"_s, u"compatibility"_s));
        QVERIFY(m->set(u"wine.sync"_s, u"fsync"_s));
        QTRY_COMPARE(value(m->rows(so2r, 0), key(so2r, u"wine.sync"_s)), u"auto (fsync, global)"_s);
        QTRY_COMPARE(value(m->rows(ff13, 0), key(ff13, u"fex.half_barrier"_s)), u"auto (off, global)"_s);
        QCOMPARE(value(m->rows(ff13, 0), key(ff13, u"fex.preset"_s)), u"auto (global)"_s);
        QCOMPARE(value(m->rows(ff13, 0), key(ff13, u"fex.tso"_s)), u"on"_s);
        QCOMPARE(value(m->rows(ff13, 0), key(ff13, u"wine.sync"_s)), u"auto (fsync, global)"_s);
        QCOMPARE(value(m->rows(ff13, 0), key(ff13, u"profile"_s)), u"on (TSO off overridden, fsync overridden)"_s);
        QCOMPARE(value(m->rows(so2r, 0), key(so2r, u"fex.tso"_s)), u"auto (off, preset)"_s);

        QVERIFY(m->set(u"fex.preset"_s, u"default"_s));
        QVERIFY(m->set(u"wine.sync"_s, u"ntsync"_s));
        QVERIFY(m->set(key(ff13, u"fex.tso"_s), u"auto"_s));
        QVERIFY(m->set(key(ff13, u"profile"_s), u"off"_s));
        QTRY_COMPARE(value(m->rows(ff13, 0), key(ff13, u"profile"_s)), u"off"_s);
        QTRY_COMPARE(value(m->rows(ff13, 0), key(ff13, u"fex.tso"_s)), none);
        QCOMPARE(value(m->rows(ff13, 0), key(ff13, u"wine.sync"_s)), u"auto (ntsync, default)"_s);
        QCOMPARE(value(m->rows(ff13, 0), key(ff13, u"fex.preset"_s)), none);
        QVERIFY(!keysOf(m->groups(0)).contains(u"group-"_s + ff13));
        QVERIFY(!keysOf(m->groups(0)).contains(u"group-"_s + so2r));
        m->load();
        QTRY_COMPARE(value(m->rows(ff13, 0), key(ff13, u"profile"_s)), u"off"_s);
        QVERIFY(m->set(key(ff13, u"profile"_s), u"on"_s));
        QTRY_COMPARE(value(m->rows(ff13, 0), key(ff13, u"profile"_s)), u"on (TSO off, fsync)"_s);
        QVERIFY(m->set(u"proton.profiles"_s, u"off"_s));
        QTRY_COMPARE(value(m->rows(ff13, 0), key(ff13, u"profile"_s)), u"on"_s);
        QCOMPARE(value(m->rows(ff13, 0), key(ff13, u"fex.tso"_s)), none);
        QCOMPARE(m->text(key(ff13, u"profile"_s), u"off"_s), u"off"_s);
        qunsetenv("MICROSET_STEAM_DIR");
    }

    void gameLeads()
    {
        const QStringList files{FakeMicroSetd::shipped()};
        fake->setSchema(FakeMicroSetd::describe(files));
        fake->setProton(files, FakeMicroSetd::Profiles);
        const auto m = model();
        m->enter(u"app.292120"_s);
        QTRY_COMPARE(value(m->rows(u"app.292120"_s, 0), u"app.292120.fex.tso"_s), u"auto (off, profile)"_s);
        const auto leads = [&](const QString &key) { return m->sheet(key).value(u"leads"_s).toStringList(); };
        const QString tso = u"may crash, hang or damage saves"_s;
        const QString strict = u"slower, strict x86 ordering"_s;
        QCOMPARE(leads(u"app.292120.fex.tso"_s), (QStringList{tso, QString(), tso}));
        QCOMPARE(leads(u"app.292120.profile"_s), (QStringList{u"TSO off: "_s + tso, QString()}));
        QCOMPARE(leads(u"app.292120.fex.preset"_s), (QStringList{QString(), QString(), strict, u"TSO off: "_s + tso}));
        QCOMPARE(leads(u"app.292120.fex.half_barrier"_s), (QStringList{QString(), QString(), u"safer, slower"_s}));
        QCOMPARE(leads(u"app.292120.fex.multiblock"_s), (QStringList{QString(), QString(), u"much slower, less stutter"_s}));
        QCOMPARE(leads(u"app.292120.fex.max_inst"_s),
                 (QStringList{QString(), QString(), QString(), QString(), u"5000 hung 1 load in 3"_s}));
        QCOMPARE(leads(u"app.292120.fex.x87_reduced"_s), (QStringList{QString(), u"may break rendering"_s, QString()}));
        QCOMPARE(leads(u"app.292120.fex.smc"_s),
                 (QStringList{QString(), u"breaks self modifying code"_s, QString(), u"very slow"_s}));
        QCOMPARE(leads(u"app.292120.wine.sync"_s), (QStringList{QString(), QString(), QString()}));
        QCOMPARE(leads(u"fex.preset"_s), (QStringList{QString(), strict}));
        QCOMPARE(leads(u"audio.volume_step"_s), (QStringList{QString(), QString(), QString()}));
        QVERIFY(m->set(u"app.292120.fex.tso"_s, u"on"_s));
        QTRY_COMPARE(leads(u"app.292120.fex.tso"_s), (QStringList{QString(), QString(), tso}));
        QCOMPARE(leads(u"app.292120.profile"_s), (QStringList{QString(), QString()}));
        QVERIFY(m->set(u"fex.preset"_s, u"compatibility"_s));
        QTRY_COMPARE(leads(u"app.292120.fex.preset"_s), (QStringList{strict, QString(), strict, u"TSO off: "_s + tso}));
        QVERIFY(m->set(u"app.292120.fex.tso"_s, u"auto"_s));
        QTRY_COMPARE(leads(u"app.292120.fex.tso"_s), (QStringList{QString(), QString(), tso}));
    }

    void gameListNeedsTemplates()
    {
        QTemporaryDir steam;
        qputenv("MICROSET_STEAM_DIR", steam.path().toUtf8());
        FakeMicroSetd::writeSteam(steam.path(), {{u"292120"_s, u"FINAL FANTASY XIII"_s}});
        QJsonObject schema = FakeMicroSetd::describe({FakeMicroSetd::shipped()});
        for (const QString &key : schema.keys())
            if (key.startsWith("app."_L1))
                schema.remove(key);
        fake->setSchema(schema);
        const auto m = model();
        m->enter(u"games"_s);
        QCOMPARE(keysOf(m->rows(u"games"_s, 0)),
                 (QStringList{u"label-microset"_s, u"fex.preset"_s, u"wine.sync"_s, u"proton.profiles"_s}));
        qunsetenv("MICROSET_STEAM_DIR");
    }

    void accentAbsent()
    {
        const auto m = model();
        QCOMPARE(m->accentHue(), -1);
        QVERIFY(!keysOf(m->groups(0)).contains(u"label-microdap"_s));
    }

    void updates()
    {
        const auto m = model();
        QCOMPARE(keysOf(m->rows(u"updates"_s, 0)), (QStringList{u"label-microset"_s, u"image"_s, u"update"_s}));
        QCOMPARE(value(m->rows(u"updates"_s, 0), u"image"_s), u"testing-38784f1"_s);
        QCOMPARE(value(m->rows(u"updates"_s, 0), u"update"_s), u"not checked"_s);
        h->writeFile(u"run/update-available"_s, "state=available\nchecked=1\nversion=testing-0628a41\n");
        QTRY_COMPARE(value(m->rows(u"updates"_s, 0), u"update"_s), u"testing-0628a41 available"_s);
        QFile::remove(h->path(u"run/update-available"_s));
        h->writeFile(u"run/update-available"_s, "state=withdrawn\nchecked=2\n");
        QTRY_COMPARE(value(m->rows(u"updates"_s, 0), u"update"_s), u"not checked"_s);
        QCOMPARE(UpdateInfo::text(u"none"_s, {}), u"up to date"_s);
        QCOMPARE(UpdateInfo::text(u"offline"_s, {}), u"offline"_s);
        QCOMPARE(UpdateInfo::checkStatus(u"ExecMainStatus=7\nExecMainStartTimestampMonotonic=12\n"_s), u"none"_s);
        QCOMPARE(UpdateInfo::checkStatus(u"ExecMainStatus=4\nExecMainStartTimestampMonotonic=12\n"_s), u"offline"_s);
        QCOMPARE(UpdateInfo::checkStatus(u"ExecMainStatus=0\nExecMainStartTimestampMonotonic=0\n"_s), u"unchecked"_s);
    }

    void scaledAndFloored()
    {
        fake->setSchema(FakeMicroSetd::describe({FakeMicroSetd::shipped(), FakeMicroSetd::fixture(u"70-scale.json"_s)}));
        fake->setValues({{u"fixture.level"_s, 300u}, {u"fixture.ranged"_s, 10u}});
        const auto m = model();
        QCOMPARE(value(m->rows(u"scale"_s, 0), u"fixture.level"_s), u"3 %"_s);
        QCOMPARE(m->text(u"fixture.level"_s, 2442u), u"24 %"_s);
        QCOMPARE(m->text(u"fixture.level"_s, 10000u), u"100 %"_s);
        QCOMPARE(m->next(u"fixture.level"_s, {}, 1), QVariant(1000u));
        QCOMPARE(m->next(u"fixture.level"_s, {}, -1), QVariant(300u));
        QCOMPARE(m->next(u"fixture.level"_s, {}, 2), QVariant(1500u));
        QCOMPARE(m->next(u"fixture.level"_s, 2442u, 1), QVariant(2500u));
        QCOMPARE(m->next(u"fixture.level"_s, 2442u, -1), QVariant(2000u));
        QCOMPARE(m->next(u"fixture.level"_s, 2442u, 0), QVariant(2500u));
        QCOMPARE(m->next(u"fixture.level"_s, 1000u, -1), QVariant(1000u));
        QCOMPARE(m->next(u"fixture.level"_s, 1500u, -1), QVariant(1000u));
        QCOMPARE(m->next(u"fixture.level"_s, 10000u, 1), QVariant(10000u));
        QCOMPARE(m->next(u"fixture.level"_s, 20000u, 1), QVariant(20000u));
        QCOMPARE(m->next(u"fixture.ranged"_s, {}, -1), QVariant(10u));
        QCOMPARE(m->next(u"fixture.ranged"_s, {}, 1), QVariant(20u));
        QCOMPARE(m->next(u"fixture.ranged"_s, 30u, -1), QVariant(20u));
        QCOMPARE(m->next(u"fixture.ranged"_s, 20u, -1), QVariant(20u));
        QCOMPARE(m->next(u"fixture.ranged"_s, 90u, 1), QVariant(100u));
    }

    void nextValues()
    {
        fake->setValues({{u"audio.output"_s, u"bt"_s}, {u"power.tdp"_s, 10u}, {u"audio.speaker_gain"_s, 100u},
                         {u"power.gpu_level"_s, u"manual"_s}, {u"power.gpu_clock"_s, 587u}});
        fake->setStored({{u"rgb.enabled"_s, true}});
        const auto m = model();
        QCOMPARE(m->next(u"audio.output"_s, {}, 1), QVariant(u"bt"_s));
        QCOMPARE(m->next(u"audio.output"_s, {}, -1), QVariant(u"usb"_s));
        QCOMPARE(m->next(u"audio.output"_s, u"speaker"_s, 1), QVariant(u"headphones"_s));
        QCOMPARE(m->next(u"audio.speaker_gain"_s, {}, 0), QVariant(99u));
        QCOMPARE(m->next(u"audio.speaker_gain"_s, {}, 1), QVariant(102u));
        QCOMPARE(m->next(u"power.tdp"_s, {}, 1), QVariant(10u));
        QCOMPARE(m->next(u"power.tdp"_s, {}, -1), QVariant(9u));
        QCOMPARE(m->next(u"display.auto_rotate"_s, {}, 1), QVariant(false));
        QCOMPARE(m->next(u"display.auto_rotate"_s, false, 1), QVariant(true));
        QCOMPARE(value(m->rows(u"power"_s, 0), u"power.gpu_clock"_s), u"587 mhz"_s);
        QCOMPARE(m->next(u"power.gpu_clock"_s, {}, 1), QVariant(587u));
        QCOMPARE(m->next(u"power.gpu_clock"_s, {}, -1), QVariant(525u));
        QCOMPARE(m->next(u"power.gpu_clock"_s, 400u, 1), QVariant(442u));
        QCOMPARE(m->next(u"power.gpu_clock"_s, 450u, 0), QVariant(442u));
        QCOMPARE(m->next(u"power.gpu_clock"_s, 450u, 1), QVariant(490u));
        QCOMPARE(m->next(u"power.gpu_clock"_s, 450u, -1), QVariant(442u));
        QCOMPARE(m->next(u"power.gpu_clock"_s, 300u, -1), QVariant(300u));
        QCOMPARE(m->next(u"power.gpu_clock"_s, 300u, 1), QVariant(305u));
        QCOMPARE(m->next(u"power.gpu_clock"_s, 305u, -1), QVariant(305u));
        QCOMPARE(m->next(u"rgb.brightness"_s, {}, 1), QVariant(100u));
        QCOMPARE(m->next(u"rgb.brightness"_s, {}, -1), QVariant(95u));
        QCOMPARE(m->next(u"rgb.brightness"_s, 0u, -1), QVariant(0u));
        QCOMPARE(m->next(u"rgb.saturation"_s, {}, 1), QVariant(5u));
        QCOMPARE(m->next(u"rgb.saturation"_s, 100u, 1), QVariant(100u));
        QCOMPARE(m->next(u"session.default"_s, {}, -1), QVariant(u"microemu"_s));
        QVERIFY(!m->next(u"audio.eq.headphones.name"_s, {}, 1).isValid());
        QVERIFY(!m->next(u"nope.key"_s, {}, 1).isValid());
    }

    void editEchoAndRefusal()
    {
        const auto m = model();
        QSignalSpy refused(m.get(), &SettingsModel::refused);
        fake->setHold(true);
        QVERIFY(m->set(u"audio.output"_s, u"speaker"_s));
        QVERIFY(m->set(u"audio.output"_s, u"headphones"_s));
        QTRY_COMPARE(fake->sets().size(), 2);
        fake->emitChanged(u"audio.output"_s, u"usb"_s);
        fake->release(1);
        QTest::qWait(100);
        QCOMPARE(value(m->rows(u"audio"_s, 0), u"audio.output"_s), u"headphones"_s);
        fake->release();
        QTest::qWait(100);
        QCOMPARE(value(m->rows(u"audio"_s, 0), u"audio.output"_s), u"headphones"_s);
        fake->setHold(false);
        fake->emitChanged(u"audio.output"_s, u"usb"_s);
        QTRY_COMPARE(value(m->rows(u"audio"_s, 0), u"audio.output"_s), u"usb"_s);
        fake->refuse(u"audio.output"_s, u"net.seilent.MicroSet1.Error.InvalidValue"_s);
        QVERIFY(m->set(u"audio.output"_s, u"speaker"_s));
        QCOMPARE(value(m->rows(u"audio"_s, 0), u"audio.output"_s), u"speaker"_s);
        QTRY_COMPARE(refused.count(), 1);
        QCOMPARE(refused.at(0).at(0).toString(), u"Output"_s);
        QCOMPARE(refused.at(0).at(1).toString(), u"set failed"_s);
        QCOMPARE(value(m->rows(u"audio"_s, 0), u"audio.output"_s), u"usb"_s);
    }

    void serviceGoneAndBack()
    {
        fake->setValues({{u"audio.output"_s, u"speaker"_s}});
        const auto m = model();
        QSignalSpy refused(m.get(), &SettingsModel::refused);
        fake->stop();
        QTRY_VERIFY(!m->available());
        QCOMPARE(value(m->rows(u"audio"_s, 0), u"audio.output"_s), u"speaker"_s);
        QVERIFY(!m->beginEdit(u"audio.output"_s));
        QVERIFY(!m->set(u"audio.output"_s, u"usb"_s));
        QCOMPARE(refused.count(), 2);
        QCOMPARE(refused.at(0).at(1).toString(), u"not available"_s);
        QVERIFY(fake->sets().isEmpty());
        fake->setValues({{u"audio.output"_s, u"bt"_s}});
        QVERIFY(fake->start());
        QTRY_VERIFY(m->available());
        QTRY_COMPARE(value(m->rows(u"audio"_s, 0), u"audio.output"_s), u"bt"_s);
    }

    void noServiceAtStart()
    {
        fake->stop();
        SettingsModel m(h->client());
        m.load();
        QTest::qWait(100);
        QVERIFY(!m.available());
        QVERIFY(m.rows(u"audio"_s, 0).isEmpty());
        QVERIFY(fake->start());
        QTRY_VERIFY(m.available());
        QVERIFY(!m.rows(u"audio"_s, 0).isEmpty());
    }

    void picks()
    {
        const QJsonObject schema = FakeMicroSetd::describe(allFiles());
        QVERIFY2(schema.contains("microdap.scale"_L1), qPrintable(MicroDapCommit));
        QVERIFY2(schema.contains("microemu.esde.theme_color_scheme"_L1), qPrintable(MicroEmuCommit));
        fake->setSchema(schema);
        fake->setValues({{u"power.gpu_level"_s, u"manual"_s}, {u"display.auto_rotate"_s, false}});
        fake->setStored({{u"rgb.enabled"_s, true}});
        const auto m = model();
        const QStringList edits{u"rgb.hue"_s, u"microdap.accent_hue"_s, u"audio.balance.headphones"_s,
                                u"audio.balance.speaker"_s, u"audio.balance.usb"_s, u"audio.balance.bt"_s,
                                u"microemu.retroarch.audio_block_frames"_s, u"microemu.retroarch.savestate_max_keep"_s};
        QStringList seenEdits;
        int sheets = 0;
        for (const QVariant &group : m->groups(0)) {
            const QString title = group.toMap().value(u"group"_s).toString();
            if (title.isEmpty())
                continue;
            for (const QVariant &item : m->rows(title, 0)) {
                const QVariantMap row = item.toMap();
                const QString key = row.value(u"key"_s).toString();
                const QString pick = row.value(u"pick"_s).toString();
                const QJsonObject entry = schema.value(key).toObject();
                if (row.value(u"readOnly"_s).toBool()) {
                    QVERIFY2(pick.isEmpty(), qPrintable(key));
                } else if (entry.value("type"_L1).toString() == "b"_L1) {
                    QCOMPARE(pick, u"flip"_s);
                } else if (edits.contains(key)) {
                    QVERIFY2(pick == "edit"_L1, qPrintable(key));
                    seenEdits << key;
                } else {
                    QVERIFY2(pick == "sheet"_L1, qPrintable(key + u' ' + pick));
                    QVERIFY2(!words(m->sheet(key)).isEmpty(), qPrintable(key));
                    ++sheets;
                }
            }
        }
        std::sort(seenEdits.begin(), seenEdits.end());
        QStringList wanted = edits;
        std::sort(wanted.begin(), wanted.end());
        QCOMPARE(seenEdits, wanted);
        QVERIFY(sheets > 30);
        QCOMPARE(words(m->sheet(u"microemu.esde.theme_color_scheme"_s)).size(), 37);
        QCOMPARE(words(m->sheet(u"microemu.retroarch.menu_shader_pipeline"_s)).size(), 7);
        QVERIFY(m->sheet(u"microemu.retroarch.audio_block_frames"_s).isEmpty());
        QVERIFY(m->sheet(u"audio.eq.headphones.on"_s).isEmpty());
    }

    void sheetOrderAndMarks()
    {
        fake->setSchema(FakeMicroSetd::describe(allFiles()));
        fake->setValues({{u"power.gpu_level"_s, u"manual"_s}, {u"power.gpu_clock"_s, 490u}, {u"power.tdp"_s, 7u},
                         {u"power.game_floor"_s, 0u}, {u"microdap.scale"_s, 1.0}, {u"audio.volume_step"_s, 5u},
                         {u"audio.hp_gain"_s, u"normal"_s}});
        fake->setStored({{u"display.brightness"_s, 2442u}});
        const auto m = model();
        const QVariantMap clock = m->sheet(u"power.gpu_clock"_s);
        QCOMPARE(words(clock), (QStringList{u"587 mhz"_s, u"525 mhz"_s, u"490 mhz"_s, u"441.6 mhz"_s, u"400 mhz"_s,
                                            u"305 mhz"_s}));
        QCOMPARE(clock.value(u"mark"_s).toInt(), 2);
        QCOMPARE(clock.value(u"focus"_s).toInt(), 2);
        QVERIFY(clock.value(u"wrap"_s).toBool());
        const QVariantMap tdp = m->sheet(u"power.tdp"_s);
        QCOMPARE(words(tdp), (QStringList{u"10 w"_s, u"9 w"_s, u"8 w"_s, u"7 w"_s, u"6 w"_s, u"5 w"_s, u"4 w"_s, u"3 w"_s}));
        QCOMPARE(tdp.value(u"mark"_s).toInt(), 3);
        const QVariantMap floor = m->sheet(u"power.game_floor"_s);
        QCOMPARE(words(floor), (QStringList{u"83 %"_s, u"68 %"_s, u"49 %"_s, u"28 %"_s, u"off"_s}));
        QCOMPARE(floor.value(u"mark"_s).toInt(), 4);
        QCOMPARE(words(m->sheet(u"power.profile"_s)), (QStringList{u"low power"_s, u"balanced"_s, u"performance"_s}));
        QCOMPARE(words(m->sheet(u"audio.output"_s)),
                 (QStringList{u"auto"_s, u"speaker"_s, u"headphones"_s, u"usb"_s, u"bt"_s}));
        QCOMPARE(words(m->sheet(u"session.default"_s)), (QStringList{u"microdap"_s, u"microemu"_s, u"steam"_s}));
        QCOMPARE(words(m->sheet(u"power.gpu_undervolt"_s)), (QStringList{u"Off"_s, u"Low"_s}));
        QCOMPARE(words(m->sheet(u"audio.volume_step"_s)), (QStringList{u"10%"_s, u"5%"_s, u"2%"_s}));
        const QVariantMap gain = m->sheet(u"audio.hp_gain"_s);
        QCOMPARE(gain.value(u"mark"_s).toInt(), 1);
        QVERIFY(!gain.value(u"wrap"_s).toBool());
        const QVariantMap brightness = m->sheet(u"display.brightness"_s);
        QCOMPARE(words(brightness).size(), 19);
        QCOMPARE(words(brightness).first(), u"100 %"_s);
        QCOMPARE(words(brightness).last(), u"10 %"_s);
        QCOMPARE(brightness.value(u"mark"_s).toInt(), -1);
        QCOMPARE(brightness.value(u"values"_s).toList().at(brightness.value(u"focus"_s).toInt()), QVariant(2500u));
        QVERIFY(!brightness.value(u"wrap"_s).toBool());
        const QVariantMap scale = m->sheet(u"microdap.scale"_s);
        QCOMPARE(words(scale), (QStringList{u"1.4"_s, u"1.3"_s, u"1.2"_s, u"1.1"_s, u"1.0"_s, u"0.9"_s, u"0.8"_s}));
        QCOMPARE(scale.value(u"mark"_s).toInt(), 4);
        QVERIFY(!scale.value(u"wrap"_s).toBool());
        const QVariantMap saturation = m->sheet(u"rgb.saturation"_s);
        QVERIFY(saturation.isEmpty());
    }

    void decimalsFromStep()
    {
        fake->setSchema(FakeMicroSetd::describe(allFiles()));
        fake->setValues({{u"microdap.scale"_s, 1.0}});
        const auto m = model();
        QCOMPARE(value(m->rows(u"display"_s, 0), u"microdap.scale"_s), u"1.0"_s);
        QCOMPARE(m->text(u"microdap.scale"_s, 1.4), u"1.4"_s);
        QCOMPARE(m->text(u"microdap.scale"_s, 0.8), u"0.8"_s);
        QCOMPARE(m->text(u"microdap.scale"_s, 0.8 + 2 * 0.1), u"1.0"_s);
        QCOMPARE(m->text(u"power.tdp"_s, 7u), u"7 w"_s);
    }

    void overridePreview()
    {
        fake->setValues({{u"rgb.enabled"_s, true}, {u"rgb.saturation"_s, 60u}});
        fake->setStored({{u"rgb.enabled"_s, true}, {u"rgb.saturation"_s, 60u}});
        const auto m = model();
        QCOMPARE(m->sheet(u"rgb.saturation"_s).value(u"mark"_s).toInt(), 8);
        QVERIFY(m->beginEdit(u"rgb.saturation"_s));
        QVERIFY(m->preview(55u));
        QVERIFY(m->preview(50u));
        QTRY_COMPARE(fake->calls(), (QStringList{u"Override rgb.saturation 55"_s, u"Override rgb.saturation 50"_s}));
        QCOMPARE(value(m->rows(u"stick leds"_s, m->revision()), u"rgb.saturation"_s), u"50 %"_s);
        QSignalSpy ended(m.get(), &SettingsModel::previewEnded);
        m->endPreview(50u);
        QCOMPARE(ended.count(), 1);
        QTRY_COMPARE(fake->calls().size(), 4);
        QCOMPARE(fake->calls().mid(2), (QStringList{u"Set rgb.saturation 50"_s, u"ClearOverride rgb.saturation"_s}));
        QTRY_COMPARE(value(m->rows(u"stick leds"_s, m->revision()), u"rgb.saturation"_s), u"50 %"_s);
        QVERIFY(m->beginEdit(u"rgb.saturation"_s));
        QVERIFY(m->preview(45u));
        m->cancelPreview();
        QTRY_COMPARE(fake->calls().size(), 6);
        QCOMPARE(fake->calls().mid(4), (QStringList{u"Override rgb.saturation 45"_s, u"ClearOverride rgb.saturation"_s}));
        m->cancelPreview();
        m->endPreviews();
        QTest::qWait(100);
        QCOMPARE(fake->calls().size(), 6);
        QCOMPARE(fake->stored().value(u"rgb.saturation"_s), QVariant(50u));
    }

    void livePreview()
    {
        const auto m = model();
        QVERIFY(m->beginEdit(u"audio.volume_step"_s));
        QVERIFY(m->preview(10u));
        QTRY_COMPARE(fake->calls(), QStringList{u"Set audio.volume_step 10"_s});
        m->endPreview(10u);
        QTest::qWait(100);
        QCOMPARE(fake->calls().size(), 1);
        QVERIFY(m->beginEdit(u"audio.volume_step"_s));
        QVERIFY(m->preview(2u));
        m->cancelPreview();
        QTRY_COMPARE(fake->calls().size(), 3);
        QCOMPARE(fake->calls().mid(1), (QStringList{u"Set audio.volume_step 2"_s, u"Set audio.volume_step 10"_s}));
        QVERIFY(m->beginEdit(u"audio.volume_step"_s));
        QVERIFY(m->preview(5u));
        m->endPreview(2u);
        QTRY_COMPARE(fake->calls().size(), 5);
        QCOMPARE(fake->calls().mid(3), (QStringList{u"Set audio.volume_step 5"_s, u"Set audio.volume_step 2"_s}));
        QVERIFY(m->beginEdit(u"audio.max_volume"_s));
        QVERIFY(m->preview(90u));
        QVERIFY(m->preview(80u));
        QTest::qWait(100);
        QCOMPARE(fake->calls().size(), 5);
        m->endPreview(80u);
        QTRY_COMPARE(fake->calls().size(), 6);
        QCOMPARE(fake->calls().last(), u"Set audio.max_volume 80"_s);
        QVERIFY(m->beginEdit(u"audio.max_volume"_s));
        QVERIFY(m->preview(70u));
        m->cancelPreview();
        QVERIFY(m->beginEdit(u"audio.max_volume"_s));
        m->endPreviews();
        QVERIFY(m->beginEdit(u"power.tdp"_s));
        QVERIFY(m->preview(9u));
        m->endPreviews();
        QTRY_COMPARE(fake->calls().size(), 7);
        QTest::qWait(100);
        QCOMPARE(fake->calls().last(), u"Set power.tdp 9"_s);
        QCOMPARE(fake->calls().size(), 7);
    }

    void outsideChangeEndsPreview()
    {
        fake->setValues({{u"rgb.enabled"_s, true}});
        fake->setStored({{u"rgb.enabled"_s, true}, {u"rgb.saturation"_s, 60u}});
        const auto m = model();
        QSignalSpy ended(m.get(), &SettingsModel::previewEnded);
        QVERIFY(m->beginEdit(u"audio.volume_step"_s));
        QVERIFY(m->preview(10u));
        QTRY_COMPARE(fake->calls().size(), 1);
        fake->emitChanged(u"audio.volume_step"_s, 5u);
        QTest::qWait(100);
        QCOMPARE(ended.count(), 0);
        fake->emitChanged(u"audio.volume_step"_s, 2u);
        QTRY_COMPARE(ended.count(), 1);
        QCOMPARE(ended.at(0).at(0).toString(), u"audio.volume_step"_s);
        m->cancelPreview();
        QTest::qWait(100);
        QCOMPARE(fake->calls().size(), 1);
        QVERIFY(m->beginEdit(u"rgb.saturation"_s));
        QVERIFY(m->preview(40u));
        QTRY_COMPARE(fake->calls().size(), 2);
        fake->setStored({{u"rgb.enabled"_s, true}, {u"rgb.saturation"_s, 20u}});
        fake->emitChanged(u"rgb.saturation"_s, 20u);
        QTRY_COMPARE(ended.count(), 2);
        QTRY_COMPARE(fake->calls().size(), 3);
        QCOMPARE(fake->calls().last(), u"ClearOverride rgb.saturation"_s);
    }

    void unavailableEndsWithoutWrite()
    {
        fake->setValues({{u"rgb.enabled"_s, true}});
        fake->setStored({{u"rgb.enabled"_s, true}, {u"rgb.saturation"_s, 60u}});
        const auto m = model();
        QSignalSpy ended(m.get(), &SettingsModel::previewEnded);
        QVERIFY(m->beginEdit(u"rgb.saturation"_s));
        QVERIFY(m->preview(40u));
        QTRY_COMPARE(fake->calls().size(), 1);
        fake->stop();
        QTRY_VERIFY(!m->available());
        QCOMPARE(ended.count(), 1);
        m->endPreviews();
        m->cancelPreview();
        m->endPreview(40u);
        QVERIFY(fake->start());
        QTRY_VERIFY(m->available());
        QTest::qWait(100);
        QCOMPARE(fake->calls().size(), 1);
    }

    void destructionSettlesOverride()
    {
        fake->setValues({{u"rgb.enabled"_s, true}});
        fake->setStored({{u"rgb.enabled"_s, true}, {u"rgb.saturation"_s, 60u}});
        {
            auto m = model();
            QVERIFY(m->beginEdit(u"rgb.saturation"_s));
            QVERIFY(m->preview(55u));
            QVERIFY(m->preview(45u));
            QTRY_COMPARE(fake->calls().size(), 2);
            m->endPreviews();
            m->endPreviews();
            QTRY_COMPARE(fake->calls().size(), 4);
            QCOMPARE(fake->calls().mid(2), (QStringList{u"Set rgb.saturation 45"_s, u"ClearOverride rgb.saturation"_s}));
            QVERIFY(m->beginEdit(u"rgb.saturation"_s));
            QVERIFY(m->preview(30u));
            QTRY_COMPARE(fake->calls().size(), 5);
        }
        QTRY_COMPARE(fake->calls().size(), 7);
        QCOMPARE(fake->calls().mid(5), (QStringList{u"Set rgb.saturation 30"_s, u"ClearOverride rgb.saturation"_s}));
        QTest::qWait(100);
        QCOMPARE(fake->calls().size(), 7);
    }

    void busyWhileSetWaits()
    {
        const auto m = model();
        fake->setHold(true);
        QVERIFY(!m->busy(u"audio.volume_step"_s));
        QVERIFY(m->set(u"audio.volume_step"_s, 10u));
        QVERIFY(m->busy(u"audio.volume_step"_s));
        QTRY_COMPARE(fake->sets().size(), 1);
        fake->setValues({{u"audio.volume_step"_s, 10u}});
        fake->release();
        QTRY_VERIFY(!m->busy(u"audio.volume_step"_s));
        QVERIFY(!m->busy(u"nope.key"_s));
    }
};

QTEST_GUILESS_MAIN(ModelTest)
#include "tst_model.moc"
