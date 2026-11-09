#include "flatpakbackend.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#pragma push_macro("signals")
#undef signals
#include <flatpak.h>
#pragma pop_macro("signals")

using namespace Qt::StringLiterals;

namespace {

constexpr int PayloadBytes = 64 * 1024 * 1024;

QByteArray noise(int size)
{
    QByteArray data(size, '\0');
    quint32 state = 0x12345678u;
    for (int i = 0; i < size; i++) {
        state = state * 1103515245u + 12345u;
        data[i] = char((state >> 16) & 0xff);
    }
    return data;
}

QByteArray pngBytes()
{
    return QByteArray::fromHex("89504e470d0a1a0a0000000d4948445200000008000000080806000000c40fbe8b00000012494441"
                               "54789c63f8cfc0f01f1f6618190a00c2d77f812f71e0010000000049454e44ae426082");
}

QByteArray appData(const QString &id, const QString &name)
{
    return uR"(<?xml version="1.0" encoding="UTF-8"?>
<components version="0.8">
  <component type="desktop-application">
    <id>%1</id>
    <name>%2</name>
    <summary>%2 summary</summary>
    <developer id="example.org"><name>Example Dev</name></developer>
    <metadata_license>CC0-1.0</metadata_license>
    <project_license>MIT</project_license>
    <launchable type="desktop-id">%1.desktop</launchable>
    <icon type="cached" width="64" height="64">%1.png</icon>
    <icon type="cached" width="128" height="128">%1.png</icon>
  </component>
</components>
)"_s.arg(id, name).toUtf8();
}

bool writeFile(const QString &path, const QByteArray &data, bool executable = false)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    if (file.write(data) != data.size())
        return false;
    if (executable)
        file.setPermissions(file.permissions() | QFileDevice::ExeOwner);
    return true;
}

QByteArray gzipData(const QByteArray &data)
{
    QProcess process;
    process.start(u"gzip"_s, {u"-c"_s});
    if (!process.waitForStarted(10000))
        return {};
    process.write(data);
    process.closeWriteChannel();
    if (!process.waitForFinished(30000))
        return {};
    return process.readAllStandardOutput();
}

struct Cancellable
{
    GCancellable *value = g_cancellable_new();

    ~Cancellable()
    {
        g_object_unref(value);
    }
};

bool hasApp(const QList<StoreEntry> &entries, const QString &id)
{
    return std::any_of(entries.begin(), entries.end(), [&id](const StoreEntry &entry) { return entry.id == id; });
}

}

class FlatpakTest : public QObject
{
    Q_OBJECT

    QTemporaryDir m_root;
    QString m_repo;
    QString m_userDir;
    QString m_payloadPath;

    struct Result
    {
        bool ok = false;
        QString output;
    };

    Result run(const QString &program, const QStringList &args)
    {
        QProcess process;
        process.setProcessChannelMode(QProcess::MergedChannels);
        process.start(program, args);
        if (!process.waitForStarted(10000))
            return {false, u"cannot start %1"_s.arg(program)};
        if (!process.waitForFinished(180000)) {
            process.kill();
            process.waitForFinished();
            return {false, u"%1 timed out"_s.arg(program)};
        }
        const bool ok = process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
        return {ok, QString::fromUtf8(process.readAll())};
    }

    void exportBuild(const QString &dir, bool appstream)
    {
        QStringList args{u"build-export"_s, u"--arch=aarch64"_s};
        if (appstream)
            args << u"--update-appstream"_s;
        args << m_repo << dir << u"stable"_s;
        const Result exported = run(u"flatpak"_s, args);
        QVERIFY2(exported.ok, qPrintable(exported.output));
    }

    void updateRepo()
    {
        const Result updated = run(u"flatpak"_s, {u"build-update-repo"_s, m_repo});
        QVERIFY2(updated.ok, qPrintable(updated.output));
    }

    void installRef(const QString &ref)
    {
        const Result installed =
            run(u"flatpak"_s, {u"install"_s, u"--user"_s, u"--arch=aarch64"_s, u"-y"_s, u"local"_s, ref});
        QVERIFY2(installed.ok, qPrintable(installed.output));
    }

