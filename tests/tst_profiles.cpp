#include "harness.h"
#include "protonprofiles.h"
#include "schema.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QTest>

#include <sys/stat.h>
#include <unistd.h>

using namespace Qt::StringLiterals;

class ProfilesTest : public QObject
{
    Q_OBJECT

private:
    static Schema shipped()
    {
        Schema schema;
        for (const QString &applier : {u"power"_s, u"alsa"_s, u"ssc"_s, u"rgb"_s, u"backlight"_s})
            schema.addApplier(applier);
        schema.addProvider(u"sessions"_s, [] { return QStringList{u"steam"_s}; });
        schema.addProvider(u"gpu_undervolts"_s, [] { return QStringList{u"off"_s}; });
        schema.load(QStringLiteral(MICROSET_SOURCE_DIR) + u"/schema"_s);
        return schema;
    }

    static QByteArray file(const QByteArray &profiles)
    {
        return "{\"version\": 1, \"profiles\": {" + profiles + "}}";
    }

    static int check(const Harness &h, const QStringList &arguments, QString *log = nullptr)
    {
        QProcess process;
        process.setProcessEnvironment(h.environment());
        process.setProcessChannelMode(QProcess::MergedChannels);
        process.start(QStringLiteral(MICROSET_DAEMON), arguments);
        if (!process.waitForFinished(10000)) {
            process.kill();
            process.waitForFinished();
            return -1;
        }
        if (log)
            *log = QString::fromUtf8(process.readAll());
        return process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
    }

    static int check(const Harness &h, const QString &path, QString *log = nullptr)
    {
        return check(h, {u"--check-profiles"_s, path}, log);
    }

private slots:
    void valid()
    {
        Harness h;
        h.writeFile(u"profiles.json"_s,
                    file(R"("292120": {"tool": "proton-11.0-", "set": {"fex.tso": "off", "wine.sync": "fsync"}},
                            "7": {"set": {"fex.max_inst": "5000", "fex.smc": "none"}})"));
        ProtonProfiles profiles;
        QVERIFY(profiles.load(h.path(u"profiles.json"_s), shipped()));
        QCOMPARE(profiles.apps(), (QStringList{u"292120"_s, u"7"_s}));
        const ProtonProfiles::Entry *game = profiles.entry(u"292120"_s);
        QVERIFY(game);
        QCOMPARE(game->tool, u"proton-11.0-"_s);
        QCOMPARE(game->set, (QMap<QString, QString>{{u"fex.tso"_s, u"off"_s}, {u"wine.sync"_s, u"fsync"_s}}));
        QVERIFY(profiles.entry(u"7"_s)->tool.isEmpty());
        QVERIFY(!profiles.entry(u"8"_s));
        const QJsonObject expected = QJsonDocument::fromJson(
            R"({"292120": {"tool": "proton-11.0-", "set": {"fex.tso": "off", "wine.sync": "fsync"}},
                "7": {"set": {"fex.max_inst": "5000", "fex.smc": "none"}}})").object();
        QCOMPARE(profiles.toJson(), expected);
        QString log;
        QCOMPARE(check(h, h.path(u"profiles.json"_s), &log), 0);
        QVERIFY(log.contains("profiles.json: 2 profiles"_L1));
    }

