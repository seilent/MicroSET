#include "fakemicrosetd.h"
#include "steamlibrary.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include <fcntl.h>
#include <sys/stat.h>

using namespace Qt::StringLiterals;

class LibraryTest : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;

    QString root() const { return m_dir.filePath(u"Steam"_s); }
    QString card() const { return m_dir.filePath(u"card"_s); }

    static void write(const QString &path, const QByteArray &data)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(data);
    }

    static QByteArray manifest(const QByteArray &appid, const QByteArray &name, const QByteArray &dir = {})
    {
        return "\"AppState\"\n{\n\t\"appid\"\t\t\"" + appid + "\"\n\t\"name\"\t\t\"" + name + "\"\n\t\"installdir\"\t\t\""
            + (dir.isEmpty() ? appid : dir) + "\"\n}\n";
    }

    static QStringList ids(const QList<SteamLibrary::Game> &games)
    {
        QStringList out;
        for (const SteamLibrary::Game &game : games)
            out << game.appid + u' ' + game.name;
        return out;
    }

private slots:
    void init()
    {
        QDir(root()).removeRecursively();
        QDir(card()).removeRecursively();
    }

    void twoLibrariesSortedByName()
    {
        write(root() + u"/steamapps/libraryfolders.vdf"_s,
              "// written by Steam\n\"libraryfolders\"\n{\n\t\"0\"\n\t{\n\t\t\"path\"\t\t\"" + root().toUtf8()
                  + "\"\n\t\t\"apps\"\n\t\t{\n\t\t\t\"292120\"\t\t\"1\"\n\t\t}\n\t}\n\t\"1\"\n\t{\n\t\t\"path\"\t\t\""
                  + card().toUtf8() + "\"\n\t}\n\t\"contentstatsid\"\t\t\"12\"\n}\n");
        write(root() + u"/steamapps/appmanifest_292120.acf"_s, manifest("292120", "FINAL FANTASY XIII"));
        write(card() + u"/steamapps/appmanifest_2238900.acf"_s,
              manifest("2238900", "STAR OCEAN THE SECOND STORY R"));
        write(card() + u"/steamapps/appmanifest_2161700.acf"_s, manifest("2161700", "Persona 3 Reload"));
        write(card() + u"/steamapps/appmanifest_292120.acf"_s, manifest("292120", "a copy on the card"));
        const SteamLibrary library(root());
        QCOMPARE(library.libraries(), (QStringList{root(), card()}));
        QCOMPARE(ids(library.games()), (QStringList{u"292120 FINAL FANTASY XIII"_s, u"2161700 Persona 3 Reload"_s,
                                                    u"2238900 STAR OCEAN THE SECOND STORY R"_s}));
        QCOMPARE(library.name(u"2238900"_s), u"STAR OCEAN THE SECOND STORY R"_s);
        QCOMPARE(library.name(u"5"_s), QString());
        QCOMPARE(library.name(u"../5"_s), QString());
    }

    void badCopyHidesNoOther()
    {
        write(root() + u"/steamapps/libraryfolders.vdf"_s,
              "\"libraryfolders\"\n{\n\t\"1\"\n\t{\n\t\t\"path\"\t\t\"" + card().toUtf8() + "\"\n\t}\n}\n");
        write(root() + u"/steamapps/appmanifest_292120.acf"_s, manifest("292120", ""));
        write(card() + u"/steamapps/appmanifest_292120.acf"_s, manifest("292120", "FINAL FANTASY XIII"));
        QCOMPARE(ids(SteamLibrary(root()).games()), QStringList{u"292120 FINAL FANTASY XIII"_s});
    }

    void stampFollowsTheFiles()
    {
        const QString apps = root() + u"/steamapps"_s;
        const QString ff13 = apps + u"/appmanifest_292120.acf"_s;
        const QString userdata = root() + u"/userdata"_s;
        const QString shortcuts = userdata + u"/42/config/shortcuts.vdf"_s;
        write(ff13, manifest("292120", "FINAL FANTASY XIII"));
        QVERIFY(QDir().mkpath(apps + u"/common/292120"_s));
        write(shortcuts, "x");
        const auto pin = [](const QString &path, long nsec = 0) {
            const timespec times[2] = {{1000, nsec}, {1000, nsec}};
            QCOMPARE(::utimensat(AT_FDCWD, QFile::encodeName(path).constData(), times, 0), 0);
        };
        for (const QString &path : {apps, ff13, userdata})
            pin(path);
        const SteamLibrary library(root());
        const QByteArray first = library.stamp();
        QCOMPARE(library.stamp(), first);
        write(ff13, manifest("292120", "FF XIII same bytes"));
        pin(ff13);
        pin(apps);
        QCOMPARE(library.stamp(), first);
        pin(ff13, 1);
        const QByteArray rewritten = library.stamp();
        QVERIFY(rewritten != first);
        write(apps + u"/common/292120/toolmanifest.vdf"_s, "\"manifest\"\n{\n}\n");
        pin(apps);
        const QByteArray tool = library.stamp();
        QVERIFY(tool != rewritten);
        QVERIFY(library.games().isEmpty());
        write(apps + u"/appmanifest_2238900.acf"_s, manifest("2238900", "STAR OCEAN THE SECOND STORY R"));
        const QByteArray added = library.stamp();
        QVERIFY(added != tool);
        pin(shortcuts);
        const QByteArray shortcut = library.stamp();
        QVERIFY(shortcut != added);
        write(apps + u"/libraryfolders.vdf"_s,
              "\"libraryfolders\"\n{\n\t\"1\"\n\t{\n\t\t\"path\"\t\t\"" + card().toUtf8() + "\"\n\t}\n}\n");
        const QByteArray folders = library.stamp();
        QVERIFY(folders != shortcut);
        const QString p3r = card() + u"/steamapps/appmanifest_2161700.acf"_s;
        write(p3r, manifest("2161700", "Persona 3 Reload"));
        pin(card() + u"/steamapps"_s);
        pin(p3r);
        const QByteArray second = library.stamp();
        QVERIFY(second != folders);
        pin(p3r, 1);
        const QByteArray secondRewritten = library.stamp();
        QVERIFY(secondRewritten != second);
        QVERIFY(QDir().mkpath(userdata + u"/43"_s));
        pin(userdata);
        QVERIFY(library.stamp() != secondRewritten);
    }

    void noLibraryFile()
    {
        write(root() + u"/steamapps/appmanifest_292120.acf"_s, manifest("292120", "FINAL FANTASY XIII"));
        const SteamLibrary library(root());
        QCOMPARE(library.libraries(), QStringList{root()});
        QCOMPARE(ids(library.games()), QStringList{u"292120 FINAL FANTASY XIII"_s});
    }

    void toolsAndRuntimesLeftOut()
    {
        const QString apps = root() + u"/steamapps/"_s;
        write(apps + u"appmanifest_4628740.acf"_s, manifest("4628740", "Proton 11.0 (ARM64)", "Proton 11.0 (ARM64)"));
        write(apps + u"common/Proton 11.0 (ARM64)/toolmanifest.vdf"_s, "\"manifest\"\n{\n}\n");
        write(apps + u"appmanifest_3810310.acf"_s, manifest("3810310", "Steam Linux Runtime 4.0", "SteamLinuxRuntime_4"));
        QDir().mkpath(apps + u"common/SteamLinuxRuntime_4"_s);
        QVERIFY(QFile::link(u"/nonexistent"_s, apps + u"common/SteamLinuxRuntime_4/toolmanifest.vdf"_s));
        for (const QByteArray &runtime : {"228980", "1826330", "1161040", "993090"})
            write(apps + u"appmanifest_"_s + QString::fromLatin1(runtime) + u".acf"_s, manifest(runtime, "runtime"));
        write(apps + u"appmanifest_292120.acf"_s, manifest("292120", "FINAL FANTASY XIII"));
        const SteamLibrary library(root());
        QCOMPARE(ids(library.games()), QStringList{u"292120 FINAL FANTASY XIII"_s});
        QCOMPARE(library.name(u"993090"_s), u"runtime"_s);
    }

    void badManifestsLeftOut()
    {
        const QString apps = root() + u"/steamapps/"_s;
        write(apps + u"appmanifest_10.acf"_s, manifest("11", "mismatch"));
        write(apps + u"appmanifest_12.acf"_s, "\"AppState\"\n{\n\t\"appid\"\t\t\"12\"\n\t\"name\"\t\t\"unterminated\n");
        write(apps + u"appmanifest_13.acf"_s, "\"AppState\"\n{\n\t\"appid\"\t\t\"13\"\n");
        write(apps + u"appmanifest_14.acf"_s, manifest("14", "big") + QByteArray(SteamLibrary::MaxFile, ' '));
        write(apps + u"appmanifest_15.acf"_s, manifest("15", QByteArray(SteamLibrary::MaxName + 1, 'x')));
        write(apps + u"appmanifest_16.acf"_s, manifest("16", ""));
        write(apps + u"appmanifest_0.acf"_s, manifest("0", "zero"));
        write(apps + u"real_17.acf"_s, manifest("17", "symlinked"));
        QVERIFY(QFile::link(apps + u"real_17.acf"_s, apps + u"appmanifest_17.acf"_s));
        QCOMPARE(::mkfifo(QFile::encodeName(apps + u"appmanifest_18.acf"_s).constData(), 0600), 0);
        write(apps + u"appmanifest_19.acf"_s, manifest("19", QByteArray(SteamLibrary::MaxName, 'y')));
        write(apps + u"appmanifest_20.acf"_s,
              "\"AppState\" { \"appid\" \"20\" \"name\" \"quote \\\" and \\\\ slash\" \"installdir\" \"20\" }");
        write(apps + u"appmanifest_21.acf"_s, "\"AppState\" { \"appid\" \"21\" \"name\" \"no dir\" }");
        write(apps + u"appmanifest_22.acf"_s, "\"AppState\" { \"appid\" \"22\" \"name\" \"empty dir\" \"installdir\" \"\" }");
        write(apps + u"appmanifest_23.acf"_s, manifest("23", "dot", "."));
        write(apps + u"appmanifest_24.acf"_s, manifest("24", "dot dot", ".."));
        write(apps + u"appmanifest_25.acf"_s, manifest("25", "slash", "a/b"));
        const SteamLibrary library(root());
        QCOMPARE(ids(library.games()),
                 (QStringList{u"20 quote \" and \\ slash"_s, u"19 "_s + QString(SteamLibrary::MaxName, u'y')}));
    }

    void appCap()
    {
        const QString apps = root() + u"/steamapps/"_s;
        for (int i = 1; i <= SteamLibrary::MaxApps + 5; ++i)
            write(apps + u"appmanifest_%1.acf"_s.arg(i), manifest(QByteArray::number(i), "game " + QByteArray::number(i)));
        QCOMPARE(SteamLibrary(root()).games().size(), SteamLibrary::MaxApps);
    }

    void examinedCap()
    {
        const QString apps = root() + u"/steamapps/"_s;
        for (int i = 1000; i < 1000 + SteamLibrary::MaxApps; ++i)
            write(apps + u"appmanifest_%1.acf"_s.arg(i), manifest("1", "mismatch"));
        write(apps + u"appmanifest_9999.acf"_s, manifest("9999", "never examined"));
        QVERIFY(SteamLibrary(root()).games().isEmpty());
        QCOMPARE(SteamLibrary(root()).name(u"9999"_s), u"never examined"_s);
    }

    void shortcuts()
    {
        write(root() + u"/steamapps/appmanifest_292120.acf"_s, manifest("292120", "FINAL FANTASY XIII"));
        FakeMicroSetd::writeSteam(m_dir.filePath(u"shortcuts"_s), {},
                                  {{2763258687u, u"Octopath Traveler"_s}, {2342494927u, u"Trails in the Sky 1st Chapter"_s},
                                   {0u, u"no id"_s}});
        QDir().mkpath(root() + u"/userdata/1145555777/config"_s);
        QVERIFY(QFile::copy(m_dir.filePath(u"shortcuts/userdata/1145555777/config/shortcuts.vdf"_s),
                            root() + u"/userdata/1145555777/config/shortcuts.vdf"_s));
        write(root() + u"/userdata/42/config/shortcuts.vdf"_s, QByteArray("\0shortcuts\0\0" "0\0\x09" "bad\0", 19));
        const SteamLibrary library(root());
        QCOMPARE(ids(library.games()), (QStringList{u"292120 FINAL FANTASY XIII"_s, u"2763258687 Octopath Traveler"_s,
                                                    u"2342494927 Trails in the Sky 1st Chapter"_s}));
        QCOMPARE(library.name(u"2763258687"_s), u"Octopath Traveler"_s);
    }

    void binaryParser()
    {
        const QByteArray file("\0shortcuts\0\0" "0\0\x02" "appid\0\x3f\xfb\xb3\xa4\x01" "AppName\0Octo\0"
                              "\x03" "f\0\0\0\0\0\x07" "u\0\0\0\0\0\0\0\0\0\x0a" "i\0\0\0\0\0\0\0\0\0\0tags\0\x08\x08\x08\x08",
                              78);
        const std::optional<QVariantMap> vdf = SteamLibrary::parseBinary(file);
        QVERIFY(vdf);
        const QVariantMap entry = vdf->value(u"shortcuts"_s).toMap().value(u"0"_s).toMap();
        QCOMPARE(entry.value(u"appid"_s).toUInt(), 2763258687u);
        QCOMPARE(entry.value(u"appname"_s).toString(), u"Octo"_s);
        QVERIFY(!SteamLibrary::parseBinary(file.left(30)));
        QVERIFY(!SteamLibrary::parseBinary(QByteArray("\x05x\0", 3)));
        QByteArray deep;
        for (int i = 0; i < 10; ++i)
            deep += QByteArray("\0n\0", 3);
        QVERIFY(!SteamLibrary::parseBinary(deep + QByteArray(11, '\x08')));
    }

    void textParser()
    {
        const std::optional<QVariantMap> vdf = SteamLibrary::parseText("\"A\" { \"B\" \"1\" bare value \"C\" { } }");
        QVERIFY(vdf);
        QCOMPARE(vdf->value(u"a"_s).toMap().value(u"b"_s).toString(), u"1"_s);
        QCOMPARE(vdf->value(u"a"_s).toMap().value(u"bare"_s).toString(), u"value"_s);
        QVERIFY(!SteamLibrary::parseText("\"A\" { \"B\" \"1\" "));
        QVERIFY(!SteamLibrary::parseText("\"A\" \"1\" }"));
        QVERIFY(!SteamLibrary::parseText("\"A\" {"));
        QByteArray deep;
        for (int i = 0; i < 10; ++i)
            deep += "\"n\" { ";
        QVERIFY(!SteamLibrary::parseText(deep + QByteArray(10, '}')));
    }
};

QTEST_GUILESS_MAIN(LibraryTest)
#include "tst_library.moc"
