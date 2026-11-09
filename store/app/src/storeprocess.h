#pragma once

#include "padlink.h"

#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <memory>

class AppModel;
class DbusPad;
class IdlePulse;
class LauncherLaunch;
class ManagerEnv;
class QQuickWindow;
class StoreCatalogModel;
class StoreJob;
class StoreProcess;
class StoreWorker;

class StoreApplication : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Application")

public:
    explicit StoreApplication(StoreProcess *process);

public slots:
    void Activate(const QVariantMap &platformData);
    void Open(const QStringList &uris, const QVariantMap &platformData);
    void ActivateAction(const QString &action, const QVariantList &parameter, const QVariantMap &platformData);

private:
    StoreProcess *m_process;
};

class StoreProcess : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "net.seilent.MicroSet.Store1")
    Q_PROPERTY(QVariantList rows READ rows NOTIFY rowsChanged)
    Q_PROPERTY(int count READ count NOTIFY rowsChanged)
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY searchChanged)
    Q_PROPERTY(bool synced READ synced NOTIFY syncedChanged)
    Q_PROPERTY(bool offline READ offline NOTIFY offlineChanged)
    Q_PROPERTY(QVariantMap current READ current NOTIFY currentChanged)
    Q_PROPERTY(bool shown READ shown NOTIFY shownChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool queued READ queued NOTIFY stateChanged)
    Q_PROPERTY(bool installing READ installing NOTIFY stateChanged)
    Q_PROPERTY(int percent READ percent NOTIFY stateChanged)
    Q_PROPERTY(QString action READ action NOTIFY stateChanged)
    Q_PROPERTY(QString kind READ kind NOTIFY stateChanged)
    Q_PROPERTY(int padTurn READ padTurn NOTIFY padTurnChanged)
    Q_PROPERTY(QString clock READ clock NOTIFY clockChanged)
    Q_PROPERTY(bool batteryPresent READ batteryPresent NOTIFY batteryChanged)
    Q_PROPERTY(int batteryPercent READ batteryPercent NOTIFY batteryChanged)
    Q_PROPERTY(bool wifi READ wifi NOTIFY wifiChanged)
    Q_PROPERTY(QString ssid READ ssid NOTIFY wifiChanged)

public:
    static constexpr int IdlePulseMs = 1000;

    StoreProcess(StoreWorker *worker, const QDBusConnection &session, QObject *parent = nullptr);
    ~StoreProcess() override;

    static QString service();
    static QString path();
    static QString interface();

    bool publish(QDBusConnection bus);
    void setWindow(QQuickWindow *window);
    void setPad(PadLink *pad);
    void setIdle(IdlePulse *idle);

    QVariantList rows() const;
    int count() const;
    QString search() const;
    bool synced() const;
    bool offline() const;
    QVariantMap current() const;
    bool shown() const;
    bool busy() const;
    bool queued() const;
    bool installing() const;
    int percent() const;
    QString action() const;
    QString kind() const;
    int padTurn() const;
    QString clock() const;
    bool batteryPresent() const;
    int batteryPercent() const;
    bool wifi() const;
    QString ssid() const;

    void setSearch(const QString &search);
    void setPadTurn(int turn);
    void setClock(const QString &clock);
    void setBattery(bool present, int percent);
    void setWifi(bool connected, const QString &ssid);

    QString desktopId(const QString &id) const;
    bool installed() const;
    bool quitting() const;

    Q_INVOKABLE void show();
    Q_INVOKABLE void hide();
    Q_INVOKABLE void open(const QString &id);
    Q_INVOKABLE void back();
    Q_INVOKABLE void activate();
    Q_INVOKABLE void uninstall();
    Q_INVOKABLE void pulse();
    Q_INVOKABLE void quit();

public slots:
    Q_SCRIPTABLE void Show();

signals:
    void rowsChanged();
    void searchChanged();
    void syncedChanged();
    void offlineChanged();
    void currentChanged();
    void shownChanged();
    void stateChanged();
    void padTurnChanged();
    void clockChanged();
    void batteryChanged();
    void wifiChanged();
    void toast(const QString &title, const QString &message);
    void quitRequested();

protected:
    virtual bool startApp(const QString &desktopId);

private:
    QString nameOf(const QString &id) const;
    void reload();
    void refreshCurrent();
    void updateJob();
    void onJobFinished(StoreJob *job);
    void onAppFailed(const QString &appId, const QString &message);
    void launchFailed(const QString &name, const QString &message);
    void leaveWhenIdle();

    StoreWorker *m_worker;
    StoreCatalogModel *m_catalog;
    std::unique_ptr<AppModel> m_apps;
    std::unique_ptr<LauncherLaunch> m_launch;
    std::unique_ptr<ManagerEnv> m_manager;
    QQuickWindow *m_window = nullptr;
    PadLink *m_pad = nullptr;
    IdlePulse *m_idle = nullptr;
    QTimer m_idleCool;
    QPointer<StoreJob> m_job;
    QString m_id;
    QString m_launchName;
    QVariantMap m_current;
    bool m_shown = false;
    bool m_leaving = false;
    int m_padTurn = 0;
    QString m_clock;
    bool m_batteryPresent = false;
    int m_batteryPercent = 100;
    bool m_wifi = false;
    QString m_ssid;
};