    void addRemote()
    {
        const Result added =
            run(u"flatpak"_s, {u"--user"_s, u"remote-add"_s, u"--no-gpg-verify"_s, u"--if-not-exists"_s,
                               u"local"_s, u"file://%1"_s.arg(m_repo)});
        QVERIFY2(added.ok, qPrintable(added.output));
    }

    void bootstrapSdk()
    {
        const QString dir = m_root.filePath(u"build/sdk"_s);
        QVERIFY(writeFile(dir + u"/metadata"_s,
                          "[Runtime]\nname=org.example.Sdk\nruntime=org.example.Sdk/aarch64/stable\n"));
        QVERIFY(QDir().mkpath(dir + u"/usr"_s));
        QVERIFY(QDir().mkpath(dir + u"/files"_s));
        exportBuild(dir, false);
        updateRepo();
        addRemote();
        installRef(u"org.example.Sdk"_s);
    }

    void buildRuntime()
    {
        const QString dir = m_root.filePath(u"build/runtime"_s);
        const Result initialized = run(u"flatpak"_s, {u"build-init"_s, u"--arch=aarch64"_s, u"--type=runtime"_s, dir,
                                                      u"org.example.Platform"_s, u"org.example.Sdk"_s,
                                                      u"org.example.Sdk"_s, u"stable"_s});
        QVERIFY2(initialized.ok, qPrintable(initialized.output));
        exportBuild(dir, false);
        updateRepo();
        installRef(u"org.example.Platform"_s);
    }

    void buildApp(const QString &id, const QString &name)
    {
        const QString dir = m_root.filePath(u"build/"_s + id);
        const Result initialized = run(u"flatpak"_s, {u"build-init"_s, u"--arch=aarch64"_s, dir, id,
                                                      u"org.example.Sdk"_s, u"org.example.Platform"_s, u"stable"_s});
        QVERIFY2(initialized.ok, qPrintable(initialized.output));
        QVERIFY(writeFile(dir + u"/files/bin/hello"_s, "#!/bin/sh\necho hello\n", true));
        QVERIFY(writeFile(dir + u"/files/share/applications/%1.desktop"_s.arg(id),
                          u"[Desktop Entry]\nType=Application\nName=%1\nExec=hello\nIcon=%1\n"_s.arg(id).toUtf8()));
        QVERIFY(writeFile(dir + u"/files/share/app-info/xmls/%1.xml.gz"_s.arg(id), gzipData(appData(id, name))));
        QVERIFY(writeFile(dir + u"/files/share/app-info/icons/flatpak/64x64/%1.png"_s.arg(id), pngBytes()));
        QVERIFY(writeFile(dir + u"/files/share/app-info/icons/flatpak/128x128/%1.png"_s.arg(id), pngBytes()));
        const QString payload = dir + u"/files/payload.bin"_s;
        QVERIFY(QDir().mkpath(QFileInfo(payload).absolutePath()));
        QVERIFY(QFile::copy(m_payloadPath, payload));
        const Result finished = run(u"flatpak"_s, {u"build-finish"_s, u"--command=bin/hello"_s, dir});
        QVERIFY2(finished.ok, qPrintable(finished.output));
        exportBuild(dir, true);
    }

    int installedRuntimeCount()
    {
        GError *gerror = nullptr;
        FlatpakInstallation *installation = flatpak_installation_new_user(nullptr, &gerror);
        if (installation == nullptr) {
            g_clear_error(&gerror);
            return -1;
        }
        GPtrArray *refs =
            flatpak_installation_list_installed_refs_by_kind(installation, FLATPAK_REF_KIND_RUNTIME, nullptr, &gerror);
        g_object_unref(installation);
        if (refs == nullptr) {
            g_clear_error(&gerror);
            return -1;
        }
        const int count = int(refs->len);
        g_ptr_array_unref(refs);
        return count;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_root.isValid());
        m_repo = m_root.filePath(u"repo"_s);
        m_userDir = m_root.filePath(u"user"_s);
        qputenv("FLATPAK_USER_DIR", m_root.filePath(u"build-user"_s).toUtf8());
        qputenv("XDG_DATA_HOME", m_root.filePath(u"data"_s).toUtf8());
        qputenv("XDG_CONFIG_HOME", m_root.filePath(u"config"_s).toUtf8());
        qputenv("XDG_CACHE_HOME", m_root.filePath(u"cache"_s).toUtf8());
        m_payloadPath = m_root.filePath(u"payload.bin"_s);
        QVERIFY(writeFile(m_payloadPath, noise(PayloadBytes)));

