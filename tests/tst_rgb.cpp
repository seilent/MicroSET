#include "fakelogind.h"
#include "harness.h"
#include "rgbapplier.h"
#include "schema.h"
#include "settings.h"
#include "sleepinhibitor.h"
#include "store.h"

#include <microset/color.h>

#include <QElapsedTimer>
#include <QSocketNotifier>
#include <QTest>

#include <cerrno>
#include <memory>

#include <fcntl.h>
#include <pty.h>
#include <unistd.h>

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

QByteArray bytesOf(std::initializer_list<int> values)
{
    QByteArray buf;
    for (const int value : values)
        buf.append(char(value));
    return buf;
}

const QByteArray Off = bytesOf({0xf7, 0x01, 0, 0, 0, 0, 0, 0, 0, 0x01, 0xed});

}

class RgbTest : public QObject
{
    Q_OBJECT

private:
    struct Fixture
    {
        Harness h;
        Schema schema;
        Store store{h.path(u"state"_s)};
        Settings settings{schema, store};
        std::unique_ptr<FakeLogind> logind;
        std::unique_ptr<SleepInhibitor> sleep;
        std::unique_ptr<RgbApplier> applier;
        int master = -1;
        int slave = -1;
        QString device;
        QElapsedTimer clock;
        QByteArray input;
        QList<qint64> writes;
        std::unique_ptr<QSocketNotifier> notifier;

        Fixture()
        {
            for (const QString &name : {u"power"_s, u"alsa"_s, u"ssc"_s, u"rgb"_s, u"backlight"_s})
                schema.addApplier(name);
            schema.addProvider(u"sessions"_s, [] { return QStringList{u"steam"_s}; });
            schema.load(QStringLiteral(MICROSET_SOURCE_DIR) + u"/schema"_s);
            h.writeFile(u"backlight/actual_brightness"_s, "4095\n");
            if (!h.startBus())
                qFatal("no test bus");
            logind = std::make_unique<FakeLogind>(h.address());
            if (!logind->start())
                qFatal("no fake logind");
            sleep = std::make_unique<SleepInhibitor>(h.client(), u"stop the stick LEDs"_s);
            char name[256] = {};
            if (::openpty(&master, &slave, name, nullptr, nullptr) < 0)
                qFatal("no pty");
            ::fcntl(master, F_SETFL, ::fcntl(master, F_GETFL) | O_NONBLOCK);
            device = QString::fromLocal8Bit(name);
            clock.start();
            notifier = std::make_unique<QSocketNotifier>(master, QSocketNotifier::Read);
            QObject::connect(notifier.get(), &QSocketNotifier::activated, notifier.get(), [this] { drain(); });
        }

        void drain()
        {
            char buf[256];
            ssize_t got = 0;
            while ((got = ::read(master, buf, sizeof(buf))) > 0)
                input.append(buf, got);
        }

        ~Fixture()
        {
            applier.reset();
            sleep.reset();
            notifier.reset();
            ::close(slave);
            ::close(master);
        }

        void start(const QString &path = {}, const QString &backlight = {})
        {
            store.load();
            sleep->take();
            QTRY_VERIFY(sleep->held());
            applier = std::make_unique<RgbApplier>(settings, sleep.get(), path.isEmpty() ? device : path,
                                                   backlight.isEmpty() ? h.path(u"backlight"_s) : backlight);
            QObject::connect(applier.get(), &RgbApplier::frameWritten, applier.get(), [this] { writes << clock.elapsed(); });
            applier->start();
        }

        void screen(int value)
        {
            h.writeFile(u"backlight/actual_brightness"_s, QByteArray::number(value) + '\n');
            applier->refresh();
        }

        void power(int value)
        {
            h.writeFile(u"backlight/bl_power"_s, QByteArray::number(value) + '\n');
        }

        QByteArray received() const
        {
            return input;
        }

        void expect(const QByteArray &frame, int from = 0)
        {
            QTRY_COMPARE(received().size(), from + 3 * frame.size());
            QCOMPARE(received().mid(from), frame + frame + frame);
        }
    };

    QStringList m_log;

    int logged(const QString &text) const
    {
        return int(std::count_if(m_log.cbegin(), m_log.cend(), [&](const QString &line) { return line.contains(text); }));
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
    }

    void frameBytes()
    {
        QCOMPARE(RgbApplier::frame(0xff0000), bytesOf({0xf7, 0x01, 0xff, 0, 0, 0, 0, 0, 0, 0x00, 0xed}));
        QCOMPARE(RgbApplier::frame(0x123456), bytesOf({0xf7, 0x01, 0x12, 0x34, 0x56, 0, 0, 0, 0, 0x9d, 0xed}));
        QCOMPARE(RgbApplier::frame(0x008000), bytesOf({0xf7, 0x01, 0, 0x80, 0, 0, 0, 0, 0, 0x81, 0xed}));
        QCOMPARE(RgbApplier::frame(0), Off);
    }