    void planExample()
    {
        Harness h;
        h.writeFile(u"profiles.json"_s, R"({
  "version": 1,
  "profiles": {
    "292120": {"tool": "proton-11.0-", "set": {"fex.tso": "off", "wine.sync": "fsync"}}
  }
}
)");
        QCOMPARE(check(h, h.path(u"profiles.json"_s)), 0);
    }

    void badEntries_data()
    {
        QTest::addColumn<QByteArray>("entry");
        QTest::addColumn<QString>("why");
        QTest::newRow("unknown key") << R"("5": {"set": {"fex.nope": "on"}})"_ba << u"fex.nope is not a game option"_s;
        QTest::newRow("long key") << "\"5\": {\"set\": {\"fex." + QByteArray(100, 'x') + "\": \"on\"}}"
                                  << u"fex."_s + QString(60, u'x') + u" is not a game option"_s;
        QTest::newRow("global key") << R"("5": {"set": {"proton.profiles": "off"}})"_ba
                                    << u"proton.profiles is not a game option"_s;
        QTest::newRow("fex.preset") << R"("5": {"set": {"fex.preset": "performance"}})"_ba
                                    << u"fex.preset is not a game option"_s;
        QTest::newRow("profile") << R"("5": {"set": {"profile": "off"}})"_ba << u"profile is not a game option"_s;
        QTest::newRow("bad value") << R"("5": {"set": {"fex.tso": "maybe"}})"_ba << u"bad value for fex.tso"_s;
        QTest::newRow("auto") << R"("5": {"set": {"fex.tso": "auto"}})"_ba << u"bad value for fex.tso"_s;
        QTest::newRow("number") << R"("5": {"set": {"fex.max_inst": 5000}})"_ba << u"bad value for fex.max_inst"_s;
        QTest::newRow("one bad of two") << R"("5": {"set": {"fex.tso": "off", "wine.sync": "maybe"}})"_ba
                                        << u"bad value for wine.sync"_s;
        QTest::newRow("empty set") << R"("5": {"set": {}})"_ba << u"no set"_s;
        QTest::newRow("no set") << R"("5": {"tool": "proton-11.0-"})"_ba << u"no set"_s;
        QTest::newRow("set not an object") << R"("5": {"set": ["fex.tso"]})"_ba << u"no set"_s;
        QTest::newRow("unknown field") << R"("5": {"set": {"fex.tso": "off"}, "note": "x"})"_ba
                                       << u"not an object of tool and set"_s;
        QTest::newRow("not an object") << R"("5": "fex.tso")"_ba << u"not an object of tool and set"_s;
        QTest::newRow("bad tool") << R"("5": {"tool": "proton 11", "set": {"fex.tso": "off"}})"_ba << u"bad tool"_s;
        QTest::newRow("tool number") << R"("5": {"tool": 11, "set": {"fex.tso": "off"}})"_ba << u"bad tool"_s;
        QTest::newRow("appid zero") << R"("0": {"set": {"fex.tso": "off"}})"_ba << u"not an appid"_s;
        QTest::newRow("appid leading zero") << R"("0292120": {"set": {"fex.tso": "off"}})"_ba << u"not an appid"_s;
        QTest::newRow("appid 11 digits") << R"("12345678901": {"set": {"fex.tso": "off"}})"_ba << u"not an appid"_s;
        QTest::newRow("appid text") << R"("ff13": {"set": {"fex.tso": "off"}})"_ba << u"not an appid"_s;
    }

    void badEntries()
    {
        QFETCH(QByteArray, entry);
        QFETCH(QString, why);
        Harness h;
        h.writeFile(u"profiles.json"_s, file(entry + R"(, "292120": {"set": {"fex.tso": "off"}})"));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"profiles.json: profile [^:]*: "_s
                                                              + QRegularExpression::escape(why) + u", dropped$"_s));
        ProtonProfiles profiles;
        QVERIFY(!profiles.load(h.path(u"profiles.json"_s), shipped()));
        QCOMPARE(profiles.apps(), QStringList{u"292120"_s});
        QString log;
        QCOMPARE(check(h, h.path(u"profiles.json"_s), &log), 1);
        QVERIFY(log.contains(why));
    }

    void badFiles_data()
    {
        QTest::addColumn<QByteArray>("content");
        QTest::addColumn<QString>("why");
        QTest::newRow("invalid json") << "{\"version\": 1, \"profiles\": {"_ba << u".+"_s;
        QTest::newRow("wrong version") << R"({"version": 2, "profiles": {}})"_ba << u"not a version 1 profiles file"_s;
        QTest::newRow("version text") << R"({"version": "1", "profiles": {}})"_ba << u"not a version 1 profiles file"_s;
        QTest::newRow("no version") << R"({"profiles": {}})"_ba << u"not a version 1 profiles file"_s;
        QTest::newRow("profiles array") << R"({"version": 1, "profiles": []})"_ba << u"not a version 1 profiles file"_s;
        QTest::newRow("no profiles") << R"({"version": 1})"_ba << u"not a version 1 profiles file"_s;
        QTest::newRow("root array") << R"([1])"_ba << u"not a version 1 profiles file"_s;
    }

    void badFiles()
    {
        QFETCH(QByteArray, content);
        QFETCH(QString, why);
        Harness h;
        h.writeFile(u"profiles.json"_s, content);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"profiles.json: "_s + why + u", no profiles$"_s));
        ProtonProfiles profiles;
        QVERIFY(!profiles.load(h.path(u"profiles.json"_s), shipped()));
        QVERIFY(profiles.apps().isEmpty());
        QCOMPARE(check(h, h.path(u"profiles.json"_s)), 1);
    }

    void missingFile()
    {
        Harness h;
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"profiles.json: No such file or directory, no profiles$"_s));
        ProtonProfiles profiles;
        QVERIFY(!profiles.load(h.path(u"profiles.json"_s), shipped()));
        QVERIFY(profiles.apps().isEmpty());
        QCOMPARE(check(h, h.path(u"profiles.json"_s)), 1);
    }

    void sizeCap()
    {
        Harness h;
        const QByteArray head = file(R"("292120": {"set": {"fex.tso": "off"}})");
        const QByteArray full = head.chopped(1) + QByteArray(64 * 1024 - head.size(), ' ') + '}';
        QCOMPARE(full.size(), 64 * 1024);
        h.writeFile(u"profiles.json"_s, full);
        ProtonProfiles profiles;
        QVERIFY(profiles.load(h.path(u"profiles.json"_s), shipped()));
        QCOMPARE(profiles.apps(), QStringList{u"292120"_s});
        QCOMPARE(check(h, h.path(u"profiles.json"_s)), 0);
        h.writeFile(u"profiles.json"_s, full.chopped(1) + " }");
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"profiles.json: too large, no profiles$"_s));
        QVERIFY(!profiles.load(h.path(u"profiles.json"_s), shipped()));
        QVERIFY(profiles.apps().isEmpty());
        QCOMPARE(check(h, h.path(u"profiles.json"_s)), 1);
    }

    void symlinkRefused()
    {
        Harness h;
        h.writeFile(u"real.json"_s, file(R"("292120": {"set": {"fex.tso": "off"}})"));
        QVERIFY(QFile::link(h.path(u"real.json"_s), h.path(u"profiles.json"_s)));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"profiles.json: a symlink, no profiles$"_s));
        ProtonProfiles profiles;
        QVERIFY(!profiles.load(h.path(u"profiles.json"_s), shipped()));
        QCOMPARE(check(h, h.path(u"profiles.json"_s)), 1);
    }

    void fifoRefused()
    {
        Harness h;
        QCOMPARE(::mkfifo(QFile::encodeName(h.path(u"profiles.json"_s)).constData(), 0600), 0);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"profiles.json: not a regular file, no profiles$"_s));
        ProtonProfiles profiles;
        QVERIFY(!profiles.load(h.path(u"profiles.json"_s), shipped()));
        QCOMPARE(check(h, h.path(u"profiles.json"_s)), 1);
    }

    void directoryRefused()
    {
        Harness h;
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"state: not a regular file, no profiles$"_s));
        ProtonProfiles profiles;
        QVERIFY(!profiles.load(h.path(u"state"_s), shipped()));
    }

    void checkUsage()
    {
        Harness h;
        h.writeFile(u"profiles.json"_s, file(R"("292120": {"set": {"fex.tso": "off"}})"));
        QString log;
        QCOMPARE(check(h, QStringList{u"--check-profiles"_s}, &log), 2);
        QVERIFY(log.contains("usage: microsetd [--check-profiles <file>]"_L1));
        QCOMPARE(check(h, QStringList{u"--check-profiles"_s, h.path(u"profiles.json"_s), u"x"_s}), 2);
        QCOMPARE(check(h, QStringList{u"--check"_s, h.path(u"profiles.json"_s)}), 2);
        QCOMPARE(check(h, QStringList{u"--check-profiles"_s, h.path(u"profiles.json"_s)}), 0);
    }
};

QTEST_GUILESS_MAIN(ProfilesTest)
#include "tst_profiles.moc"
