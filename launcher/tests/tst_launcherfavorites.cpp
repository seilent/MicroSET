#include "appmodel.h"
#include "favoritesmodel.h"
#include "fakemicrosetd.h"

#include <QDBusConnection>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using namespace Qt::StringLiterals;

class FavoritesTest : public QObject
{
    Q_OBJECT

    QTemporaryDir m_data;
    QTemporaryDir m_home;
    QString m_address;
    QProcess m_bus;
    std::unique_ptr<FakeMicroSetd> m_fake;
    AppModel *m_apps = nullptr;
    FavoritesModel *m_favorites = nullptr;

    static QStringList namesOf(const QVariantList &rows)
    {
        QStringList out;
        for (const QVariant &row : rows)
            out << row.toMap().value(u"name"_s).toString();
        return out;
    }

    static QStringList idsOf(const QVariantList &rows)
    {
        QStringList out;
        for (const QVariant &row : rows)
            out << row.toMap().value(u"id"_s).toString();
        return out;
    }

    void install(const QString &id, const QString &name, const QString &extra = {}) const
    {
        const QString dir = m_data.filePath(u"applications"_s);
        QDir().mkpath(dir);
        QFile file(dir + u'/' + id);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(u"[Desktop Entry]\nType=Application\nName=%1\nExec=true\n%2\n"_s.arg(name, extra).toUtf8());
        file.close();
    }

    FavoritesModel *open()
    {
        m_apps->refresh();
        m_favorites =
            new FavoritesModel(QDBusConnection::connectToBus(m_address, u"favorites-model"_s), m_apps, this);
        return m_favorites;
    }

private slots:
    void initTestCase()
    {
        const QString path = m_home.filePath(u"bus"_s);
        m_bus.setStandardOutputFile(QProcess::nullDevice());
        m_bus.setStandardErrorFile(QProcess::nullDevice());
        m_bus.start(u"dbus-daemon"_s,
                    {u"--session"_s, u"--nofork"_s, u"--nopidfile"_s, u"--address=unix:path="_s + path});
        QVERIFY(m_bus.waitForStarted());
        QElapsedTimer ready;
        ready.start();
        while (!QFile::exists(path) && ready.elapsed() < 10000)
            QTest::qWait(20);
        QVERIFY(QFile::exists(path));
        m_address = u"unix:path="_s + path;
        qputenv("XDG_DATA_DIRS", m_data.path().toUtf8());
        qputenv("XDG_DATA_HOME", m_home.filePath(u"home"_s).toUtf8());
        qputenv("XDG_CURRENT_DESKTOP", "Hyprland");
        qputenv("XDG_CONFIG_HOME", m_home.filePath(u"home"_s).toUtf8());
        QDir().mkpath(m_data.filePath(u"applications"_s));
    }

    void cleanupTestCase()
    {
        qunsetenv("XDG_DATA_DIRS");
        qunsetenv("XDG_DATA_HOME");
        qunsetenv("XDG_CURRENT_DESKTOP");
        qunsetenv("XDG_CONFIG_HOME");
        m_bus.terminate();
        if (!m_bus.waitForFinished(3000)) {
            m_bus.kill();
            m_bus.waitForFinished();
        }
    }

    void init()
    {
        m_fake = std::make_unique<FakeMicroSetd>(m_address);
        QVERIFY(m_fake->start());
        m_apps = new AppModel(this);
    }

    void cleanup()
    {
        delete m_favorites;
        m_favorites = nullptr;
        delete m_apps;
        m_apps = nullptr;
        m_fake.reset();
        QDBusConnection::disconnectFromBus(u"favorites-model"_s);
        QDir dir(m_data.filePath(u"applications"_s));
        for (const QString &name : dir.entryList(QDir::Files))
            dir.remove(name);
    }

    void toggleRecordsASet()
    {
        m_fake->setValues({{u"launcher.favorites"_s, QStringList{u"zed.desktop"_s}}});
        install(u"zed.desktop"_s, u"Zed"_s);
        FavoritesModel *favorites = open();
        QCOMPARE(favorites->ids(), QStringList{u"zed.desktop"_s});
        QCOMPARE(namesOf(favorites->favorites()), QStringList{u"Zed"_s});
        QSignalSpy changed(favorites, &FavoritesModel::idsChanged);
        favorites->toggle(u"zed.desktop"_s);
        QCOMPARE(favorites->ids(), QStringList());
        QVERIFY(!changed.isEmpty());
        QTRY_COMPARE(m_fake->sets().size(), 1);
        QCOMPARE(m_fake->sets().last().first, u"launcher.favorites"_s);
        QCOMPARE(m_fake->sets().last().second.toStringList(), QStringList());
        QVERIFY(favorites->favorites().isEmpty());
    }

