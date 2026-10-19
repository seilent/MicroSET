#include "fakemicrosetd.h"
#include "harness.h"
#include "settingsmodel.h"

#include <QFile>
#include <QSignalSpy>
#include <QTest>

#include <memory>

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
                              u"audio.power_on_volume"_s, u"audio.bt_codec"_s, u"audio.eq.headphones.on"_s,
                              u"audio.eq.headphones.name"_s, u"audio.balance.headphones"_s,
                              u"audio.crossfeed.headphones"_s, u"audio.eq.speaker.on"_s, u"audio.balance.speaker"_s,
                              u"audio.eq.usb.on"_s, u"audio.balance.usb"_s, u"audio.crossfeed.usb"_s,
                              u"audio.eq.bt.on"_s, u"audio.balance.bt"_s}));
        QCOMPARE(keysOf(m->rows(u"power"_s, 0)),
                 (QStringList{u"label-microset"_s, u"power.profile"_s, u"power.tdp"_s, u"power.governor"_s,
                              u"power.gpu_level"_s, u"power.gpu_clock"_s, u"power.game_floor"_s}));
        QCOMPARE(keysOf(m->rows(u"display"_s, 0)),
                 (QStringList{u"label-microset"_s, u"display.orientation"_s, u"display.auto_rotate"_s}));
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
    }

    void groupsMerged()
    {
        fake->setSchema(FakeMicroSetd::describe({FakeMicroSetd::shipped(), FakeMicroSetd::fixture(u"50-fixture.json"_s),
                                                 FakeMicroSetd::fixture(u"60-microdap.json"_s)}));
        fake->setValues({{u"microdap.accent_hue"_s, 120u}});
        const auto m = model();
        QCOMPARE(keysOf(m->groups(0)),
                 (QStringList{u"label-microset"_s, u"group-audio"_s, u"group-display"_s, u"group-stick leds"_s,
                              u"group-power"_s, u"group-system"_s, u"group-updates"_s, u"label-fixture"_s,
                              u"group-group with a long title"_s}));
        const QVariantMap leds = rowOf(m->groups(0), u"group-stick leds"_s);
        QCOMPARE(leds.value(u"title"_s).toString(), u"stick leds"_s);
        QCOMPARE(leds.value(u"group"_s).toString(), u"stick leds"_s);
        QCOMPARE(keysOf(m->rows(u"display"_s, 0)),
                 (QStringList{u"label-microset"_s, u"display.orientation"_s, u"display.auto_rotate"_s,
                              u"label-microdap"_s, u"microdap.accent_hue"_s}));
        QCOMPARE(m->accentHue(), 120);
        const QVariantMap accent = rowOf(m->rows(u"display"_s, 0), u"microdap.accent_hue"_s);
        QCOMPARE(accent.value(u"hue"_s).toInt(), 120);
        QCOMPARE(accent.value(u"swatch"_s).toString(), QString());
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
};

QTEST_GUILESS_MAIN(ModelTest)
#include "tst_model.moc"
