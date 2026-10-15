#include "alsaapplier.h"
#include "fakealsa.h"
#include "harness.h"
#include "schema.h"
#include "settings.h"
#include "store.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDir>
#include <QFile>
#include <QTest>

#include <algorithm>
#include <memory>
#include <tuple>

using namespace Qt::StringLiterals;

namespace {

QStringList *captured = nullptr;
QtMessageHandler previous = nullptr;

void capture(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    if (captured)
        *captured << message;
    previous(type, context, message);
}

const QStringList Defaults = {
    u"HPHL Volume=24"_s,         u"HPHR Volume=24"_s,         u"RX HPH Mode=CLS_H_LOHIFI"_s,
    u"RX_HPH PWR Mode=LOHIFI"_s, u"WSA_RX0 Digital Volume=102"_s, u"WSA_RX1 Digital Volume=102"_s,
    u"SpkrLeft PA Volume=12"_s,  u"SpkrRight PA Volume=12"_s,
};

}

class AlsaTest : public QObject
{
    Q_OBJECT

private:
    struct Fixture
    {
        Harness h;
        Schema schema;
        Store store{h.path(u"state"_s)};
        Settings settings{schema, store};
        FakeAlsa alsa;
        std::unique_ptr<AlsaApplier> applier;

        Fixture()
        {
            schema.addApplier(u"power"_s);
            schema.addApplier(u"alsa"_s);
            schema.addProvider(u"sessions"_s, [] { return QStringList{u"steam"_s}; });
            schema.load(QStringLiteral(MICROSET_SOURCE_DIR) + u"/schema"_s);
            QDir().mkpath(h.path(u"dev/snd"_s));
            if (!h.startBus())
                qFatal("no test bus");
        }

        void start(int settleMs = 400)
        {
            store.load();
            applier = std::make_unique<AlsaApplier>(h.client(), settings, alsa, h.path(u"dev/snd"_s), settleMs);
            applier->start();
        }

        void touch(const QString &name) const
        {
            h.writeFile(u"dev/snd/"_s + name, {});
        }
    };

    QStringList m_log;

    int logged(const QString &text) const
    {
        return int(std::count_if(m_log.cbegin(), m_log.cend(), [&](const QString &line) { return line.contains(text); }));
    }

    static void sleep(const Harness &h, bool start)
    {
        QDBusConnection logind = QDBusConnection::connectToBus(h.address(), u"fake-logind"_s);
        QDBusMessage signal = QDBusMessage::createSignal(u"/org/freedesktop/login1"_s, u"org.freedesktop.login1.Manager"_s,
                                                         u"PrepareForSleep"_s);
        signal << start;
        QVERIFY(logind.send(signal));
    }

    static void ownLogind(const Harness &h)
    {
        QDBusConnection logind = QDBusConnection::connectToBus(h.address(), u"fake-logind"_s);
        QVERIFY(logind.registerService(u"org.freedesktop.login1"_s));
    }

private slots:
    void init()
    {
        m_log.clear();
        captured = &m_log;
        previous = qInstallMessageHandler(capture);
    }

    void cleanup()
    {
        qInstallMessageHandler(previous);
        captured = nullptr;
        QDBusConnection::disconnectFromBus(u"fake-logind"_s);
    }

    void wantedPerKey()
    {
        Fixture f;
        f.start();
        const QList<std::tuple<QString, QVariant, QStringList>> cases = {
            {u"audio.hp_gain"_s, u"low"_s, {u"HPHL Volume=8"_s, u"HPHR Volume=8"_s}},
            {u"audio.hp_gain"_s, u"normal"_s, {u"HPHL Volume=20"_s, u"HPHR Volume=20"_s}},
            {u"audio.hp_gain"_s, u"high"_s, {u"HPHL Volume=24"_s, u"HPHR Volume=24"_s}},
            {u"audio.hp_amp"_s, u"ulp"_s, {u"RX HPH Mode=CLS_H_ULP"_s, u"RX_HPH PWR Mode=ULP"_s}},
            {u"audio.hp_amp"_s, u"hifi"_s, {u"RX HPH Mode=CLS_H_HIFI"_s, u"RX_HPH PWR Mode=LOHIFI"_s}},
            {u"audio.hp_amp"_s, u"lohifi"_s, {u"RX HPH Mode=CLS_H_LOHIFI"_s, u"RX_HPH PWR Mode=LOHIFI"_s}},
            {u"audio.speaker_gain"_s, 84u, {u"WSA_RX0 Digital Volume=84"_s, u"WSA_RX1 Digital Volume=84"_s}},
            {u"audio.speaker_gain"_s, 108u, {u"WSA_RX0 Digital Volume=108"_s, u"WSA_RX1 Digital Volume=108"_s}},
            {u"audio.speaker_gain"_s, 102u, {u"WSA_RX0 Digital Volume=102"_s, u"WSA_RX1 Digital Volume=102"_s}},
        };
        for (const auto &[key, value, controls] : cases) {
            f.alsa.writes.clear();
            QCOMPARE(f.settings.store(key, value), Settings::Changed);
            QCOMPARE(f.alsa.writes, controls);
            for (const QString &entry : controls) {
                const qsizetype eq = entry.indexOf(u'=');
                QCOMPARE(f.alsa.value(entry.left(eq)), entry.mid(eq + 1));
            }
        }
        QVERIFY(f.applier->wanted(u"audio.output"_s).isEmpty());
        QVERIFY(f.applier->wanted(u"audio.volume_step"_s).isEmpty());
    }