    void rangeRefused()
    {
        Fixture f;
        const Key *hue = f.schema.key(u"rgb.hue"_s);
        const Key *saturation = f.schema.key(u"rgb.saturation"_s);
        const Key *brightness = f.schema.key(u"rgb.brightness"_s);
        QVERIFY(hue && saturation && brightness);
        QVERIFY(f.schema.accepts(*hue, 0u));
        QVERIFY(f.schema.accepts(*hue, 359u));
        QVERIFY(!f.schema.accepts(*hue, 360u));
        QVERIFY(!f.schema.accepts(*hue, -1));
        QVERIFY(f.schema.accepts(*saturation, 100u));
        QVERIFY(!f.schema.accepts(*saturation, 101u));
        QVERIFY(f.schema.accepts(*brightness, 100u));
        QVERIFY(!f.schema.accepts(*brightness, 101u));
        QVERIFY(!f.schema.accepts(*f.schema.key(u"rgb.enabled"_s), 1u));
    }

    void startWritesThreeTimes()
    {
        Fixture f;
        f.start();
        f.expect(Off);
        const QList<qint64> at = f.writes;
        QCOMPARE(at.size(), 3);
        QVERIFY2(at.at(1) - at.at(0) >= 35, qPrintable(QString::number(at.at(1) - at.at(0))));
        QVERIFY2(at.at(2) - at.at(1) >= 35, qPrintable(QString::number(at.at(2) - at.at(1))));
        QTest::qWait(200);
        QCOMPARE(f.received().size(), 33);
    }

    void storedValuesAndChanges()
    {
        Fixture f;
        f.h.writeFile(u"backlight/actual_brightness"_s, "1000\n");
        f.h.writeState(R"({"rgb.enabled": true})");
        f.start();
        f.expect(bytesOf({0xf7, 0x01, 0x3e, 0x3e, 0x3e, 0, 0, 0, 0, 0xbb, 0xed}));
        QCOMPARE(f.settings.store(u"rgb.brightness"_s, 50u), Settings::Changed);
        f.expect(bytesOf({0xf7, 0x01, 0x1f, 0x1f, 0x1f, 0, 0, 0, 0, 0x5e, 0xed}), 33);
        QCOMPARE(f.settings.store(u"rgb.enabled"_s, false), Settings::Changed);
        f.expect(Off, 66);
        QTest::qWait(200);
        QCOMPARE(f.received().size(), 99);
    }

    void followsScreen()
    {
        Fixture f;
        f.h.writeState(R"({"rgb.enabled": true, "rgb.hue": 350, "rgb.saturation": 100, "rgb.brightness": 50})");
        f.start();
        f.expect(RgbApplier::frame(MicroSet::scale(0xff002b, 50, 100)));
        f.screen(3071);
        f.expect(bytesOf({0xf7, 0x01, 0x60, 0x00, 0x10, 0, 0, 0, 0, 0x71, 0xed}), 33);
        f.screen(3071);
        QTest::qWait(200);
        QCOMPARE(f.received().size(), 66);
        f.screen(0);
        f.expect(Off, 66);
        f.screen(123);
        f.expect(RgbApplier::frame(MicroSet::scale(0xff002b, 123 * 50, 4095 * 100)), 99);
        QTest::qWait(200);
        QCOMPARE(f.received().size(), 132);
    }

    void panelPowerOff()
    {
        Fixture f;
        f.h.writeState(R"({"rgb.enabled": true})");
        f.start();
        f.expect(RgbApplier::frame(0xffffff));
        QVERIFY(!f.applier->watchingPower());
        f.power(4);
        f.applier->refresh();
        f.expect(Off, 33);
        QVERIFY(f.applier->watchingPower());
        QTest::qWait(RgbApplier::powerMs + 300);
        QCOMPARE(f.received().size(), 66);
        QVERIFY(f.applier->watchingPower());
        f.power(0);
        QElapsedTimer back;
        back.start();
        QTRY_COMPARE_WITH_TIMEOUT(f.received().size(), 99, RgbApplier::powerMs + 500);
        QVERIFY2(back.elapsed() <= RgbApplier::powerMs + 300, qPrintable(QString::number(back.elapsed())));
        QVERIFY(f.received().endsWith(RgbApplier::frame(0xffffff).repeated(3)));
        QVERIFY(!f.applier->watchingPower());
        QTest::qWait(RgbApplier::powerMs + 300);
        QVERIFY(!f.applier->watchingPower());
        QCOMPARE(f.received().size(), 99);
    }

