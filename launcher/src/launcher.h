#pragma once

#include "homeclient.h"
#include "padlink.h"

#include <QDBusConnection>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

class Activator;
class AppModel;
class FavoritesModel;
class IdlePulse;
class PowerAction;
class SleepInhibitor;
class StoreJob;
class StoreWorker;

class Launcher : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "net.seilent.MicroSet.Launcher1")
    Q_PROPERTY(bool shown READ shown NOTIFY shownChanged)
    Q_PROPERTY(bool content READ content NOTIFY contentChanged)
    Q_PROPERTY(bool power READ power NOTIFY powerChanged)
    Q_PROPERTY(int padTurn READ padTurn NOTIFY padTurnChanged)
    Q_PROPERTY(bool serviceDown READ serviceDown NOTIFY serviceDownChanged)
    Q_PROPERTY(bool launching READ launching NOTIFY launchingChanged)
    Q_PROPERTY(bool store READ store NOTIFY storeChanged)
    Q_PROPERTY(QVariantList windows READ windows NOTIFY windowsChanged)
    Q_PROPERTY(QVariantList apps READ apps NOTIFY appsChanged)
    Q_PROPERTY(QVariantList allApps READ allApps NOTIFY allAppsChanged)
    Q_PROPERTY(QStringList favoriteIds READ favoriteIds NOTIFY favoritesChanged)
    Q_PROPERTY(int favoriteCount READ favoriteCount NOTIFY favoritesChanged)
    Q_PROPERTY(QString page READ page NOTIFY pageChanged)
    Q_PROPERTY(int columns READ columns WRITE setColumns NOTIFY columnsChanged)
    Q_PROPERTY(int focusRow READ focusRow NOTIFY focusChanged)
    Q_PROPERTY(int focusIndex READ focusIndex NOTIFY focusChanged)
    Q_PROPERTY(QString clock READ clock NOTIFY clockChanged)
    Q_PROPERTY(bool batteryPresent READ batteryPresent NOTIFY batteryChanged)
    Q_PROPERTY(int batteryPercent READ batteryPercent NOTIFY batteryChanged)
    Q_PROPERTY(bool wifi READ wifi NOTIFY wifiChanged)
    Q_PROPERTY(QString ssid READ ssid NOTIFY wifiChanged)

public:
    static constexpr int ReShowMs = 3000;
    static constexpr int PowerWaitMs = 1000;
    static constexpr int IdlePulseMs = 1000;

    Launcher(DesktopHome *home, AppModel *apps, FavoritesModel *favorites, Activator *launch, PowerAction *power,
             PadLink *pad, SleepInhibitor *sleep, int landscapeTurn, int reShowMs = ReShowMs,
             QObject *parent = nullptr);

    static QString service();
    static QString path();
    static QString interface();
    static QString storeAppId();
    static QString homePage();
    static QString appsPage();
    static int turnFor(uint transform, int landscapeTurn);

    bool publish(QDBusConnection bus);
    void setIdle(IdlePulse *idle);

    bool shown() const;
    bool content() const;
    bool power() const;
    int padTurn() const;
    bool serviceDown() const;
    bool launching() const;
    bool store() const;
    QVariantList windows() const;
    QVariantList apps() const;
    QVariantList allApps() const;
    QStringList favoriteIds() const;
    int favoriteCount() const;
    QString page() const;
    int columns() const;
    int focusRow() const;
    int focusIndex() const;
    QString clock() const;
    bool batteryPresent() const;
    int batteryPercent() const;
    bool wifi() const;
    QString ssid() const;

    void setColumns(int columns);
    void setClock(const QString &clock);
    void setBattery(bool present, int percent);
    void setWifi(bool connected, const QString &ssid);

    Q_INVOKABLE bool handleAction(const QString &name, bool repeat);
    bool arbiterWanted() const;

    Q_INVOKABLE QString windowIcon(int index);
    Q_INVOKABLE QString windowName(int index);
    Q_INVOKABLE void focusGridAt(int index);
    Q_INVOKABLE void focusRunningAt(int index);
    Q_INVOKABLE void activateFocused();
    Q_INVOKABLE void runPower(const QString &verb);
    Q_INVOKABLE void closePower();
    Q_INVOKABLE void leave();
    Q_INVOKABLE void pulse();
    Q_INVOKABLE void launchApp(const QString &id);
    Q_INVOKABLE void openAllApps();
    Q_INVOKABLE void openStore();
    Q_INVOKABLE void closePage();
    Q_INVOKABLE void toggleFavorite(const QString &id);
    Q_INVOKABLE void removeFavorite(const QString &id);
    Q_INVOKABLE void uninstallApp(const QString &id);
#ifdef MICROSET_STORE_CORE
    void setStore(StoreWorker *store);
#endif

public slots:
    Q_SCRIPTABLE void Show();
    Q_SCRIPTABLE void Toggle();
    Q_SCRIPTABLE void ShowPower();

signals:
    void shownChanged();
    void contentChanged();
    void powerChanged();
    void padTurnChanged();
    void serviceDownChanged();
    void launchingChanged();
    void storeChanged();
    void windowsChanged();
    void appsChanged();
    void allAppsChanged();
    void favoritesChanged();
    void pageChanged();
    void columnsChanged();
    void focusChanged();
    void clockChanged();
    void batteryChanged();
    void wifiChanged();
    void sheetRequested();
    void launchFailed(const QString &name, const QString &message);
    void uninstallFailed(const QString &name, const QString &message);

protected:
    virtual bool startUninstall(const QString &appid);

private:
    int gridBase() const;
    int gridCount() const;
    int gridRows() const;
    int rowCount() const;
    int countOf(int row) const;
    QString focusedFavorite() const;
    void setPage(const QString &page);
    void setShown(bool shown);
    void setContent(bool content);
    void setPower(bool power);
    void setLaunching(bool launching);
    void setPadTurn(int turn);
    void syncArbiter();
    void hideForReShow();
    void reShowNow();
    void clampFocus();
    void focusWindowAt(int index);
    void activateGridAt(int index);
    void launch(const QString &id, const QString &name);
#ifdef MICROSET_STORE_CORE
    void storeJobFinished(StoreJob *job);
#endif

    DesktopHome *m_home;
    AppModel *m_apps;
    FavoritesModel *m_favorites;
    Activator *m_launch;
    PowerAction *m_powerRunner;
    PadLink *m_pad;
    SleepInhibitor *m_sleep;
    IdlePulse *m_idle = nullptr;
    int m_landscapeTurn = 1;
    QTimer m_reShow;
    int m_reShowMs = ReShowMs;
    QTimer m_powerWait;
    QTimer m_idleCool;
    bool m_shown = false;
    bool m_content = true;
    bool m_power = false;
    bool m_launching = false;
    bool m_hiddenByUs = false;
    int m_padTurn = 0;
    int m_columns = 6;
    int m_gridBase = 0;
    int m_focusRow = 0;
    int m_focusIndex = 0;
    QString m_page;
    QString m_launchPage;
    QString m_launchName;
    QString m_uninstallId;
    QString m_uninstallApp;
    QString m_uninstallName;
    QString m_clock;
    bool m_batteryPresent = false;
    int m_batteryPercent = 100;
    bool m_wifi = false;
    QString m_ssid;
#ifdef MICROSET_STORE_CORE
    StoreWorker *m_store = nullptr;
#endif
};
