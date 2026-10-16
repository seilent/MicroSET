#include "fakepowerd.h"
#include "harness.h"

#include <microset/client.h>

#include <QDBusConnectionInterface>
#include <QDBusMetaType>
#include <QDBusVariant>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>
#include <QXmlStreamReader>

#include <memory>

#include <sys/inotify.h>
#include <unistd.h>

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

    QString callFrom(const QString &connection, const QString &method, const QVariantList &arguments) const
    {
        QDBusMessage message = QDBusMessage::createMethodCall(u"net.seilent.MicroSet"_s, u"/net/seilent/MicroSet"_s,
                                                              u"net.seilent.MicroSet1"_s, method);
        message.setArguments(arguments);
        return errorOf(QDBusConnection(connection).call(message, QDBus::BlockWithGui, 10000));
    }

    QString override(const QString &connection, const QString &key, const QVariant &value) const
    {
        return callFrom(connection, u"Override"_s, {key, QVariant::fromValue(QDBusVariant(value))});
    }

    QVariantMap stored(const QString &prefix = {}) const
    {
        return qdbus_cast<QVariantMap>(h->call(u"Stored"_s, {prefix}).arguments().value(0));
    }

    int stateWrites(int fd) const
    {
        int count = 0;
        alignas(inotify_event) char buffer[4096];
        ssize_t got = 0;
        while ((got = ::read(fd, buffer, sizeof buffer)) > 0) {
            for (char *at = buffer; at < buffer + got;) {
                const auto *event = reinterpret_cast<const inotify_event *>(at);
                if (event->len && qstrcmp(event->name, "settings.json") == 0)
                    ++count;
                at += sizeof(inotify_event) + event->len;
            }
        }
        return count;
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
        QCOMPARE(shipped.size(), 11);
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
        QCOMPARE(all.size(), 31);
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

    void audioStoredWithoutCard()
    {
        QVERIFY(up());
        QTRY_VERIFY(h->daemonLog().contains("alsa: no card MicroSetTest, waiting for it"_L1));
        QCOMPARE(set(u"audio.hp_gain"_s, u"low"_s), QString());
        QCOMPARE(get(u"audio.hp_gain"_s), QVariant(u"low"_s));
        QCOMPARE(set(u"audio.speaker_gain"_s, 100u), u"net.seilent.MicroSet1.Error.InvalidValue"_s);
        QCOMPARE(set(u"audio.hp_amp"_s, u"CLS_H_HIFI"_s), u"net.seilent.MicroSet1.Error.InvalidValue"_s);
        QVERIFY(h->readState().contains("\"audio.hp_gain\": \"low\""));
    }

    void displayAndRgb()
    {
        QVERIFY(up());
        const QString invalid = u"net.seilent.MicroSet1.Error.InvalidValue"_s;
        QCOMPARE(get(u"display.rotation"_s), QVariant(u"portrait"_s));
        QCOMPARE(set(u"display.rotation"_s, u"landscape"_s), u"net.seilent.MicroSet1.Error.ReadOnly"_s);
        QCOMPARE(set(u"display.orientation"_s, u"sideways"_s), invalid);
        MicroSet::Client client(h->client());
        QSignalSpy spy(&client, &MicroSet::Client::changed);
        QCOMPARE(set(u"display.orientation"_s, u"landscape"_s), QString());
        QCOMPARE(get(u"display.rotation"_s), QVariant(u"landscape"_s));
        QTRY_COMPARE(spy.count(), 2);
        QCOMPARE(spy.at(1).at(0).toString(), u"display.rotation"_s);
        QCOMPARE(spy.at(1).at(1), QVariant(u"landscape"_s));
        QCOMPARE(set(u"rgb.brightness"_s, 256u), invalid);
        QCOMPARE(set(u"rgb.brightness"_s, 255), invalid);
        QCOMPARE(set(u"rgb.color"_s, 0x1000000u), invalid);
        QCOMPARE(set(u"rgb.enabled"_s, 1u), invalid);
        QCOMPARE(set(u"rgb.color"_s, 0xff8000u), QString());
        QCOMPARE(set(u"rgb.brightness"_s, 255u), QString());
        QCOMPARE(get(u"rgb.color"_s), QVariant(0xff8000u));
        QCOMPARE(errorOf(h->call(u"Subscribe"_s, {u"audio.output"_s})), u"org.freedesktop.DBus.Error.InvalidArgs"_s);
        QCOMPARE(errorOf(h->call(u"Subscribe"_s, {u"nope.key"_s})), u"net.seilent.MicroSet1.Error.UnknownKey"_s);
        h->writeCgroup(QCoreApplication::applicationPid(), Harness::GameCgroup);
        QCOMPARE(errorOf(h->call(u"Subscribe"_s, {u"display.rotation"_s})), u"org.freedesktop.DBus.Error.AccessDenied"_s);
        h->writeCgroup(QCoreApplication::applicationPid(), Harness::RootCgroup);
        QTRY_COMPARE(h->daemonLog().count("rgb: cannot open "_L1 + h->path(u"dev/ttyHS2"_s)), 1);
        QVERIFY(!h->readState().contains("display.rotation"));
    }

    void auth()
    {
        QVERIFY(up());
        const QStringList profile = {u"set"_s, u"power.profile"_s};
        QCOMPARE(h->cli(profile + QStringList{u"performance"_s}, Harness::wheel(), Harness::EmuLookalikeCgroup), 0);
        QCOMPARE(h->cli(profile + QStringList{u"performance"_s}, Harness::wheel(), Harness::UserCgroup), 0);
        QCOMPARE(h->cli(profile + QStringList{u"low-power"_s}, Harness::noWheel(), Harness::UserCgroup), 4);
        QCOMPARE(h->cli(profile + QStringList{u"low-power"_s}, Harness::wheel(), Harness::GameCgroup), 4);
        QCOMPARE(h->cli(profile + QStringList{u"low-power"_s}, Harness::wheel(), Harness::EmuCgroup), 4);
        QCOMPARE(h->cli({u"set"_s, u"session.default"_s, u"microdap"_s}, Harness::noWheel(), Harness::UserCgroup), 4);
        QString out;
        QCOMPARE(h->cli({u"get"_s, u"power.profile"_s}, Harness::noWheel(), Harness::GameCgroup, &out), 0);
        QCOMPARE(out, u"performance\n"_s);
        QCOMPARE(h->cli({u"set"_s, u"session.default"_s, u"microdap"_s}), 0);
        QCOMPARE(powerd->sets, (QStringList{u"Power1.Profile=performance"_s, u"Power1.Profile=performance"_s}));
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
        QTRY_COMPARE(QString::fromUtf8(watch.peek(1024)), u"session.default\tsteam\nsession.default\tmicrodap\n"_s);
        watch.kill();
        watch.waitForFinished();
    }

    void cliWatchSubscribesLive()
    {
        QVERIFY(up());
        const QJsonObject display =
            QJsonDocument::fromJson(h->call(u"Schema"_s, {u"display"_s}).arguments().value(0).toString().toUtf8()).object();
        QCOMPARE(display.value("display.rotation"_L1).toObject().value("live"_L1), QJsonValue(true));
        QVERIFY(!display.value("display.orientation"_L1).toObject().contains("live"_L1));
        QProcess watch;
        h->startCli(watch, {u"watch"_s, u"display"_s});
        QVERIFY(watch.waitForStarted());
        QTRY_COMPARE(QString::fromUtf8(watch.peek(1024)),
                     u"display.auto_rotate\ttrue\ndisplay.orientation\tportrait\ndisplay.rotation\tportrait\n"_s);
        QTRY_VERIFY(h->daemonLog().contains("ssc: subscribed"_L1));
        QVERIFY(h->daemonLog().contains("subscribe display.rotation by :"_L1));
        watch.kill();
        watch.waitForFinished();
        QTRY_VERIFY(h->daemonLog().contains("ssc: no subscriber"_L1));
        QVERIFY(h->daemonLog().contains("unsubscribe display.rotation by :"_L1));
    }

    void subscribeAndUnsubscribe()
    {
        QVERIFY(up());
        const QString name = h->client().baseService();
        QCOMPARE(errorOf(h->call(u"Subscribe"_s, {u"display.rotation"_s})), QString());
        QTRY_VERIFY(h->daemonLog().contains("ssc: subscribed\n"_L1));
        QVERIFY(h->daemonLog().contains(u"subscribe display.rotation by %1 uid %2\n"_s.arg(name).arg(::getuid())));
        QCOMPARE(errorOf(h->call(u"Subscribe"_s, {u"display.rotation"_s})), QString());
        QCOMPARE(h->daemonLog().count("subscribe display.rotation by "_L1), 1);
        QCOMPARE(errorOf(h->call(u"Unsubscribe"_s, {u"display.rotation"_s})), QString());
        QTRY_VERIFY(h->daemonLog().contains("ssc: no subscriber\n"_L1));
        QVERIFY(h->daemonLog().contains(u"unsubscribe display.rotation by %1\n"_s.arg(name)));
        QCOMPARE(h->daemonLog().count("ssc: subscribed"_L1), 1);
    }

    void storedValues()
    {
        h->writeState(R"({"future.key": 1, "power.gpu_clock": 9999, "audio.output": "usb", "rgb.color": 255})");
        QVERIFY(up());
        QCOMPARE(stored(), (QVariantMap{{u"audio.output"_s, u"usb"_s}, {u"rgb.color"_s, 255u}}));
        QCOMPARE(set(u"audio.volume_step"_s, 10u), QString());
        QCOMPARE(set(u"audio.eq.usb.filters"_s, QStringList{u"preamp -3"_s, u"peak 100 2 1"_s}), QString());
        QCOMPARE(stored(u"audio"_s), (QVariantMap{{u"audio.output"_s, u"usb"_s}, {u"audio.volume_step"_s, 10u},
                                                  {u"audio.eq.usb.filters"_s,
                                                   QStringList{u"preamp -3"_s, u"peak 100 2 1"_s}}}));
        QCOMPARE(stored(u"rgb"_s), (QVariantMap{{u"rgb.color"_s, 255u}}));
        QCOMPARE(stored(u"display"_s), QVariantMap());
        QCOMPARE(set(u"audio.volume_step"_s, 5u), QString());
        QCOMPARE(stored(u"audio.volume_step"_s), (QVariantMap{{u"audio.volume_step"_s, 5u}}));
        h->stopDaemon();
        h->writeFile(u"state/settings.json"_s, "{\"version\": 1, \"values\": {");
        QVERIFY(h->startDaemon());
        QCOMPARE(stored(), QVariantMap());
        QCOMPARE(get(u"audio.output"_s), QVariant(u"auto"_s));
    }

    void importValues()
    {
        qDBusRegisterMetaType<QMap<QString, QString>>();
        h->writeState(R"({"audio.hp_amp": "ulp", "audio.output": "bt"})");
        QVERIFY(up());
        QCOMPARE(stored(u"audio.output"_s), (QVariantMap{{u"audio.output"_s, u"bt"_s}}));
        QCOMPARE(set(u"audio.bt_codec"_s, u"ldac"_s), QString());
        MicroSet::Client client(h->client());
        QSignalSpy spy(&client, &MicroSet::Client::changed);
        const int fd = ::inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        QVERIFY(fd >= 0);
        QVERIFY(::inotify_add_watch(fd, QFile::encodeName(h->path(u"state"_s)).constData(), IN_MOVED_TO) >= 0);
        const QStringList filters{u"preamp -2"_s, u"lowshelf 105 4 0.7"_s};
        const QVariantMap values{
            {u"audio.output"_s, u"speaker"_s},       {u"audio.hp_amp"_s, u"hifi"_s},
            {u"audio.bt_codec"_s, u"aac"_s},         {u"audio.volume_step"_s, 10u},
            {u"audio.eq.usb.filters"_s, filters},    {u"audio.eq.usb.on"_s, true},
            {u"audio.speaker_gain"_s, 100u},         {u"audio.hp_gain"_s, u"normal"_s},
            {u"audio.eq.bt.filters"_s, QStringList{u"peak 100 1 1"_s}},
            {u"nope.key"_s, true},                   {u"power.tdp"_s, 5u},
            {u"nope.forged\nline"_s, true},
            {u"display.rotation"_s, u"landscape"_s}, {u"meta.imported_power"_s, true},
        };
        const QDBusMessage reply = h->call(u"Import"_s, {values});
        QCOMPARE(errorOf(reply), QString());
        QCOMPARE(reply.arguments().value(0).toStringList(),
                 (QStringList{u"audio.eq.usb.filters"_s, u"audio.eq.usb.on"_s, u"audio.hp_gain"_s, u"audio.volume_step"_s}));
        const QMap<QString, QString> refused = qdbus_cast<QMap<QString, QString>>(reply.arguments().value(1));
        QCOMPARE(refused, (QMap<QString, QString>{{u"audio.speaker_gain"_s, u"not a valid value"_s},
                                                   {u"audio.eq.bt.filters"_s, u"not a valid value"_s},
                                                   {u"nope.key"_s, u"no key"_s},
                                                   {u"nope.forged\nline"_s, u"no key"_s},
                                                   {u"power.tdp"_s, u"set through powerd"_s},
                                                   {u"display.rotation"_s, u"read only"_s},
                                                   {u"meta.imported_power"_s, u"read only"_s}}));
        QCOMPARE(stateWrites(fd), 1);
        ::close(fd);
        QCOMPARE(get(u"audio.output"_s), QVariant(u"bt"_s));
        QCOMPARE(get(u"audio.hp_amp"_s), QVariant(u"ulp"_s));
        QCOMPARE(get(u"audio.bt_codec"_s), QVariant(u"ldac"_s));
        QCOMPARE(get(u"audio.volume_step"_s), QVariant(10u));
        QCOMPARE(get(u"audio.eq.usb.filters"_s), QVariant(filters));
        QCOMPARE(get(u"audio.eq.usb.on"_s), QVariant(true));
        QCOMPARE(get(u"audio.hp_gain"_s), QVariant(u"normal"_s));
        QCOMPARE(get(u"power.tdp"_s), QVariant(10u));
        QVERIFY(powerd->sets.isEmpty());
        QTRY_COMPARE(spy.count(), 4);
        QStringList moved;
        for (const QList<QVariant> &signal : spy)
            moved << signal.at(0).toString();
        moved.sort();
        QCOMPARE(moved, reply.arguments().value(0).toStringList());
        const QString log = h->daemonLog();
        QVERIFY(log.contains("import by uid 0: audio.eq.usb.filters audio.eq.usb.on audio.hp_gain audio.volume_step\n"_L1));
        QVERIFY(log.contains("import audio.speaker_gain by uid 0 refused: not a valid value\n"_L1));
        QVERIFY(log.contains("import by uid 0 refused 2 unknown keys\n"_L1));
        QVERIFY(!log.contains("nope."_L1));
        QVERIFY(!log.contains("forged"_L1));

        const QDBusMessage again = h->call(u"Import"_s, {values});
        QCOMPARE(errorOf(again), QString());
        QVERIFY(again.arguments().value(0).toStringList().isEmpty());
        QCOMPARE((qdbus_cast<QMap<QString, QString>>(again.arguments().value(1))), refused);

        h->writeCgroup(QCoreApplication::applicationPid(), Harness::GameCgroup);
        QCOMPARE(errorOf(h->call(u"Import"_s, {QVariantMap{{u"audio.eq.bt.on"_s, true}}})),
                 u"org.freedesktop.DBus.Error.AccessDenied"_s);
        h->writeCgroup(QCoreApplication::applicationPid(), Harness::RootCgroup);
        QCOMPARE(get(u"audio.eq.bt.on"_s), QVariant(false));
    }

    void importTooManyKeys()
    {
        QVERIFY(up());
        QVariantMap values;
        for (int i = 0; i < 32; ++i)
            values.insert(u"nope.k%1"_s.arg(i), true);
        QCOMPARE(errorOf(h->call(u"Import"_s, {values})), u"org.freedesktop.DBus.Error.InvalidArgs"_s);
        values.remove(u"nope.k0"_s);
        QCOMPARE(errorOf(h->call(u"Import"_s, {values})), QString());
        QVERIFY(h->daemonLog().contains("import by uid 0 refused 31 unknown keys\n"_L1));
    }

    void importWriteFailure()
    {
        qDBusRegisterMetaType<QMap<QString, QString>>();
        QVERIFY(up());
        QVERIFY(QDir(h->path(u"state"_s)).removeRecursively());
        const QDBusMessage reply = h->call(u"Import"_s, {QVariantMap{{u"audio.volume_step"_s, 2u}, {u"nope.key"_s, 1u}}});
        QCOMPARE(errorOf(reply), u"org.freedesktop.DBus.Error.Failed"_s);
        QCOMPARE(get(u"audio.volume_step"_s), QVariant(5u));
        QCOMPARE(stored(), QVariantMap());
    }

    void overrides()
    {
        QVERIFY(up());
        const QString a = u"override-a"_s;
        const QString b = u"override-b"_s;
        const QString nameA = QDBusConnection::connectToBus(h->address(), a).baseService();
        QDBusConnection::connectToBus(h->address(), b);
        MicroSet::Client client(h->client());
        QSignalSpy spy(&client, &MicroSet::Client::changed);
        QCOMPARE(set(u"rgb.color"_s, 0x0000ffu), QString());
        QTRY_COMPARE(spy.count(), 1);

        QCOMPARE(override(a, u"rgb.color"_s, 0xff0000u), QString());
        QCOMPARE(get(u"rgb.color"_s), QVariant(0xff0000u));
        QCOMPARE(override(b, u"rgb.color"_s, 0x00ff00u), QString());
        QCOMPARE(get(u"rgb.color"_s), QVariant(0x00ff00u));
        QCOMPARE(override(a, u"rgb.color"_s, 0x111111u), QString());
        QCOMPARE(get(u"rgb.color"_s), QVariant(0x111111u));
        QCOMPARE(stored(u"rgb"_s), (QVariantMap{{u"rgb.color"_s, 0x0000ffu}}));
        QCOMPARE(callFrom(a, u"ClearOverride"_s, {u"rgb.color"_s}), QString());
        QCOMPARE(get(u"rgb.color"_s), QVariant(0x00ff00u));
        QCOMPARE(callFrom(a, u"ClearOverride"_s, {u"rgb.color"_s}), QString());
        QCOMPARE(get(u"rgb.color"_s), QVariant(0x00ff00u));
        QCOMPARE(override(a, u"rgb.enabled"_s, true), QString());
        QDBusConnection::disconnectFromBus(b);
        QTRY_COMPARE(get(u"rgb.color"_s), QVariant(0x0000ffu));
        QCOMPARE(get(u"rgb.enabled"_s), QVariant(true));
        QTRY_VERIFY(h->daemonLog().contains("override rgb.color dropped: "_L1));
        QDBusConnection::disconnectFromBus(a);
        QTRY_COMPARE(get(u"rgb.enabled"_s), QVariant(false));
        QVERIFY(h->daemonLog().contains(u"override rgb.enabled dropped: %1 left the bus"_s.arg(nameA)));

        QTRY_COMPARE(spy.count(), 8);
        QStringList seen;
        for (const QList<QVariant> &signal : spy)
            seen << signal.at(0).toString() + u'=' + QString::number(signal.at(1).toUInt(), 16);
        QCOMPARE(seen, (QStringList{u"rgb.color=ff"_s, u"rgb.color=ff0000"_s, u"rgb.color=ff00"_s, u"rgb.color=111111"_s,
                                    u"rgb.color=ff00"_s, u"rgb.enabled=1"_s, u"rgb.color=ff"_s, u"rgb.enabled=0"_s}));
        QVERIFY(!h->readState().contains("rgb.enabled"));
    }

    void overrideRefusals()
    {
        QVERIFY(up());
        const QString c = u"override-c"_s;
        QDBusConnection::connectToBus(h->address(), c);
        QCOMPARE(override(c, u"audio.output"_s, u"usb"_s), u"org.freedesktop.DBus.Error.InvalidArgs"_s);
        QCOMPARE(override(c, u"power.profile"_s, u"low-power"_s), u"org.freedesktop.DBus.Error.InvalidArgs"_s);
        QCOMPARE(override(c, u"nope.key"_s, true), u"net.seilent.MicroSet1.Error.UnknownKey"_s);
        QCOMPARE(override(c, u"rgb.brightness"_s, 256u), u"net.seilent.MicroSet1.Error.InvalidValue"_s);
        QCOMPARE(override(c, u"rgb.brightness"_s, 20), u"net.seilent.MicroSet1.Error.InvalidValue"_s);
        QCOMPARE(callFrom(c, u"ClearOverride"_s, {u"audio.output"_s}), u"org.freedesktop.DBus.Error.InvalidArgs"_s);
        h->writeCgroup(QCoreApplication::applicationPid(), Harness::GameCgroup);
        QCOMPARE(override(c, u"rgb.brightness"_s, 20u), u"org.freedesktop.DBus.Error.AccessDenied"_s);
        h->writeCgroup(QCoreApplication::applicationPid(), Harness::RootCgroup);
        QCOMPARE(get(u"rgb.brightness"_s), QVariant(64u));
        QCOMPARE(get(u"audio.output"_s), QVariant(u"auto"_s));
        QVERIFY(h->daemonLog().contains("override rgb.brightness by uid 0 refused: called from a game"_L1));
        QDBusConnection::disconnectFromBus(c);
    }

    void setUnderOverride()
    {
        QVERIFY(up());
        const QString a = u"override-d"_s;
        QDBusConnection::connectToBus(h->address(), a);
        QCOMPARE(override(a, u"rgb.brightness"_s, 200u), QString());
        MicroSet::Client client(h->client());
        QSignalSpy spy(&client, &MicroSet::Client::changed);
        QCOMPARE(set(u"rgb.brightness"_s, 100u), QString());
        QCOMPARE(get(u"rgb.brightness"_s), QVariant(200u));
        QVERIFY(h->readState().contains("\"rgb.brightness\": 100"));
        QCOMPARE(set(u"rgb.brightness"_s, 200u), QString());
        QVERIFY(h->readState().contains("\"rgb.brightness\": 200"));
        QCOMPARE(stored(u"rgb"_s), (QVariantMap{{u"rgb.brightness"_s, 200u}}));
        QCOMPARE(callFrom(a, u"ClearOverride"_s, {u"rgb.brightness"_s}), QString());
        QCOMPARE(get(u"rgb.brightness"_s), QVariant(200u));
        QCOMPARE(override(a, u"rgb.brightness"_s, 50u), QString());
        QTRY_COMPARE(spy.count(), 1);
        QCOMPARE(set(u"rgb.brightness"_s, 100u), QString());
        QCOMPARE(callFrom(a, u"ClearOverride"_s, {u"rgb.brightness"_s}), QString());
        QTRY_COMPARE(spy.count(), 2);
        QTest::qWait(300);
        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy.at(0).at(1), QVariant(50u));
        QCOMPARE(spy.at(1).at(1), QVariant(100u));
        QCOMPARE(get(u"rgb.brightness"_s), QVariant(100u));
        QDBusConnection::disconnectFromBus(a);
    }

    void cliArrays()
    {
        QVERIFY(up());
        QString out;
        const QString filters = uR"(["preamp -3.5","peak 1000 -2 1.41"])"_s;
        QCOMPARE(h->cli({u"set"_s, u"audio.eq.usb.filters"_s, filters}), 0);
        QCOMPARE(h->cli({u"get"_s, u"audio.eq.usb.filters"_s}, {}, Harness::RootCgroup, &out), 0);
        QCOMPARE(out, filters + u'\n');
        QCOMPARE(h->cli({u"set"_s, u"audio.eq.bt.filters"_s, out.trimmed()}), 0);
        QCOMPARE(get(u"audio.eq.bt.filters"_s), QVariant(QStringList{u"preamp -3.5"_s, u"peak 1000 -2 1.41"_s}));
        QCOMPARE(h->cli({u"set"_s, u"audio.eq.usb.filters"_s, u"preamp -3"_s}), 3);
        QCOMPARE(h->cli({u"set"_s, u"audio.eq.usb.filters"_s, uR"(["preamp -3", 5])"_s}), 3);
        QCOMPARE(h->cli({u"set"_s, u"audio.eq.usb.filters"_s, uR"({"preamp": -3})"_s}), 3);
        QCOMPARE(h->cli({u"set"_s, u"audio.eq.usb.filters"_s, uR"(["preamp 3"])"_s}), 3);
        QCOMPARE(h->cli({u"get"_s, u"audio.eq.speaker.filters"_s}, {}, Harness::RootCgroup, &out), 0);
        QCOMPARE(out, uR"(["preamp 0"])"_s + u'\n');
        QCOMPARE(h->cli({u"set"_s, u"audio.eq.usb.name"_s, u"HD 600"_s}), 0);
        QCOMPARE(h->cli({u"stored"_s, u"audio.eq"_s}, {}, Harness::RootCgroup, &out), 0);
        QCOMPARE(out, u"audio.eq.bt.filters\t"_s + filters + u"\naudio.eq.usb.filters\t"_s + filters
                          + u"\naudio.eq.usb.name\tHD 600\n"_s);
        QCOMPARE(h->cli({u"stored"_s, u"rgb"_s}, {}, Harness::RootCgroup, &out), 0);
        QCOMPARE(out, QString());
        QCOMPARE(h->cli({u"stored"_s, u"a"_s, u"b"_s}), 1);
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
