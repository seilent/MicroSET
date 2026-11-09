#include "storecatalog.h"
#include "storecatalogmodel.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

class StoreCatalogTest : public QObject
{
    Q_OBJECT

    QTemporaryDir m_icons;
    QList<StoreEntry> m_entries;
    QList<StoreEntry> m_iconEntries;
    QString m_icons64;
    QString m_icons128;
    QString m_localIcon;

    static const StoreEntry *entryWithId(const QList<StoreEntry> &entries, const QString &id)
    {
        for (const StoreEntry &entry : entries) {
            if (entry.id == id)
                return &entry;
        }
        return nullptr;
    }

    void touch(const QString &dir, const QString &name)
    {
        QFile file(dir + u'/' + name);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("png");
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_icons.isValid());
        m_icons64 = m_icons.filePath(u"64x64"_s);
        m_icons128 = m_icons.filePath(u"128x128"_s);
        QVERIFY(QDir().mkpath(m_icons64));
        QVERIFY(QDir().mkpath(m_icons128));
        touch(m_icons64, u"org.example.Alpha.png"_s);
        touch(m_icons128, u"org.example.Alpha.png"_s);
        QString error;
        m_entries = parseAppStreamCatalog(u"%1/appstream-fixture.xml"_s.arg(MICROSET_STORE_DATA), m_icons64,
                                          m_icons128, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));

        touch(m_icons64, u"org.example.Cached.png"_s);
        touch(m_icons128, u"org.example.Cached.png"_s);
        touch(m_icons64, u"org.example.Fallback.png"_s);
        touch(m_icons64, u"org.example.Local.png"_s);
        m_localIcon = m_icons64 + u"/org.example.Local.png"_s;
        const QString fixture = uR"(<?xml version="1.0" encoding="UTF-8"?>
<components version="0.8">
  <component type="desktop-application">
    <id>org.example.Cached</id>
    <name>Cached</name>
    <summary>Cached summary</summary>
    <icon type="remote" url="%2"/>
    <icon type="cached" width="64" height="64">org.example.Cached.png</icon>
    <icon type="cached" width="128" height="128">org.example.Cached.png</icon>
  </component>
  <component type="desktop-application">
    <id>org.example.Fallback</id>
    <name>Fallback</name>
    <icon type="cached" width="32" height="32">org.example.Fallback.png</icon>
  </component>
  <component type="desktop-application">
    <id>org.example.Local</id>
    <name>Local</name>
    <icon type="local">%1</icon>
  </component>
  <component type="desktop-application">
    <id>org.example.Traversal</id>
    <name>Traversal</name>
    <icon type="cached" width="64" height="64">../evil.png</icon>
  </component>
  <component type="desktop-application">
    <id>org.example.Duplicate</id>
    <name>First</name>
  </component>
  <component type="desktop-application">
    <id>org.example.Duplicate</id>
    <name>Second</name>
  </component>
