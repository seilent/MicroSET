#pragma once

#include <QCollator>
#include <QIcon>
#include <QImage>
#include <QHash>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QQuickImageProvider>
#include <QVariantList>

class Activator;
class AppModel;
class GlibThread;

class AppIconProvider : public QQuickImageProvider
{
public:
    AppIconProvider();

    QImage requestImage(const QString &id, QSize *size, const QSize &requested) override;
};

struct IconData
{
    QString file;
    QStringList names;
};

class AppIcons
{
public:
    static void attach(AppModel *model);
    static void detach(AppModel *model);
    static QImage image(const QString &id);
    static void publish(const QHash<QString, IconData> &icons);
    static bool known(const QString &id);

private:
    static QMutex s_lock;
    static QHash<QString, IconData> s_icons;
    static AppModel *s_model;
};

struct AppEntry
{
    QString id;
    QString name;
    QString sortKey;
    QStringList iconNames;
    QString iconFile;
    QString command;
    QString startupClass;
    QString flatpak;
};

class AppModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList entries READ entries NOTIFY entriesChanged)
    Q_PROPERTY(int count READ count NOTIFY entriesChanged)

public:
    static QString steamPrefix();
    static QString steamLoaderId();
    static bool hidden(const QString &id, const QString &command);
    static QString iconSource(const QString &id);

    explicit AppModel(QObject *parent = nullptr);
    ~AppModel() override;

    void shutdown();

    QVariantList entries() const;
    QVariantList entriesFor(const QStringList &ids) const;
    int count() const;
    const AppEntry *find(const QString &id) const;
    QString nameOf(const QString &id) const;
    QString classOf(const QString &class_) const;

    void startMonitor(GlibThread *glib);
    void stopMonitor();
    void refresh();
    void setEntriesForTest(const QVariantList &entries);
    void launch(const QString &id, Activator *activator);
    void reportLaunched(const QString &appId, qint64 pid);

signals:
    void entriesChanged();
    void launched(const QString &appId, qint64 pid);

private:
    void setEntries(const QList<AppEntry> &entries);
    void createMonitor();

    QHash<QString, IconData> m_icons;
    QList<AppEntry> m_entries;
    QVariantList m_variants;
    QCollator m_collator;
    void *m_monitor = nullptr;
    GlibThread *m_glib = nullptr;
};