        bootstrapSdk();
        buildRuntime();
        buildApp(u"org.example.App"_s, u"Example App"_s);
        buildApp(u"org.example.Other"_s, u"Other App"_s);
        updateRepo();
        const QString index = m_repo + u"/summary.idx"_s;
        if (QFileInfo::exists(index))
            QVERIFY(QFile::remove(index));
    }

    void init()
    {
        QDir dir(m_userDir);
        QVERIFY(!dir.exists() || dir.removeRecursively());
        QVERIFY(QDir().mkpath(m_userDir));
        qputenv("FLATPAK_USER_DIR", m_userDir.toUtf8());
        addRemote();
    }

    void cleanupTestCase()
    {
        qunsetenv("FLATPAK_USER_DIR");
        qunsetenv("XDG_DATA_HOME");
        qunsetenv("XDG_CONFIG_HOME");
        qunsetenv("XDG_CACHE_HOME");
    }

    void syncsAndParsesTheCatalog()
    {
        FlatpakBackend backend(u"local"_s, u"aarch64"_s);
        QString error;
        QVERIFY2(backend.syncCatalog(&error), qPrintable(error));
        const QList<StoreEntry> entries = backend.catalog(&error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(entries.size(), 2);
        const auto app = std::find_if(entries.begin(), entries.end(), [](const StoreEntry &entry) {
            return entry.id == u"org.example.App"_s;
        });
        QVERIFY(app != entries.end());
        QCOMPARE(app->name, u"Example App"_s);
        QCOMPARE(app->summary, u"Example App summary"_s);
        QCOMPARE(app->developer, u"Example Dev"_s);
        QCOMPARE(app->ref, u"app/org.example.App/aarch64/stable"_s);
        QVERIFY(app->icon.endsWith(u"/64x64/org.example.App.png"_s));
        QVERIFY(app->iconLarge.endsWith(u"/128x128/org.example.App.png"_s));
        QVERIFY(QFileInfo::exists(app->icon));
    }

    void installsWithProgress()
    {
        FlatpakBackend backend(u"local"_s, u"aarch64"_s);
        QList<StoreProgress> updates;
        Cancellable cancellable;
        QString error;
        const bool ok = backend.install(
            u"org.example.App"_s, cancellable.value, [&updates](const StoreProgress &update) { updates << update; },
            &error);
        QVERIFY2(ok, qPrintable(error));
        QVERIFY(!updates.isEmpty());
        QVERIFY(std::any_of(updates.begin(), updates.end(), [](const StoreProgress &update) {
            return update.percent > 0;
        }));
        QVERIFY(std::any_of(updates.begin(), updates.end(), [](const StoreProgress &update) {
            return update.installing;
        }));

        const QList<StoreEntry> installed = backend.installed(&error);
        const auto app = std::find_if(installed.begin(), installed.end(), [](const StoreEntry &entry) {
            return entry.id == u"org.example.App"_s;
        });
        QVERIFY(app != installed.end());
        QVERIFY(app->installedSize > 0);
        QCOMPARE(app->name, u"Example App"_s);
    }

    void multiOperationProgressIsMonotonic()
    {
        FlatpakBackend backend(u"local"_s, u"aarch64"_s);
        QList<int> percents;
        QStringList messages;
        bool installingAtFull = true;
        bool installingSeen = false;
        Cancellable cancellable;
        QString error;
        const bool ok = backend.install(
            u"org.example.App"_s, cancellable.value,
            [&percents, &messages, &installingSeen, &installingAtFull](const StoreProgress &update) {
                if (update.percent >= 0)
                    percents << update.percent;
                if (!update.message.isEmpty())
                    messages << update.message;
                if (update.installing) {
                    installingSeen = true;
                    if (update.percent != 100)
                        installingAtFull = false;
                }
            },
            &error);
        QVERIFY2(ok, qPrintable(error));
        QVERIFY(percents.size() > 2);
        for (int i = 1; i < percents.size(); i++)
            QVERIFY(percents.at(i) >= percents.at(i - 1));
        QCOMPARE(percents.last(), 100);
        QVERIFY(installingSeen);
        QVERIFY(installingAtFull);
        QVERIFY(std::any_of(messages.begin(), messages.end(), [](const QString &message) {
            return message.contains(u"org.example.App"_s);
        }));
        QVERIFY(std::any_of(messages.begin(), messages.end(), [](const QString &message) {
            return message.contains(u"org.example.Platform"_s);
        }));
    }

    void cancelsAnInstallFromOutside()
    {
        FlatpakBackend backend(u"local"_s, u"aarch64"_s);
        Cancellable cancellable;
        std::mutex mutex;
        std::condition_variable ready;
        bool started = false;
        QString error;
        bool ok = true;
        std::thread work([&] {
            ok = backend.install(
                u"org.example.Other"_s, cancellable.value,
                [&mutex, &ready, &started](const StoreProgress &) {
                    std::lock_guard<std::mutex> lock(mutex);
                    started = true;
                    ready.notify_one();
                },
                &error);
        });
        {
            std::unique_lock<std::mutex> lock(mutex);
            ready.wait_for(lock, std::chrono::seconds(30), [&started] { return started; });
        }
        QVERIFY(started);
        g_cancellable_cancel(cancellable.value);
        work.join();
        QVERIFY(!ok);
        QVERIFY(g_cancellable_is_cancelled(cancellable.value));
        const QList<StoreEntry> installed = backend.installed(&error);
        QVERIFY(!hasApp(installed, u"org.example.Other"_s));
    }

    void installOfAnInstalledAppEndsDone()
    {
        FlatpakBackend backend(u"local"_s, u"aarch64"_s);
        QString error;
        Cancellable first;
        QVERIFY2(backend.install(u"org.example.App"_s, first.value, {}, &error), qPrintable(error));
        QStringList messages;
        Cancellable second;
        const bool ok = backend.install(
            u"org.example.App"_s, second.value,
            [&messages](const StoreProgress &update) {
                if (!update.message.isEmpty())
                    messages << update.message;
            },
            &error);
        QVERIFY2(ok, qPrintable(error));
        QVERIFY(!messages.isEmpty());
        QVERIFY(messages.last().contains(u"already installed"_s));
    }

    void uninstallRemovesUnusedRuntimes()
    {
        FlatpakBackend backend(u"local"_s, u"aarch64"_s);
        QString error;
        Cancellable install;
        QVERIFY2(backend.install(u"org.example.App"_s, install.value, {}, &error), qPrintable(error));
        QCOMPARE(backend.installed(&error).size(), 1);
        Cancellable removal;
        QVERIFY2(backend.uninstall(u"org.example.App"_s, removal.value, {}, &error), qPrintable(error));
        QVERIFY(backend.installed(&error).isEmpty());
        QCOMPARE(installedRuntimeCount(), 0);
    }

    void sharedRuntimeKeptWhenAnotherAppUsesIt()
    {
        FlatpakBackend backend(u"local"_s, u"aarch64"_s);
        QString error;
        Cancellable first;
        Cancellable second;
        QVERIFY2(backend.install(u"org.example.App"_s, first.value, {}, &error), qPrintable(error));
        QVERIFY2(backend.install(u"org.example.Other"_s, second.value, {}, &error), qPrintable(error));
        Cancellable removal;
        QVERIFY2(backend.uninstall(u"org.example.App"_s, removal.value, {}, &error), qPrintable(error));
        const QList<StoreEntry> installed = backend.installed(&error);
        QVERIFY(!hasApp(installed, u"org.example.App"_s));
        QVERIFY(hasApp(installed, u"org.example.Other"_s));
        QVERIFY(installedRuntimeCount() > 0);
    }

    void uninstallOfAMissingAppFails()
    {
        FlatpakBackend backend(u"local"_s, u"aarch64"_s);
        QString error;
        Cancellable cancellable;
        QVERIFY(!backend.uninstall(u"org.example.App"_s, cancellable.value, {}, &error));
        QVERIFY(!error.isEmpty());
    }
};

QTEST_GUILESS_MAIN(FlatpakTest)

#include "tst_storeflatpak.moc"