    void startWritesStoredValues()
    {
        Fixture f;
        f.h.writeState(R"({"audio.hp_gain": "normal", "audio.speaker_gain": 90})");
        f.start();
        QCOMPARE(f.alsa.writes, (QStringList{u"HPHL Volume=20"_s, u"HPHR Volume=20"_s, u"RX HPH Mode=CLS_H_LOHIFI"_s,
                                             u"RX_HPH PWR Mode=LOHIFI"_s, u"WSA_RX0 Digital Volume=90"_s,
                                             u"WSA_RX1 Digital Volume=90"_s, u"SpkrLeft PA Volume=12"_s,
                                             u"SpkrRight PA Volume=12"_s}));
        QCOMPARE(logged(u"alsa start: wrote HPHL Volume=20"_s), 1);
        QTest::qWait(600);
        QCOMPARE(f.alsa.writes.size(), 8);
        QCOMPARE(logged(u"read "_s), 0);
    }

    void foreignChangeWrittenBack()
    {
        Fixture f;
        f.start();
        f.alsa.writes.clear();
        f.alsa.foreign(u"HPHL Volume"_s, u"12"_s);
        QTRY_COMPARE(f.alsa.value(u"HPHL Volume"_s), u"24"_s);
        QCOMPARE(f.alsa.writes, QStringList{u"HPHL Volume=24"_s});
        QCOMPARE(logged(u"alsa value event: HPHL Volume read 12, wrote 24"_s), 1);
        f.alsa.foreign(u"HPHL Volume"_s, u"0"_s);
        QTRY_COMPARE(f.alsa.value(u"HPHL Volume"_s), u"24"_s);
        f.alsa.foreign(u"WSA_RX1 Digital Volume"_s, u"84"_s);
        QTRY_COMPARE(f.alsa.value(u"WSA_RX1 Digital Volume"_s), u"102"_s);
        QCOMPARE(logged(u"HPHL Volume read"_s), 1);
        QCOMPARE(logged(u"alsa value event: WSA_RX1 Digital Volume read 84, wrote 102"_s), 1);
    }

    void speakerPaHeldAtMax()
    {
        Fixture f;
        ownLogind(f.h);
        f.start();
        QCOMPARE(f.alsa.value(u"SpkrLeft PA Volume"_s), u"12"_s);
        QCOMPARE(f.alsa.value(u"SpkrRight PA Volume"_s), u"12"_s);
        QCOMPARE(f.applier->wanted(u"audio.speaker_gain"_s).size(), 2);
        f.alsa.writes.clear();
        f.alsa.foreign(u"SpkrLeft PA Volume"_s, u"8"_s);
        QTRY_COMPARE(f.alsa.value(u"SpkrLeft PA Volume"_s), u"12"_s);
        QCOMPARE(f.alsa.writes, QStringList{u"SpkrLeft PA Volume=12"_s});
        QCOMPARE(logged(u"alsa value event: SpkrLeft PA Volume read 8, wrote 12"_s), 1);
        f.alsa.controls[u"SpkrRight PA Volume"_s].events = false;
        f.alsa.foreign(u"SpkrRight PA Volume"_s, u"8"_s);
        sleep(f.h, false);
        QTRY_COMPARE(f.alsa.value(u"SpkrRight PA Volume"_s), u"12"_s);
        QCOMPARE(logged(u"alsa resume: SpkrRight PA Volume read 8, wrote 12"_s), 1);
        f.alsa.writes.clear();
        QCOMPARE(f.settings.store(u"audio.speaker_gain"_s, 84u), Settings::Changed);
        QCOMPARE(f.alsa.writes, (QStringList{u"WSA_RX0 Digital Volume=84"_s, u"WSA_RX1 Digital Volume=84"_s}));
    }