    void outsideChangeShowsUp()
    {
        m_fake->setValues({{u"launcher.favorites"_s, QStringList{u"foot.desktop"_s}}});
        install(u"foot.desktop"_s, u"Foot"_s);
        install(u"zed.desktop"_s, u"Zed"_s);
        FavoritesModel *favorites = open();
        QCOMPARE(namesOf(favorites->favorites()), QStringList{u"Foot"_s});
        QSignalSpy changed(favorites, &FavoritesModel::favoritesChanged);
        m_fake->emitChanged(u"launcher.favorites"_s, QStringList{u"zed.desktop"_s});
        QTRY_COMPARE(favorites->ids(), QStringList{u"zed.desktop"_s});
        QCOMPARE(namesOf(favorites->favorites()), QStringList{u"Zed"_s});
        QVERIFY(!changed.isEmpty());
    }

    void uninstalledIdIsHiddenButKept()
    {
        m_fake->setValues({{u"launcher.favorites"_s, QStringList{u"absent.desktop"_s, u"zed.desktop"_s}}});
        install(u"zed.desktop"_s, u"Zed"_s);
        FavoritesModel *favorites = open();
        QCOMPARE(favorites->ids(), QStringList({u"absent.desktop"_s, u"zed.desktop"_s}));
        QCOMPARE(idsOf(favorites->favorites()), QStringList{u"zed.desktop"_s});
        favorites->unstar(u"absent.desktop"_s);
        QCOMPARE(favorites->ids(), QStringList{u"zed.desktop"_s});
        QTRY_COMPARE(m_fake->sets().size(), 1);
        QCOMPARE(m_fake->sets().last().second.toStringList(), QStringList{u"zed.desktop"_s});
    }

    void alphabeticalOrder()
    {
        m_fake->setValues({{u"launcher.favorites"_s,
                            QStringList{u"zed.desktop"_s, u"foot.desktop"_s, u"org.example.App.desktop"_s}}});
        install(u"zed.desktop"_s, u"Zed"_s);
        install(u"foot.desktop"_s, u"Foot"_s);
        install(u"org.example.App.desktop"_s, u"App"_s);
        FavoritesModel *favorites = open();
        QCOMPARE(namesOf(favorites->favorites()), QStringList({u"App"_s, u"Foot"_s, u"Zed"_s}));
        QCOMPARE(idsOf(favorites->favorites()),
                 QStringList({u"org.example.App.desktop"_s, u"foot.desktop"_s, u"zed.desktop"_s}));
    }

    void hiddenAppsNeverShow()
    {
        m_fake->setValues({{u"launcher.favorites"_s, QStringList{u"org.es_de.frontend.desktop"_s,
                                                                  u"org.libretro.RetroArch.desktop"_s,
                                                                  u"foot.desktop"_s}}});
        install(u"org.es_de.frontend.desktop"_s, u"ES-DE"_s);
        install(u"org.libretro.RetroArch.desktop"_s, u"RetroArch"_s);
        install(u"foot.desktop"_s, u"Foot"_s);
        FavoritesModel *favorites = open();
        QCOMPARE(favorites->ids().size(), 3);
        QCOMPARE(idsOf(favorites->favorites()), QStringList{u"foot.desktop"_s});
    }

    void rapidDoubleToggleKeepsBoth()
    {
        m_fake->setValues({{u"launcher.favorites"_s, QStringList()}});
        install(u"foot.desktop"_s, u"Foot"_s);
        install(u"zed.desktop"_s, u"Zed"_s);
        FavoritesModel *favorites = open();
        QVERIFY(favorites->ids().isEmpty());
        favorites->toggle(u"zed.desktop"_s);
        favorites->toggle(u"foot.desktop"_s);
        QCOMPARE(favorites->ids(), QStringList({u"zed.desktop"_s, u"foot.desktop"_s}));
        QTRY_COMPARE(m_fake->sets().size(), 2);
        QCOMPARE(m_fake->sets().at(0).second.toStringList(), QStringList{u"zed.desktop"_s});
        QCOMPARE(m_fake->sets().at(1).second.toStringList(),
                 QStringList({u"zed.desktop"_s, u"foot.desktop"_s}));
        QCOMPARE(namesOf(favorites->favorites()), QStringList({u"Foot"_s, u"Zed"_s}));
        QTest::qWait(200);
        QCOMPARE(favorites->ids(), QStringList({u"zed.desktop"_s, u"foot.desktop"_s}));
    }
};

QTEST_GUILESS_MAIN(FavoritesTest)

#include "tst_launcherfavorites.moc"
