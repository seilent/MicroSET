#include "fakepowerd.h"
#include "harness.h"

#include <microset/client.h>

#include <QDBusConnectionInterface>
#include <QDBusVariant>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>
#include <QXmlStreamReader>

#include <memory>

using namespace Qt::StringLiterals;

class BusTest : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<Harness> h;
    std::unique_ptr<FakePowerd> powerd;

    bool up(bool withPowerd = true)
    {
        h->writeSessions();
        if (!h->startBus())
            return false;
        powerd = std::make_unique<FakePowerd>(h->address());
        if (withPowerd && !powerd->start())
            return false;
        const bool ok = h->startDaemon()
            && (!withPowerd || QTest::qWaitFor([&] { return h->daemonLog().contains("push to"_L1); }, 10000));
        if (!ok)
            qWarning("microsetd: %s", qPrintable(h->daemonLog()));
        return ok;
    }

    static QString errorOf(const QDBusMessage &reply)
    {
        return reply.type() == QDBusMessage::ErrorMessage ? reply.errorName() : QString();
    }

    QVariant get(const QString &key) const
    {
        return h->call(u"Get"_s, {key}).arguments().value(0).value<QDBusVariant>().variant();
    }

    QString set(const QString &key, const QVariant &value) const
    {
        return errorOf(h->call(u"Set"_s, {key, QVariant::fromValue(QDBusVariant(value))}));
    }

    static QStringList members(const QString &xml)
    {
        QStringList out;
        QXmlStreamReader reader(xml);
        bool inside = false;
        QString current;
        QStringList in;
        QStringList outArgs;
        while (!reader.atEnd()) {
            reader.readNext();
            const auto name = reader.name();
            if (reader.isStartElement() && name == "interface"_L1)
                inside = reader.attributes().value("name"_L1) == "net.seilent.MicroSet1"_L1;
            else if (reader.isEndElement() && name == "interface"_L1)
                inside = false;
            if (!inside)
                continue;
            if (reader.isStartElement() && (name == "method"_L1 || name == "signal"_L1)) {
                current = name.toString() + u' ' + reader.attributes().value("name"_L1).toString();
                in.clear();
                outArgs.clear();
            } else if (reader.isStartElement() && name == "arg"_L1) {
                const QString type = reader.attributes().value("type"_L1).toString();
                if (current.startsWith("method"_L1) && reader.attributes().value("direction"_L1) == "out"_L1)
                    outArgs << type;
                else
                    in << type;
            } else if (reader.isEndElement() && (name == "method"_L1 || name == "signal"_L1)) {
                out << current + u'(' + in.join(u',') + u")("_s + outArgs.join(u',') + u')';
            }
        }
        out.sort();
        return out;
    }

    void waitPushes(int count)
    {
        QTRY_COMPARE_WITH_TIMEOUT(h->daemonLog().count("push to"_L1), count, 10000);
    }