    void oneFramePerPass()
    {
        Fixture f;
        f.h.writeState(R"({"rgb.enabled": true, "rgb.saturation": 100})");
        f.start();
        f.expect(RgbApplier::frame(0xff0000));
        QTest::qWait(100);
        QCOMPARE(f.settings.store(u"rgb.hue"_s, 120u), Settings::Changed);
        QCOMPARE(f.settings.store(u"rgb.saturation"_s, 40u), Settings::Changed);
        f.expect(RgbApplier::frame(MicroSet::hueColor(120, 40)), 33);
        QTest::qWait(200);
        QCOMPARE(f.received().size(), 66);
        f.applier->refresh();
        QTest::qWait(200);
        QCOMPARE(f.received().size(), 66);
    }

    void missingBacklightFullScale()
    {
        Fixture f;
        const QString dir = f.h.path(u"nobacklight"_s);
        f.h.writeState(R"({"rgb.enabled": true, "rgb.brightness": 50})");
        f.start({}, dir);
        f.expect(RgbApplier::frame(0x808080));
        QCOMPARE(f.settings.store(u"rgb.brightness"_s, 100u), Settings::Changed);
        f.expect(RgbApplier::frame(0xffffff), 33);
        QCOMPARE(logged(u"rgb: cannot read %1/max_brightness"_s.arg(dir)), 1);
        QCOMPARE(logged(u"rgb: cannot"_s), 1);
    }

    void unreadableScreenRetried()
    {
        Fixture f;
        f.h.writeState(R"({"rgb.enabled": true})");
        f.h.writeFile(u"backlight/actual_brightness"_s, "1000\n");
        f.start();
        const QByteArray dim = bytesOf({0xf7, 0x01, 0x3e, 0x3e, 0x3e, 0, 0, 0, 0, 0xbb, 0xed});
        f.expect(dim);
        f.h.writeFile(u"backlight/actual_brightness"_s, "x\n");
        f.applier->refresh();
        f.expect(RgbApplier::frame(0xffffff), 33);
        QCOMPARE(logged(u"rgb: cannot read %1/actual_brightness: not a number"_s.arg(f.h.path(u"backlight"_s))), 1);
        f.h.writeFile(u"backlight/actual_brightness"_s, "1000\n");
        QTest::qWait(100);
        QCOMPARE(f.received().size(), 66);
        QCOMPARE(f.settings.store(u"rgb.saturation"_s, 0u), Settings::Unchanged);
        QCOMPARE(f.settings.store(u"rgb.brightness"_s, 100u), Settings::Unchanged);
        QCOMPARE(f.settings.store(u"rgb.hue"_s, 10u), Settings::Changed);
        f.expect(dim, 66);
        QCOMPARE(logged(u"rgb: cannot"_s), 1);
    }

    void changeDuringSequenceKeepsSpacing()
    {
        Fixture f;
        f.start();
        QCOMPARE(f.settings.store(u"rgb.enabled"_s, true), Settings::Changed);
        QCOMPARE(f.settings.store(u"rgb.saturation"_s, 100u), Settings::Changed);
        const QByteArray red = RgbApplier::frame(0xff0000);
        QTRY_VERIFY(f.received().endsWith(red + red + red));
        QTest::qWait(200);
        const QByteArray all = f.received();
        QCOMPARE(all.size() % 11, 0);
        QVERIFY(all.startsWith(Off));
        QVERIFY(all.endsWith(red + red + red));
        const QList<qint64> at = f.writes;
        QCOMPARE(at.size() * 11, all.size());
        for (qsizetype i = 1; i < at.size(); ++i)
            QVERIFY2(at.at(i) - at.at(i - 1) >= 35, qPrintable(QString::number(at.at(i) - at.at(i - 1))));
    }

    void offInSleepRestoredOnWake()
    {
        qsizetype atRelease = -1;
        Fixture f;
        f.h.writeState(R"({"rgb.enabled": true, "rgb.saturation": 100})");
        f.start();
        const QByteArray red = RgbApplier::frame(0xff0000);
        f.expect(red);
        QTest::qWait(100);
        QCOMPARE(f.logind->open(), 1);
        SleepInhibitor *inhibitor = f.sleep.get();
        QObject::connect(inhibitor, &SleepInhibitor::heldChanged, inhibitor, [&f, &atRelease, inhibitor] {
            if (!inhibitor->held()) {
                f.drain();
                atRelease = f.received().size();
            }
        });
        f.logind->prepareForSleep(true);
        f.expect(Off, 33);
        QTRY_VERIFY(!f.sleep->held());
        QCOMPARE(atRelease, 66);
        QTRY_COMPARE(f.logind->open(), 0);
        QCOMPARE(f.settings.store(u"rgb.hue"_s, 120u), Settings::Changed);
        f.screen(2048);
        QTest::qWait(200);
        QCOMPARE(f.received().size(), 66);
        f.logind->prepareForSleep(false);
        f.expect(RgbApplier::frame(MicroSet::scale(0x00ff00, 2048, 4095)), 66);
        QTRY_VERIFY(f.sleep->held());
        QCOMPARE(f.sleep->takeCount(), 2);
    }