</components>
)"_s.arg(m_localIcon, u"https://example.org/remote.png"_s);
        const QString path = m_icons.filePath(u"icons-fixture.xml"_s);
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(file.write(fixture.toUtf8()), qint64(fixture.toUtf8().size()));
        file.close();
        m_iconEntries = parseAppStreamCatalog(path, m_icons64, m_icons128, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
    }

    void parsesDesktopAppsOnly()
    {
        QCOMPARE(m_entries.size(), 4);
        QVERIFY(entryWithId(m_entries, u"org.example.Alpha"_s) != nullptr);
        QVERIFY(entryWithId(m_entries, u"org.example.Beta"_s) != nullptr);
        QVERIFY(entryWithId(m_entries, u"org.example.Gamma"_s) != nullptr);
        QVERIFY(entryWithId(m_entries, u"org.example.Delta"_s) != nullptr);
        QVERIFY(entryWithId(m_entries, u"org.example.Plugin"_s) == nullptr);
    }

    void readsFields()
    {
        const StoreEntry *alpha = entryWithId(m_entries, u"org.example.Alpha"_s);
        QVERIFY(alpha != nullptr);
        QCOMPARE(alpha->name, u"Alpha"_s);
        QCOMPARE(alpha->summary, u"Alpha summary"_s);
        QCOMPARE(alpha->developer, u"Alpha Dev"_s);
        QCOMPARE(alpha->ref, u"app/org.example.Alpha/aarch64/stable"_s);
        const StoreEntry *beta = entryWithId(m_entries, u"org.example.Beta"_s);
        QVERIFY(beta != nullptr);
        QVERIFY(beta->ref.isEmpty());
    }

    void looksUpCachedIcons()
    {
        const StoreEntry *alpha = entryWithId(m_entries, u"org.example.Alpha"_s);
        QVERIFY(alpha != nullptr);
        QCOMPARE(alpha->icon, m_icons64 + u"/org.example.Alpha.png"_s);
        QCOMPARE(alpha->iconLarge, m_icons128 + u"/org.example.Alpha.png"_s);
        const StoreEntry *beta = entryWithId(m_entries, u"org.example.Beta"_s);
        QVERIFY(beta != nullptr);
        QVERIFY(beta->icon.isEmpty());
        QVERIFY(beta->iconLarge.isEmpty());
    }

    void cachedIconWinsOverRemote()
    {
        const StoreEntry *cached = entryWithId(m_iconEntries, u"org.example.Cached"_s);
        QVERIFY(cached != nullptr);
        QCOMPARE(cached->icon, m_icons64 + u"/org.example.Cached.png"_s);
        QCOMPARE(cached->iconLarge, m_icons128 + u"/org.example.Cached.png"_s);
    }

    void fallsBackToAnyCachedIcon()
    {
        const StoreEntry *fallback = entryWithId(m_iconEntries, u"org.example.Fallback"_s);
        QVERIFY(fallback != nullptr);
        QCOMPARE(fallback->icon, m_icons64 + u"/org.example.Fallback.png"_s);
        QVERIFY(fallback->iconLarge.isEmpty());
    }

    void localIconUsesTheFilename()
    {
        const StoreEntry *local = entryWithId(m_iconEntries, u"org.example.Local"_s);
        QVERIFY(local != nullptr);
        QCOMPARE(local->icon, m_localIcon);
        QCOMPARE(local->iconLarge, m_localIcon);
    }

    void rejectsNamesWithASlash()
    {
        const StoreEntry *traversal = entryWithId(m_iconEntries, u"org.example.Traversal"_s);
        QVERIFY(traversal != nullptr);
        QVERIFY(traversal->icon.isEmpty());
        QVERIFY(traversal->iconLarge.isEmpty());
    }

    void dedupesIds()
    {
        QCOMPARE(m_iconEntries.size(), 5);
        int duplicates = 0;
        for (const StoreEntry &entry : m_iconEntries) {
            if (entry.id != u"org.example.Duplicate"_s)
                continue;
            duplicates++;
            QCOMPARE(entry.name, u"First"_s);
        }
        QCOMPARE(duplicates, 1);
    }

    void rowsSortAndCarryLetters()
    {
        StoreCatalogModel model;
        model.setEntries(m_entries);
        QCOMPARE(model.count(), 4);
        QVariantList rows = model.rows();
        QCOMPARE(rows.at(0).toMap().value(u"name"_s).toString(), u"Alpha"_s);
        QCOMPARE(rows.at(1).toMap().value(u"name"_s).toString(), u"Beta"_s);
        QCOMPARE(rows.at(3).toMap().value(u"name"_s).toString(), u"Gamma"_s);
        QCOMPARE(rows.at(0).toMap().value(u"letter"_s).toString(), u"A"_s);
        QCOMPARE(rows.at(0).toMap().value(u"id"_s).toString(), u"org.example.Alpha"_s);
    }

    void searchesLocally()
    {
        StoreCatalogModel model;
        model.setEntries(m_entries);
        model.setSearch(u"graphics"_s);
        QCOMPARE(model.count(), 1);
        QCOMPARE(model.rows().at(0).toMap().value(u"id"_s).toString(), u"org.example.Gamma"_s);
        model.setSearch(u"beta dev"_s);
        QCOMPARE(model.count(), 1);
        QCOMPARE(model.rows().at(0).toMap().value(u"id"_s).toString(), u"org.example.Beta"_s);
        model.setSearch(u"ORG.EXAMPLE.ALPHA"_s);
        QCOMPARE(model.count(), 1);
        model.setSearch(QString());
        QCOMPARE(model.count(), 4);
    }

    void installedStateIsMarked()
    {
        StoreCatalogModel model;
        model.setEntries(m_entries);
        model.setInstalled({u"org.example.Beta"_s});
        const QVariantList rows = model.rows();
        for (const QVariant &row : rows) {
            const QVariantMap values = row.toMap();
            const bool installed = values.value(u"installed"_s).toBool();
            QCOMPARE(installed, values.value(u"id"_s).toString() == u"org.example.Beta"_s);
        }
    }

    void releaseFreesTheCatalog()
    {
        StoreCatalogModel model;
        model.setEntries(m_entries);
        model.setInstalled({u"org.example.Alpha"_s});
        QCOMPARE(model.count(), 4);
        model.release();
        QCOMPARE(model.count(), 0);
        QVERIFY(model.rows().isEmpty());
        QVERIFY(!model.contains(u"org.example.Alpha"_s));
        QVERIFY(model.offline());
        model.setEntries(m_entries);
        for (const QVariant &row : model.rows())
            QVERIFY(!row.toMap().value(u"installed"_s).toBool());
    }

    void offlineFollowsTheCatalog()
    {
        StoreCatalogModel model;
        QVERIFY(model.offline());
        QSignalSpy offline(&model, &StoreCatalogModel::offlineChanged);
        model.setEntries(m_entries);
        QCOMPARE(offline.count(), 1);
        QVERIFY(!model.offline());
        model.setSynced(true);
        QCOMPARE(offline.count(), 1);
        model.release();
        QCOMPARE(offline.count(), 1);
        QVERIFY(!model.offline());
        model.setSynced(false);
        QCOMPARE(offline.count(), 2);
        QVERIFY(model.offline());
        model.setEntries(m_entries);
        QCOMPARE(offline.count(), 3);
        QVERIFY(!model.offline());
    }

    void missingCacheReportsEmpty()
    {
        QString error;
        const QList<StoreEntry> entries = parseAppStreamCatalog(m_icons.filePath(u"absent.xml"_s), m_icons64,
                                                                m_icons128, &error);
        QVERIFY(entries.isEmpty());
        QVERIFY(!error.isEmpty());
    }
};

QTEST_GUILESS_MAIN(StoreCatalogTest)

#include "tst_storecatalog.moc"