    void ownEchoIgnored()
    {
        Fixture f;
        f.start();
        QTest::qWait(100);
        f.alsa.writes.clear();
        QCOMPARE(f.settings.store(u"audio.hp_gain"_s, u"low"_s), Settings::Changed);
        QCOMPARE(f.settings.store(u"audio.speaker_gain"_s, 96u), Settings::Changed);
        QTest::qWait(200);
        QCOMPARE(f.alsa.writes, (QStringList{u"HPHL Volume=8"_s, u"HPHR Volume=8"_s, u"WSA_RX0 Digital Volume=96"_s,
                                             u"WSA_RX1 Digital Volume=96"_s}));
        QCOMPARE(logged(u" read "_s), 0);
    }

    void missingControlLoggedOnce()
    {
        Fixture f;
        ownLogind(f.h);
        f.alsa.controls.remove(u"WSA_RX1 Digital Volume"_s);
        f.start();
        QCOMPARE(logged(u"card PocketMICRO2 has no control WSA_RX1 Digital Volume"_s), 1);
        QCOMPARE(f.settings.store(u"audio.speaker_gain"_s, 84u), Settings::Changed);
        QCOMPARE(f.alsa.value(u"WSA_RX0 Digital Volume"_s), u"84"_s);
        sleep(f.h, false);
        QTest::qWait(300);
        QCOMPARE(logged(u"has no control"_s), 1);
    }

    void writeFailureLoggedUntilWritten()
    {
        Fixture f;
        f.start();
        QTest::qWait(100);
        f.alsa.broken = true;
        QCOMPARE(f.settings.store(u"audio.hp_gain"_s, u"low"_s), Settings::Changed);
        QCOMPARE(f.settings.store(u"audio.hp_gain"_s, u"normal"_s), Settings::Changed);
        QCOMPARE(logged(u"alsa: cannot write HPHL Volume=8"_s), 1);
        QCOMPARE(logged(u"alsa: cannot write HPHR Volume=8"_s), 1);
        QCOMPARE(logged(u"cannot write"_s), 2);
        f.alsa.foreign(u"HPHL Volume"_s, u"12"_s);
        QTest::qWait(200);
        QCOMPARE(logged(u"cannot write"_s), 2);
        f.alsa.broken = false;
        f.alsa.foreign(u"HPHL Volume"_s, u"0"_s);
        QTRY_COMPARE(f.alsa.value(u"HPHL Volume"_s), u"20"_s);
        QCOMPARE(logged(u"alsa value event: HPHL Volume read 0, wrote 20"_s), 1);
        f.alsa.broken = true;
        QCOMPARE(f.settings.store(u"audio.hp_gain"_s, u"high"_s), Settings::Changed);
        QCOMPARE(logged(u"alsa: cannot write HPHL Volume=24"_s), 1);
        QCOMPARE(logged(u"alsa: cannot write HPHR Volume"_s), 1);
    }

    void silentControlCheckedOnResume()
    {
        Fixture f;
        ownLogind(f.h);
        f.start();
        f.alsa.writes.clear();
        f.alsa.foreign(u"RX_HPH PWR Mode"_s, u"ULP"_s);
        QTest::qWait(200);
        QVERIFY(f.alsa.writes.isEmpty());
        sleep(f.h, true);
        QTest::qWait(200);
        QVERIFY(f.alsa.writes.isEmpty());
        sleep(f.h, false);
        QTRY_COMPARE(f.alsa.value(u"RX_HPH PWR Mode"_s), u"LOHIFI"_s);
        QCOMPARE(f.alsa.writes, QStringList{u"RX_HPH PWR Mode=LOHIFI"_s});
        QCOMPARE(logged(u"alsa resume: RX_HPH PWR Mode read ULP, wrote LOHIFI"_s), 1);

        QDBusConnection intruder = QDBusConnection::connectToBus(f.h.address(), u"intruder"_s);
        QDBusMessage signal = QDBusMessage::createSignal(u"/org/freedesktop/login1"_s, u"org.freedesktop.login1.Manager"_s,
                                                         u"PrepareForSleep"_s);
        signal << false;
        f.alsa.foreign(u"RX_HPH PWR Mode"_s, u"ULP"_s);
        QVERIFY(intruder.send(signal));
        QTest::qWait(300);
        QCOMPARE(f.alsa.value(u"RX_HPH PWR Mode"_s), u"ULP"_s);
        QDBusConnection::disconnectFromBus(u"intruder"_s);
    }