    void overrideOffInSleep()
    {
        Fixture f;
        f.h.writeState(R"({"rgb.saturation": 100})");
        f.start();
        f.expect(Off);
        const QByteArray red = RgbApplier::frame(0xff0000);
        f.settings.setOverride(u"rgb.enabled"_s, u":1.42"_s, true);
        f.expect(red, 33);
        QTest::qWait(100);
        QCOMPARE(f.logind->open(), 1);
        f.logind->prepareForSleep(true);
        f.expect(Off, 66);
        QTRY_VERIFY(!f.sleep->held());
        f.logind->prepareForSleep(false);
        f.expect(red, 99);
        QTRY_VERIFY(f.sleep->held());
        f.settings.dropOverrides(u":1.42"_s);
        f.expect(Off, 132);
        QTest::qWait(200);
        QCOMPARE(f.received().size(), 165);
    }

    void darkSleepWritesNothing()
    {
        Fixture f;
        f.start();
        f.expect(Off);
        QTest::qWait(100);
        f.logind->prepareForSleep(true);
        QTRY_VERIFY(!f.sleep->held());
        f.logind->prepareForSleep(false);
        QTRY_VERIFY(f.sleep->held());
        QTest::qWait(200);
        QCOMPARE(f.received().size(), 33);
    }

    void sharedInhibitorWaitsForAll()
    {
        bool deferred = false;
        Fixture f;
        f.h.writeState(R"({"rgb.enabled": true})");
        f.start();
        f.expect(RgbApplier::frame(0xffffff));
        QTest::qWait(100);
        QObject::connect(f.sleep.get(), &SleepInhibitor::prepareForSleep, f.sleep.get(), [&](bool start) {
            if (start) {
                f.sleep->defer();
                deferred = true;
            }
        });
        f.logind->prepareForSleep(true);
        QTRY_VERIFY(deferred);
        f.expect(Off, 33);
        QTest::qWait(100);
        QVERIFY(f.sleep->held());
        f.sleep->ready();
        QVERIFY(!f.sleep->held());
        f.sleep->ready();
        QTRY_COMPARE(f.logind->open(), 0);
    }

    void busyWriteRetried()
    {
        Fixture f;
        f.start();
        f.expect(Off);
        QTest::qWait(100);
        f.notifier->setEnabled(false);
        ::fcntl(f.slave, F_SETFL, ::fcntl(f.slave, F_GETFL) | O_NONBLOCK);
        const QByteArray fill(4096, 'x');
        const auto filled = [&] {
            qsizetype total = 0;
            ssize_t written = 0;
            for (const size_t size : {size_t(fill.size()), size_t(1)})
                while ((written = ::write(f.slave, fill.constData(), size)) > 0)
                    total += written;
            return total;
        };
        for (int i = 0; i < 20 && filled() > 0; ++i)
            QTest::qWait(50);
        QCOMPARE(filled(), 0);
        QCOMPARE(errno, EAGAIN);
        QCOMPARE(f.settings.store(u"rgb.enabled"_s, true), Settings::Changed);
        QTest::qWait(200);
        QCOMPARE(logged(u"rgb: cannot write "_s + f.device), 1);
        f.notifier->setEnabled(true);
        f.drain();
        const QByteArray lit = RgbApplier::frame(0xffffff);
        QTRY_VERIFY(f.received().endsWith(lit + lit + lit));
        QTest::qWait(200);
        QVERIFY(f.received().endsWith(lit + lit + lit));
        QCOMPARE(f.received().count(lit), 3);
        QCOMPARE(logged(u"rgb: cannot write"_s), 1);
        QCOMPARE(logged(u"rgb: cannot open"_s), 0);
    }

    void missingDeviceLoggedOnce()
    {
        Fixture f;
        const QString path = f.h.path(u"dev/ttyHS2"_s);
        const QString dir = f.h.path(u"nobacklight"_s);
        f.start(path, dir);
        QCOMPARE(f.settings.store(u"rgb.enabled"_s, true), Settings::Changed);
        QTest::qWait(100);
        QCOMPARE(f.settings.store(u"rgb.brightness"_s, 10u), Settings::Changed);
        QTest::qWait(200);
        QCOMPARE(logged(u"rgb: cannot open "_s + path), 1);
        QCOMPARE(logged(u"rgb: cannot read %1/max_brightness"_s.arg(dir)), 1);
        f.logind->prepareForSleep(true);
        QTRY_VERIFY(!f.sleep->held());
        QCOMPARE(logged(u"rgb: cannot open"_s), 1);
    }
};

QTEST_GUILESS_MAIN(RgbTest)
#include "tst_rgb.moc"
