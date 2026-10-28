#include "harness.h"
#include "store.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include <sys/stat.h>

using namespace Qt::StringLiterals;

class StoreTest : public QObject
{
    Q_OBJECT

private:
    static QByteArray read(const QString &path)
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }

    static QStringList badFiles(const QString &dir)
    {
        return QDir(dir).entryList({u"settings.json.bad-*"_s}, QDir::Files | QDir::System);
    }

private slots:
    void missingFileIsDefaults()
    {
        Harness h;
        Store store(h.path(u"state"_s));
        store.load();
        QVERIFY(!store.contains(u"power.profile"_s));
        QVERIFY(badFiles(h.path(u"state"_s)).isEmpty());
    }

    void writeReplacesByRename()
    {
        Harness h;
        Store store(h.path(u"state"_s));
        store.load();
        QVERIFY(store.set(u"power.profile"_s, u"low-power"_s));
        struct stat before;
        QCOMPARE(::stat(QFile::encodeName(store.path()).constData(), &before), 0);
        QCOMPARE(before.st_mode & 0777, 0600u);
        QVERIFY(store.set(u"power.tdp"_s, 6));
        struct stat after;
        QCOMPARE(::stat(QFile::encodeName(store.path()).constData(), &after), 0);
        QVERIFY(after.st_ino != before.st_ino);
        QCOMPARE(QDir(h.path(u"state"_s)).entryList(QDir::Files | QDir::Hidden | QDir::System),
                 QStringList{u"settings.json"_s});
        const QJsonObject root = QJsonDocument::fromJson(read(store.path())).object();
        QCOMPARE(root.value("version"_L1).toInt(), 1);
        QCOMPARE(root.value("values"_L1).toObject().value("power.profile"_L1).toString(), u"low-power"_s);
        QCOMPARE(root.value("values"_L1).toObject().value("power.tdp"_L1).toInt(), 6);

        Store reloaded(h.path(u"state"_s));
        reloaded.load();
        QCOMPARE(reloaded.value(u"power.profile"_s).toString(), u"low-power"_s);
    }

    void removedKeysLeaveTheFile()
    {
        Harness h;
        Store store(h.path(u"state"_s));
        store.load();
        QVERIFY(store.set(QJsonObject{{u"app.7.wine.sync"_s, u"fsync"_s}, {u"power.tdp"_s, 6}}));
        QVERIFY(store.set(QJsonObject{{u"power.tdp"_s, 5}}, {u"app.7.wine.sync"_s, u"app.8.wine.sync"_s}));
        QCOMPARE(store.keys(), QStringList{u"power.tdp"_s});
        Store reloaded(h.path(u"state"_s));
        reloaded.load();
        QCOMPARE(reloaded.keys(), QStringList{u"power.tdp"_s});
        QCOMPARE(reloaded.value(u"power.tdp"_s).toInt(), 5);
    }

    void failedWriteKeepsMemory()
    {
        Harness h;
        Store store(h.path(u"state"_s));
        store.load();
        QVERIFY(store.set(u"power.profile"_s, u"low-power"_s));
        QVERIFY(QDir(h.path(u"state"_s)).removeRecursively());
        QVERIFY(!store.set(u"power.profile"_s, u"performance"_s));
        QCOMPARE(store.value(u"power.profile"_s).toString(), u"low-power"_s);
    }

    void badFileMovedAside_data()
    {
        QTest::addColumn<QByteArray>("content");
        QTest::newRow("not json") << QByteArray("{\"version\": 1, \"values\": {");
        QTest::newRow("version 2") << QByteArray("{\"version\": 2, \"values\": {}}");
        QTest::newRow("values not an object") << QByteArray("{\"version\": 1, \"values\": []}");
        QTest::newRow("not an object") << QByteArray("[]");
    }

    void badFileMovedAside()
    {
        QFETCH(QByteArray, content);
        Harness h;
        const QString dir = h.path(u"state"_s);
        h.writeFile(u"state/settings.json.bad-20200101T000000Z"_s, "old");
        h.writeFile(u"state/settings.json"_s, content);
        Store store(dir);
        store.load();
        QVERIFY(!QFileInfo::exists(store.path()));
        const QStringList bad = badFiles(dir);
        QCOMPARE(bad.size(), 1);
        QVERIFY(bad.first() != u"settings.json.bad-20200101T000000Z"_s);
        QCOMPARE(read(dir + u'/' + bad.first()), content);
        QVERIFY(!store.contains(u"power.profile"_s));
        QVERIFY(store.set(u"power.profile"_s, u"performance"_s));
        QCOMPARE(badFiles(dir).size(), 1);
    }

    void symlinkAtStatePathNotFollowed()
    {
        Harness h;
        const QString dir = h.path(u"state"_s);
        h.writeFile(u"outside/target.json"_s, "{\"version\": 1, \"values\": {\"power.profile\": \"performance\"}}");
        const QByteArray original = read(h.path(u"outside/target.json"_s));
        QVERIFY(QFile::link(h.path(u"outside/target.json"_s), dir + u"/settings.json"_s));
        Store store(dir);
        store.load();
        QVERIFY(!store.contains(u"power.profile"_s));
        QVERIFY(store.set(u"power.tdp"_s, 5));
        QVERIFY(!QFileInfo(store.path()).isSymLink());
        QCOMPARE(read(h.path(u"outside/target.json"_s)), original);
        QCOMPARE(badFiles(dir).size(), 1);
        QVERIFY(QFileInfo(dir + u'/' + badFiles(dir).first()).isSymLink());
    }

    void danglingSymlinkAtStatePathNotCreated()
    {
        Harness h;
        const QString dir = h.path(u"state"_s);
        QDir().mkpath(h.path(u"outside"_s));
        QVERIFY(QFile::link(h.path(u"outside/new.json"_s), dir + u"/settings.json"_s));
        Store store(dir);
        store.load();
        QVERIFY(store.set(u"power.tdp"_s, 5));
        QVERIFY(!QFileInfo::exists(h.path(u"outside/new.json"_s)));
        QVERIFY(QFileInfo(store.path()).isFile() && !QFileInfo(store.path()).isSymLink());
    }

    void symlinksAtTempNamesNotFollowed()
    {
        Harness h;
        const QString dir = h.path(u"state"_s);
        QStringList targets;
        for (int i = 0; i < 64; ++i) {
            const QString suffix = u"%1"_s.arg(i, 6, 10, u'0');
            const QString target = h.path(u"outside/t%1"_s.arg(i));
            if (i % 2) {
                h.writeFile(u"outside/t%1"_s.arg(i), "untouched");
                targets << target;
            }
            QVERIFY(QFile::link(target, dir + u"/.settings.json."_s + suffix));
        }
        Store store(dir);
        store.load();
        for (uint i = 0; i < 50; ++i)
            QVERIFY(store.set(u"power.tdp"_s, qint64(3 + i % 8)));
        for (const QString &target : targets)
            QCOMPARE(read(target), QByteArray("untouched"));
        QCOMPARE(QDir(h.path(u"outside"_s)).entryList(QDir::Files).size(), 32);
    }

    void oversizedWriteRefused()
    {
        Harness h;
        Store store(h.path(u"state"_s));
        store.load();
        QVERIFY(store.set(u"power.profile"_s, u"low-power"_s));
        const QByteArray before = read(store.path());
        QVERIFY(!store.set(u"future.blob"_s, QString(2 << 20, u'x')));
        QCOMPARE(read(store.path()), before);
        QVERIFY(!store.contains(u"future.blob"_s));
        QCOMPARE(QDir(h.path(u"state"_s)).entryList(QDir::Files | QDir::Hidden | QDir::System), QStringList{u"settings.json"_s});
    }

    void fifoAtStatePath()
    {
        Harness h;
        const QString dir = h.path(u"state"_s);
        QCOMPARE(::mkfifo(QFile::encodeName(dir + u"/settings.json"_s).constData(), 0600), 0);
        Store store(dir);
        QElapsedTimer timer;
        timer.start();
        store.load();
        QVERIFY(timer.elapsed() < 2000);
        QVERIFY(!QFileInfo::exists(store.path()));
        QCOMPARE(badFiles(dir).size(), 1);
        QVERIFY(store.set(u"power.tdp"_s, 5));
        QVERIFY(QFileInfo(store.path()).isFile());
    }

    void unknownKeysKept()
    {
        Harness h;
        h.writeState("{\"future.key\": [1, 2], \"power.profile\": \"low-power\"}");
        Store store(h.path(u"state"_s));
        store.load();
        QVERIFY(store.set(u"power.tdp"_s, 4));
        Store reloaded(h.path(u"state"_s));
        reloaded.load();
        QVERIFY(reloaded.contains(u"future.key"_s));
        QCOMPARE(reloaded.value(u"power.profile"_s).toString(), u"low-power"_s);
    }
};

QTEST_GUILESS_MAIN(StoreTest)
#include "tst_store.moc"