    void cardReopen()
    {
        Fixture f;
        f.start(800);
        QCOMPARE(f.alsa.opens, 1);
        f.alsa.unplug();
        QTRY_VERIFY(!f.alsa.isOpen());
        QCOMPARE(logged(u"alsa: card PocketMICRO2 gone"_s), 1);
        f.alsa.writes.clear();
        QCOMPARE(f.settings.store(u"audio.hp_gain"_s, u"normal"_s), Settings::Changed);
        f.touch(u"pcmC0D0p"_s);
        QTest::qWait(200);
        QVERIFY(!f.alsa.isOpen());
        QVERIFY(f.alsa.writes.isEmpty());

        f.alsa.foreign(u"HPHL Volume"_s, u"24"_s);
        f.alsa.foreign(u"HPHR Volume"_s, u"24"_s);
        f.alsa.foreign(u"RX_HPH PWR Mode"_s, u"ULP"_s);
        f.alsa.present = true;
        f.touch(u"controlC0"_s);
        QTRY_VERIFY(f.alsa.isOpen());
        QCOMPARE(f.alsa.opens, 2);
        QCOMPARE(logged(u"alsa: card PocketMICRO2 opened"_s), 1);
        QCOMPARE(f.alsa.writes, (QStringList{u"HPHL Volume=20"_s, u"HPHR Volume=20"_s, u"RX_HPH PWR Mode=LOHIFI"_s}));
        QCOMPARE(logged(u"alsa card reopen: RX_HPH PWR Mode read ULP, wrote LOHIFI"_s), 1);

        f.alsa.writes.clear();
        QTest::qWait(400);
        QVERIFY(f.alsa.writes.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(f.alsa.writes.size(), 8, 3000);
        QCOMPARE(f.alsa.writes, (QStringList{u"HPHL Volume=20"_s, u"HPHR Volume=20"_s, u"RX HPH Mode=CLS_H_LOHIFI"_s,
                                             u"RX_HPH PWR Mode=LOHIFI"_s, u"WSA_RX0 Digital Volume=102"_s,
                                             u"WSA_RX1 Digital Volume=102"_s, u"SpkrLeft PA Volume=12"_s,
                                             u"SpkrRight PA Volume=12"_s}));
        QCOMPARE(logged(u"alsa card appeared: wrote"_s), 1);
        QTest::qWait(1000);
        QCOMPARE(f.alsa.writes.size(), 8);
    }

    void cardAbsentAtStart()
    {
        Fixture f;
        QDir(f.h.path(u"dev/snd"_s)).removeRecursively();
        f.alsa.present = false;
        f.start(200);
        QVERIFY(!f.alsa.isOpen());
        QCOMPARE(logged(u"alsa: no card PocketMICRO2"_s), 1);
        QVERIFY(f.alsa.writes.isEmpty());
        f.alsa.present = true;
        f.touch(u"controlC0"_s);
        QTRY_VERIFY(f.alsa.isOpen());
        QCOMPARE(f.alsa.writes, (QStringList{u"HPHL Volume=24"_s, u"HPHR Volume=24"_s, u"WSA_RX0 Digital Volume=102"_s,
                                             u"WSA_RX1 Digital Volume=102"_s, u"SpkrLeft PA Volume=12"_s,
                                             u"SpkrRight PA Volume=12"_s}));
        f.alsa.writes.clear();
        QTRY_COMPARE_WITH_TIMEOUT(f.alsa.writes, Defaults, 3000);
        f.alsa.unplug();
        QTRY_VERIFY(!f.alsa.isOpen());
        f.alsa.present = true;
        f.touch(u"pcmC0D1p"_s);
        QTRY_VERIFY(f.alsa.isOpen());
        QCOMPARE(f.alsa.opens, 2);
    }
};

QTEST_GUILESS_MAIN(AlsaTest)
#include "tst_alsa.moc"