private slots:
    void init()
    {
        h = std::make_unique<Harness>();
    }

    void cleanup()
    {
        h->stopDaemon();
        powerd.reset();
        h.reset();
    }

    void interfaceMatchesXml()
    {
        QVERIFY(up());
        QDBusMessage call = QDBusMessage::createMethodCall(u"net.seilent.MicroSet"_s, u"/net/seilent/MicroSet"_s,
                                                           u"org.freedesktop.DBus.Introspectable"_s, u"Introspect"_s);
        const QDBusMessage reply = h->client().call(call, QDBus::BlockWithGui);
        QFile xml(QStringLiteral(MICROSET_SOURCE_DIR) + u"/data/net.seilent.MicroSet1.xml"_s);
        QVERIFY(xml.open(QIODevice::ReadOnly));
        const QStringList shipped = members(QString::fromUtf8(xml.readAll()));
        QCOMPARE(shipped.size(), 5);
        QCOMPARE(members(reply.arguments().value(0).toString()), shipped);
    }

    void getAfterSet()
    {
        QVERIFY(up());
        powerd->adjust = [](const QString &name, const QVariant &value) {
            return name == "ManualGpuClock"_L1 ? QVariant(525u) : value;
        };
        QCOMPARE(set(u"power.gpu_clock"_s, 587u), QString());
        QCOMPARE(get(u"power.gpu_clock"_s), QVariant(525u));
        QVERIFY(powerd->sets.contains(u"Power1.ManualGpuClock=587"_s));

        MicroSet::Client client(h->client());
        QSignalSpy spy(&client, &MicroSet::Client::changed);
        powerd->values[FakePowerd::Power1][u"Profile"_s] = u"low-power"_s;
        QCOMPARE(set(u"power.profile"_s, u"low-power"_s), QString());
        QCOMPARE(get(u"power.profile"_s), QVariant(u"low-power"_s));
        QTRY_COMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), u"power.profile"_s);
        QCOMPARE(spy.at(0).at(1), QVariant(u"low-power"_s));
        QVERIFY(h->readState().contains("\"power.profile\": \"low-power\""));
    }

    void pushDifferingOnly()
    {
        h->writeState(R"({"power.profile": "low-power", "power.tdp": 6, "power.governor": "schedutil"})");
        QVERIFY(up());
        QCOMPARE(powerd->sets, (QStringList{u"Power1.Profile=low-power"_s, u"TdpLimit1.TdpLimit=6"_s}));
        QVERIFY(h->daemonLog().contains("power.profile=low-power power.tdp=6"_L1));
        powerd->sets.clear();
        h->stopDaemon();
        QVERIFY(h->startDaemon());
        waitPushes(1);
        QVERIFY(h->daemonLog().contains("nothing differs"_L1));
        QVERIFY(powerd->sets.isEmpty());
    }

    void startSignalsIgnored()
    {
        h->writeState(R"({"power.profile": "low-power"})");
        h->writeSessions();
        QVERIFY(h->startBus());
        powerd = std::make_unique<FakePowerd>(h->address());
        powerd->beforeReply = {{u"Profile"_s, u"performance"_s}, {u"CpuGovernor"_s, u"ondemand"_s}};
        QVERIFY(powerd->start());
        QVERIFY(h->startDaemon());
        waitPushes(1);
        QCOMPARE(powerd->sets, QStringList{u"Power1.Profile=low-power"_s});
        QCOMPARE(get(u"power.profile"_s), QVariant(u"low-power"_s));
        QCOMPARE(get(u"power.governor"_s), QVariant(u"schedutil"_s));
        powerd->emitChanged(FakePowerd::Power1, {{u"Profile"_s, u"performance"_s}});
        QTRY_COMPARE(get(u"power.profile"_s), QVariant(u"performance"_s));
    }

    void failedPushEndsWindow()
    {
        h->writeState(R"({"power.profile": "low-power"})");
        h->writeSessions();
        QVERIFY(h->startBus());
        powerd = std::make_unique<FakePowerd>(h->address());
        powerd->failSets = 1;
        powerd->afterFailure = {{u"GpuLevel"_s, u"manual"_s}};
        QVERIFY(powerd->start());
        QVERIFY(h->startDaemon());
        waitPushes(1);
        QTRY_COMPARE(get(u"power.gpu_level"_s), QVariant(u"manual"_s));
        QCOMPARE(powerd->sets, QStringList{u"Power1.Profile=low-power"_s});
        QVERIFY(h->daemonLog().contains("power.profile=low-power failed (org.freedesktop.DBus.Error.InvalidArgs)"_L1));
    }

    void windowEndsAtLastPush()
    {
        h->writeState(R"({"power.profile": "low-power", "power.tdp": 6})");
        h->writeSessions();
        QVERIFY(h->startBus());
        powerd = std::make_unique<FakePowerd>(h->address());
        powerd->failSets = 1;
        powerd->afterFailure = {{u"GpuLevel"_s, u"manual"_s}};
        QVERIFY(powerd->start());
        QVERIFY(h->startDaemon());
        waitPushes(1);
        QCOMPARE(powerd->sets, (QStringList{u"Power1.Profile=low-power"_s, u"TdpLimit1.TdpLimit=6"_s}));
        QTest::qWait(200);
        QCOMPARE(get(u"power.gpu_level"_s), QVariant(u"auto"_s));
        powerd->emitChanged(FakePowerd::Power1, {{u"CpuGovernor"_s, u"ondemand"_s}});
        QTRY_COMPARE(get(u"power.governor"_s), QVariant(u"ondemand"_s));
        QCOMPARE(get(u"power.gpu_level"_s), QVariant(u"auto"_s));
    }

    void valuesLoggedOnlyWhenValid()
    {
        QVERIFY(up());
        const QString invalid = u"net.seilent.MicroSet1.Error.InvalidValue"_s;
        QCOMPARE(set(u"session.default"_s, u"secretvalue"_s), invalid);
        QCOMPARE(set(u"session.default"_s, QString(300, u'~')), invalid);
        QCOMPARE(h->cli({u"set"_s, u"session.default"_s, u"secondsecret"_s}, Harness::noWheel(), Harness::UserCgroup), 4);
        QCOMPARE(set(u"session.default"_s, u"microdap"_s), QString());
        QTRY_VERIFY(h->daemonLog().contains("set session.default=microdap by uid 0"_L1));
        const QString log = h->daemonLog();
        QVERIFY(!log.contains("secretvalue"_L1));
        QVERIFY(!log.contains("secondsecret"_L1));
        QVERIFY(!log.contains("~~~~"_L1));
        QVERIFY(log.contains("set session.default by uid"_L1));
    }

    void laterSignalsClamped()
    {
        QVERIFY(up());
        powerd->emitChanged(FakePowerd::Power1, {{u"ManualGpuClock"_s, 200u}});
        QTRY_COMPARE(get(u"power.gpu_clock"_s), QVariant(305u));
        powerd->emitChanged(FakePowerd::Tdp1, {{u"TdpLimit"_s, 1u}});
        QTRY_COMPARE(get(u"power.tdp"_s), QVariant(3u));
        powerd->emitChanged(FakePowerd::Tdp1, {{u"TdpLimit"_s, 4294967295u}});
        QTRY_COMPARE(get(u"power.tdp"_s), QVariant(10u));
        powerd->emitChanged(FakePowerd::Power1, {{u"Profile"_s, u"turbo"_s}});
        powerd->emitChanged(FakePowerd::Power1, {{u"CpuGovernor"_s, u"performance"_s}});
        QTRY_COMPARE(get(u"power.governor"_s), QVariant(u"performance"_s));
        QCOMPARE(get(u"power.profile"_s), QVariant(u"balanced"_s));
        QVERIFY(h->daemonLog().contains("powerd reported power.profile=turbo, outside the schema"_L1));

        QDBusConnection intruder = QDBusConnection::connectToBus(h->address(), u"intruder"_s);
        QDBusMessage signal = QDBusMessage::createSignal(u"/net/seilent/MicroOs/Power"_s,
                                                         u"org.freedesktop.DBus.Properties"_s, u"PropertiesChanged"_s);
        signal << FakePowerd::Power1 << QVariantMap{{u"Profile"_s, u"low-power"_s}} << QStringList();
        QVERIFY(intruder.send(signal));
        powerd->emitChanged(FakePowerd::Power1, {{u"GpuLevel"_s, u"manual"_s}});
        QTRY_COMPARE(get(u"power.gpu_level"_s), QVariant(u"manual"_s));
        QCOMPARE(get(u"power.profile"_s), QVariant(u"balanced"_s));
        QDBusConnection::disconnectFromBus(u"intruder"_s);
    }

    void powerdRestartPushesAgain()
    {
        QVERIFY(up());
        QCOMPARE(set(u"power.profile"_s, u"performance"_s), QString());
        powerd->stop();
        QTRY_VERIFY(h->daemonLog().contains("powerd left the bus"_L1));
        QCOMPARE(set(u"power.profile"_s, u"low-power"_s), u"org.freedesktop.DBus.Error.ServiceUnknown"_s);
        QCOMPARE(get(u"power.profile"_s), QVariant(u"performance"_s));
        powerd = std::make_unique<FakePowerd>(h->address());
        QVERIFY(powerd->start());
        waitPushes(2);
        QCOMPARE(powerd->sets, QStringList{u"Power1.Profile=performance"_s});
    }

    void powerdAbsent()
    {
        QVERIFY(up(false));
        QCOMPARE(set(u"power.profile"_s, u"low-power"_s), u"org.freedesktop.DBus.Error.ServiceUnknown"_s);
        QCOMPARE(get(u"power.profile"_s), QVariant(u"balanced"_s));
        QCOMPARE(set(u"session.default"_s, u"microdap"_s), QString());
        QCOMPARE(get(u"session.default"_s), QVariant(u"microdap"_s));
    }

    void typeAndRange()
    {
        QVERIFY(up());
        const QString invalid = u"net.seilent.MicroSet1.Error.InvalidValue"_s;
        QCOMPARE(set(u"power.gpu_clock"_s, 400), invalid);
        QCOMPARE(set(u"power.gpu_clock"_s, 900u), invalid);
        QCOMPARE(set(u"power.gpu_clock"_s, u"400"_s), invalid);
        QCOMPARE(set(u"power.tdp"_s, 2u), invalid);
        QCOMPARE(set(u"power.profile"_s, u"turbo"_s), invalid);
        QCOMPARE(set(u"power.governor"_s, u"userspace"_s), invalid);
        QCOMPARE(set(u"session.default"_s, u"recovery"_s), invalid);
        QCOMPARE(set(u"session.default"_s, u"nope"_s), invalid);
        QCOMPARE(set(u"meta.imported_power"_s, true), u"net.seilent.MicroSet1.Error.ReadOnly"_s);
        QCOMPARE(set(u"nope.key"_s, true), u"net.seilent.MicroSet1.Error.UnknownKey"_s);
        QCOMPARE(errorOf(h->call(u"Get"_s, {u"nope.key"_s})), u"net.seilent.MicroSet1.Error.UnknownKey"_s);
        QVERIFY(powerd->sets.isEmpty());
        QVERIFY(h->readState().isEmpty());
    }

    void sameValueNoSignal()
    {
        QVERIFY(up());
        MicroSet::Client client(h->client());
        QSignalSpy spy(&client, &MicroSet::Client::changed);
        QCOMPARE(set(u"session.default"_s, u"steam"_s), QString());
        QVERIFY(h->readState().isEmpty());
        QCOMPARE(set(u"session.default"_s, u"microdap"_s), QString());
        QTRY_COMPARE(spy.count(), 1);
        QCOMPARE(set(u"session.default"_s, u"microdap"_s), QString());
        QCOMPARE(set(u"power.profile"_s, u"balanced"_s), QString());
        QTest::qWait(300);
        QCOMPARE(spy.count(), 1);
    }

    void unknownAndInvalidStoredKept()
    {
        h->writeState(R"({"future.key": 1, "power.gpu_clock": 9999, "session.default": "gone"})");
        QVERIFY(up());
        const QVariantMap all = qdbus_cast<QVariantMap>(h->call(u"List"_s, {QString()}).arguments().value(0));
        QCOMPARE(all.size(), 7);
        QVERIFY(!all.contains(u"future.key"_s));
        QCOMPARE(all.value(u"power.gpu_clock"_s), QVariant(587u));
        QCOMPARE(all.value(u"session.default"_s), QVariant(u"steam"_s));
        QCOMPARE(set(u"power.tdp"_s, 5u), QString());
        const QJsonObject values = QJsonDocument::fromJson(h->readState()).object().value("values"_L1).toObject();
        QCOMPARE(values.value("future.key"_L1).toInt(), 1);
        QCOMPARE(values.value("power.gpu_clock"_L1).toInt(), 9999);
        QCOMPARE(values.value("session.default"_L1).toString(), u"gone"_s);
        QCOMPARE(values.value("power.tdp"_L1).toInt(), 5);
        const QVariantMap power = qdbus_cast<QVariantMap>(h->call(u"List"_s, {u"power"_s}).arguments().value(0));
        QCOMPARE(power.size(), 5);
    }

    void auth()
    {
        QVERIFY(up());
        const QStringList profile = {u"set"_s, u"power.profile"_s};
        QCOMPARE(h->cli(profile + QStringList{u"performance"_s}, Harness::wheel(), Harness::UserCgroup), 0);
        QCOMPARE(h->cli(profile + QStringList{u"low-power"_s}, Harness::noWheel(), Harness::UserCgroup), 4);
        QCOMPARE(h->cli(profile + QStringList{u"low-power"_s}, Harness::wheel(), Harness::GameCgroup), 4);
        QCOMPARE(h->cli({u"set"_s, u"session.default"_s, u"microdap"_s}, Harness::noWheel(), Harness::UserCgroup), 4);
        QString out;
        QCOMPARE(h->cli({u"get"_s, u"power.profile"_s}, Harness::noWheel(), Harness::GameCgroup, &out), 0);
        QCOMPARE(out, u"performance\n"_s);
        QCOMPARE(h->cli({u"set"_s, u"session.default"_s, u"microdap"_s}), 0);
        QCOMPARE(powerd->sets, QStringList{u"Power1.Profile=performance"_s});
        const QString log = h->daemonLog();
        const QString uid = QString::number(Harness::user());
        QVERIFY(log.contains(u"set power.profile=performance by uid %1\n"_s.arg(uid)));
        QVERIFY(log.contains(u"set power.profile by uid %1 refused: not in wheel"_s.arg(uid)));
        QVERIFY(log.contains(u"set power.profile by uid %1 refused: called from a game"_s.arg(uid)));
        QVERIFY(!log.contains("low-power"_L1));
        QVERIFY(log.contains("set session.default=microdap by uid 0\n"_L1));
    }

    void cliExitCodes()
    {
        QVERIFY(up());
        QString out;
        QCOMPARE(h->cli({}), 1);
        QCOMPARE(h->cli({u"get"_s, u"nope.key"_s}), 2);
        QCOMPARE(h->cli({u"set"_s, u"nope.key"_s, u"1"_s}), 2);
        QCOMPARE(h->cli({u"set"_s, u"power.gpu_clock"_s, u"abc"_s}), 3);
        QCOMPARE(h->cli({u"set"_s, u"power.gpu_clock"_s, u"900"_s}), 3);
        QCOMPARE(h->cli({u"set"_s, u"meta.imported_power"_s, u"true"_s}), 3);
        QCOMPARE(h->cli({u"set"_s, u"power.tdp"_s, u"6"_s}), 0);
        QCOMPARE(h->cli({u"get"_s, u"power.tdp"_s}, {}, Harness::RootCgroup, &out), 0);
        QCOMPARE(out, u"6\n"_s);
        QCOMPARE(h->cli({u"get"_s, u"meta.imported_power"_s}, {}, Harness::RootCgroup, &out), 0);
        QCOMPARE(out, u"false\n"_s);
        QCOMPARE(h->cli({u"list"_s, u"power"_s}, {}, Harness::RootCgroup, &out), 0);
        QCOMPARE(out, u"power.governor\tschedutil\npower.gpu_clock\t587\npower.gpu_level\tauto\npower.profile\tbalanced\npower.tdp\t6\n"_s);
        QCOMPARE(h->cli({u"schema"_s, u"session"_s}, {}, Harness::RootCgroup, &out), 0);
        const QJsonObject session = QJsonDocument::fromJson(out.toUtf8()).object().value("session.default"_L1).toObject();
        QCOMPARE(session.value("enum"_L1).toVariant().toStringList(), (QStringList{u"microdap"_s, u"steam"_s}));
        h->stopDaemon();
        QCOMPARE(h->cli({u"get"_s, u"power.tdp"_s}), 5);
        QCOMPARE(h->cli({u"set"_s, u"power.tdp"_s, u"5"_s}), 5);
    }

    void cliWatch()
    {
        QVERIFY(up());
        QProcess watch;
        watch.setProcessEnvironment(h->environment());
        watch.start(QStringLiteral(MICROSET_CLI), {u"watch"_s, u"session"_s});
        QVERIFY(watch.waitForStarted());
        QTest::qWait(1000);
        QCOMPARE(set(u"power.tdp"_s, 4u), QString());
        QCOMPARE(set(u"session.default"_s, u"microdap"_s), QString());
        QTRY_COMPARE(QString::fromUtf8(watch.peek(1024)), u"session.default\tmicrodap\n"_s);
        watch.kill();
        watch.waitForFinished();
    }

    void importAndPush()
    {
        QFile fixture(QStringLiteral(MICROSET_SOURCE_DIR) + u"/tests/fixtures/power.conf"_s);
        QVERIFY(fixture.open(QIODevice::ReadOnly));
        h->writeFile(u"power.conf"_s, fixture.readAll());
        QVERIFY(up());
        QCOMPARE(powerd->sets, (QStringList{u"Power1.Profile=low-power"_s, u"Power1.GpuLevel=manual"_s,
                                            u"Power1.ManualGpuClock=441"_s, u"Power1.CpuGovernor=performance"_s,
                                            u"TdpLimit1.TdpLimit=6"_s}));
        QCOMPARE(get(u"meta.imported_power"_s), QVariant(true));
        QVERIFY(h->daemonLog().contains("imported "_L1));
        h->stopDaemon();
        powerd->sets.clear();
        QVERIFY(h->startDaemon());
        waitPushes(1);
        QVERIFY(!h->daemonLog().contains("imported "_L1));
        QVERIFY(powerd->sets.isEmpty());
    }
};

QTEST_GUILESS_MAIN(BusTest)
#include "tst_bus.moc"
