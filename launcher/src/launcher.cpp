#include "launcher.h"

#include "activator.h"
#include "appmodel.h"
#include "desktophome.h"
#include "favoritesmodel.h"
#include "idlepulse.h"
#include "launch.h"
#include "poweraction.h"
#include "sleepinhibitor.h"
#ifdef MICROSET_STORE_CORE
#include "storejob.h"
#include "storeworker.h"
#endif

#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDebug>

using namespace Qt::StringLiterals;

namespace {

constexpr int EndTileCount = 2;

QVariantMap endTile(const QString &name, const QString &end)
{
    return QVariantMap{
        {u"id"_s, QString()},
        {u"name"_s, name},
        {u"source"_s, QString()},
        {u"startupClass"_s, QString()},
        {u"flatpak"_s, QString()},
        {u"end"_s, end},
    };
}

}

Launcher::Launcher(DesktopHome *home, AppModel *apps, FavoritesModel *favorites, Activator *launch, PowerAction *power,
                   PadLink *pad, SleepInhibitor *sleep, int landscapeTurn, int reShowMs, QObject *parent)
    : QObject(parent)
    , m_home(home)
    , m_apps(apps)
    , m_favorites(favorites)
    , m_launch(launch)
    , m_powerRunner(power)
    , m_pad(pad)
    , m_sleep(sleep)
    , m_landscapeTurn(landscapeTurn)
    , m_reShowMs(reShowMs)
    , m_page(homePage())
{
    m_gridBase = gridBase();
    m_reShow.setSingleShot(true);
    m_reShow.setTimerType(Qt::PreciseTimer);
    m_reShow.setInterval(m_reShowMs);
    connect(&m_reShow, &QTimer::timeout, this, &Launcher::reShowNow);

    m_powerWait.setSingleShot(true);
    m_powerWait.setTimerType(Qt::PreciseTimer);
    m_powerWait.setInterval(PowerWaitMs);
    connect(&m_powerWait, &QTimer::timeout, this, [this] {
        qWarning("launcher: ShowPower gave up after %d ms with the home not shown", PowerWaitMs);
    });

    m_idleCool.setSingleShot(true);
    m_idleCool.setInterval(IdlePulseMs);

    connect(m_home, &DesktopHome::shownChanged, this, [this] {
        setShown(m_home->shown() && m_home->available());
        if (m_home->shown() && m_powerWait.isActive()) {
            m_powerWait.stop();
            setPower(true);
        }
        if (m_hiddenByUs && m_home->shown())
            reShowNow();
        else if (!m_hiddenByUs)
            setContent(m_home->shown() || !m_home->available());
    });
    connect(m_home, &DesktopHome::availableChanged, this, [this] {
        setShown(m_home->shown() && m_home->available());
        if (!m_hiddenByUs)
            setContent(m_home->shown() || !m_home->available());
        emit serviceDownChanged();
    });
    connect(m_home, &DesktopHome::windowsChanged, this, [this] {
        const int base = gridBase();
        if (base > m_gridBase)
            ++m_focusRow;
        else if (base < m_gridBase && m_focusRow >= m_gridBase)
            --m_focusRow;
        m_gridBase = base;
        clampFocus();
        emit windowsChanged();
    });
    connect(m_home, &DesktopHome::transformChanged, this,
            [this] { setPadTurn(turnFor(m_home->transform(), m_landscapeTurn)); });
    connect(m_home, &DesktopHome::failed, this, [this](const QString &method, const QString &message) {
        qWarning("launcher: %s failed: %s", qPrintable(method), qPrintable(message));
        if (m_hiddenByUs)
            reShowNow();
    });
    connect(m_apps, &AppModel::entriesChanged, this, [this] {
        clampFocus();
        emit appsChanged();
        emit allAppsChanged();
    });
    if (m_favorites != nullptr) {
        connect(m_favorites, &FavoritesModel::favoritesChanged, this, [this] {
            clampFocus();
            emit appsChanged();
            emit favoritesChanged();
        });
    }
    connect(m_launch, &Activator::started, this, [this](const QString &appId, qint64 pid) {
        qInfo("launcher: %s started as pid %lld", qPrintable(appId), static_cast<long long>(pid));
        m_launchName.clear();
        setLaunching(false);
    });
    connect(m_launch, &Activator::failed, this, [this](const QString &appId, const QString &message) {
        qWarning("launcher: %s did not start: %s", qPrintable(appId), qPrintable(message));
        const QString name = m_launchName.isEmpty() ? appId : m_launchName;
        const QString page = m_launchPage;
        m_launchName.clear();
        setLaunching(false);
        if (m_hiddenByUs) {
            reShowNow();
            if (!page.isEmpty())
                setPage(page);
        }
        emit launchFailed(name, message);
    });
    if (m_sleep != nullptr) {
        connect(m_sleep, &SleepInhibitor::prepareForSleep, this, [this](bool start) {
            if (!start) {
                syncArbiter();
                return;
            }
            if (m_pad == nullptr || !m_pad->wanted())
                return;
            m_sleep->defer();
            m_pad->setWanted(false);
        });
    }
    if (m_pad != nullptr) {
        connect(m_pad, &PadLink::releaseDone, this, [this] {
            if (m_sleep != nullptr && m_sleep->held())
                m_sleep->ready();
        });
    }
    setPadTurn(turnFor(m_home->transform(), m_landscapeTurn));
    setContent(m_home->shown() || !m_home->available());
    setShown(m_home->shown() && m_home->available());
    syncArbiter();
}

QString Launcher::service()
{
    return u"net.seilent.MicroSet.Launcher"_s;
}

QString Launcher::path()
{
    return u"/net/seilent/MicroSet/Launcher"_s;
}

QString Launcher::interface()
{
    return u"net.seilent.MicroSet.Launcher1"_s;
}

QString Launcher::storeAppId()
{
    return u"net.seilent.MicroSet.Store.desktop"_s;
}

QString Launcher::homePage()
{
    return u"home"_s;
}

QString Launcher::appsPage()
{
    return u"allapps"_s;
}

int Launcher::turnFor(uint transform, int landscapeTurn)
{
    const int turn = int(transform % 4);
    return ((landscapeTurn + turn) % 4 + 4) % 4;
}

bool Launcher::publish(QDBusConnection bus)
{
    if (!bus.registerObject(path(), this, QDBusConnection::ExportScriptableSlots))
        return false;
    return bus.interface()->registerService(service(), QDBusConnectionInterface::DontQueueService,
                                            QDBusConnectionInterface::DontAllowReplacement)
        == QDBusConnectionInterface::ServiceRegistered;
}

void Launcher::setIdle(IdlePulse *idle)
{
    m_idle = idle;
}

bool Launcher::shown() const
{
    return m_shown;
}

bool Launcher::content() const
{
    return m_content;
}

bool Launcher::power() const
{
    return m_power;
}

int Launcher::padTurn() const
{
    return m_padTurn;
}

bool Launcher::serviceDown() const
{
    return !m_home->available();
}

bool Launcher::launching() const
{
    return m_launching;
}

QVariantList Launcher::windows() const
{
    return m_home->windows();
}

QVariantList Launcher::apps() const
{
    QVariantList grid = m_favorites != nullptr ? m_favorites->favorites() : QVariantList();
    grid << endTile(u"All apps"_s, u"allapps"_s) << endTile(u"Store"_s, u"store"_s);
    return grid;
}

QVariantList Launcher::allApps() const
{
    return m_apps->entries();
}

QStringList Launcher::favoriteIds() const
{
    return m_favorites != nullptr ? m_favorites->ids() : QStringList();
}

int Launcher::favoriteCount() const
{
    return m_favorites != nullptr ? int(m_favorites->favorites().size()) : 0;
}

QString Launcher::page() const
{
    return m_page;
}

int Launcher::columns() const
{
    return m_columns;
}

int Launcher::focusRow() const
{
    return m_focusRow;
}

int Launcher::focusIndex() const
{
    return m_focusIndex;
}

QString Launcher::clock() const
{
    return m_clock;
}

bool Launcher::batteryPresent() const
{
    return m_batteryPresent;
}

int Launcher::batteryPercent() const
{
    return m_batteryPercent;
}

bool Launcher::wifi() const
{
    return m_wifi;
}

QString Launcher::ssid() const
{
    return m_ssid;
}

QString Launcher::windowIcon(int index)
{
    const QVariantList rows = m_home->windows();
    if (index < 0 || index >= rows.size())
        return {};
    const QString id = m_apps->classOf(rows.at(index).toMap().value(u"class"_s).toString());
    return id.isEmpty() ? QString() : AppModel::iconSource(id);
}

QString Launcher::windowName(int index)
{
    const QVariantList rows = m_home->windows();
    if (index < 0 || index >= rows.size())
        return {};
    const QVariantMap window = rows.at(index).toMap();
    const QString id = m_apps->classOf(window.value(u"class"_s).toString());
    const QString name = m_apps->nameOf(id);
    return name.isEmpty() ? window.value(u"title"_s).toString() : name;
}

void Launcher::setColumns(int columns)
{
    const int clamped = qMax(1, columns);
    if (clamped == m_columns)
        return;
    m_columns = clamped;
    clampFocus();
    emit columnsChanged();
}

void Launcher::setClock(const QString &clock)
{
    if (clock == m_clock)
        return;
    m_clock = clock;
    emit clockChanged();
}

void Launcher::setBattery(bool present, int percent)
{
    const int clamped = qBound(0, percent, 100);
    if (present == m_batteryPresent && clamped == m_batteryPercent)
        return;
    m_batteryPresent = present;
    m_batteryPercent = clamped;
    emit batteryChanged();
}

void Launcher::setWifi(bool connected, const QString &ssid)
{
    if (connected == m_wifi && ssid == m_ssid)
        return;
    m_wifi = connected;
    m_ssid = ssid;
    emit wifiChanged();
}

void Launcher::setShown(bool shown)
{
    if (shown == m_shown)
        return;
    m_shown = shown;
    if (!shown) {
        m_powerWait.stop();
        setPower(false);
    }
    syncArbiter();
    emit shownChanged();
}

void Launcher::setContent(bool content)
{
    if (content == m_content)
        return;
    m_content = content;
    emit contentChanged();
}

void Launcher::setPower(bool power)
{
    if (power == m_power)
        return;
    m_power = power;
    emit powerChanged();
    emit focusChanged();
}

void Launcher::setLaunching(bool launching)
{
    if (launching == m_launching)
        return;
    m_launching = launching;
    emit launchingChanged();
}

void Launcher::setPadTurn(int turn)
{
    qInfo("launcher: pad turn %d for transform %u", turn, m_home->transform());
    if (turn == m_padTurn)
        return;
    m_padTurn = turn;
    emit padTurnChanged();
}

bool Launcher::arbiterWanted() const
{
    return m_shown && m_home->available();
}

void Launcher::syncArbiter()
{
    if (m_pad == nullptr)
        return;
    const bool wanted = arbiterWanted();
    if (m_pad->wanted() == wanted)
        return;
    qInfo("launcher: %s the pad arbiter", wanted ? "taking" : "giving back");
    m_pad->setWanted(wanted);
}

void Launcher::hideForReShow()
{
    m_hiddenByUs = true;
    setPage(homePage());
    setContent(false);
    m_reShow.start(m_reShowMs);
}

void Launcher::reShowNow()
{
    m_reShow.stop();
    m_hiddenByUs = false;
    setLaunching(false);
    setContent(m_home->shown() || !m_home->available());
}

void Launcher::Show()
{
    m_home->enter();
}

void Launcher::Toggle()
{
    m_home->toggle();
}

void Launcher::ShowPower()
{
    m_powerWait.start();
    m_home->enter();
    if (m_home->shown()) {
        m_powerWait.stop();
        setPower(true);
    }
}

void Launcher::closePower()
{
    setPower(false);
}

void Launcher::focusWindowAt(int index)
{
    const QVariantList rows = m_home->windows();
    if (index < 0 || index >= rows.size())
        return;
    const QString address = rows.at(index).toMap().value(u"address"_s).toString();
    if (address.isEmpty())
        return;
    hideForReShow();
    m_home->focusWindow(address);
}

void Launcher::focusGridAt(int index)
{
    if (m_power || index < 0 || index >= gridCount())
        return;

    const int row = gridBase() + index / m_columns;
    const int col = index % m_columns;
    if (row == m_focusRow && col == m_focusIndex)
        return;
    m_focusRow = row;
    m_focusIndex = col;
    emit focusChanged();
}

void Launcher::focusRunningAt(int index)
{
    if (m_power || index < 0 || index >= m_home->windows().size())
        return;

    m_focusRow = 0;
    m_focusIndex = index;
    emit focusChanged();
}

void Launcher::leave()
{
    hideForReShow();
    m_home->leave();
}

void Launcher::pulse()
{
    if (m_idle == nullptr || m_idleCool.isActive())
        return;
    m_idleCool.start();
    m_idle->pulse();
}

void Launcher::runPower(const QString &verb)
{
    setPower(false);
    if (m_powerRunner != nullptr)
        m_powerRunner->run(verb);
}

int Launcher::gridBase() const
{
    return m_home->windows().isEmpty() ? 0 : 1;
}

int Launcher::gridCount() const
{
    return favoriteCount() + EndTileCount;
}

int Launcher::gridRows() const
{
    return (gridCount() + m_columns - 1) / m_columns;
}

int Launcher::rowCount() const
{
    return gridBase() + gridRows();
}

int Launcher::countOf(int row) const
{
    const int base = gridBase();
    if (row < base)
        return int(m_home->windows().size());
    return qBound(0, gridCount() - (row - base) * m_columns, m_columns);
}

QString Launcher::focusedFavorite() const
{
    if (m_focusRow < gridBase())
        return {};
    const QVariantList list = m_favorites != nullptr ? m_favorites->favorites() : QVariantList();
    const int index = (m_focusRow - gridBase()) * m_columns + m_focusIndex;
    if (index < 0 || index >= list.size())
        return {};
    return list.at(index).toMap().value(u"id"_s).toString();
}

void Launcher::clampFocus()
{
    const int rows = rowCount();
    const int row = rows <= 0 ? 0 : qBound(0, m_focusRow, rows - 1);
    const int index = qBound(0, m_focusIndex, qMax(0, countOf(row) - 1));
    if (row == m_focusRow && index == m_focusIndex)
        return;
    m_focusRow = row;
    m_focusIndex = index;
    emit focusChanged();
}

void Launcher::activateFocused()
{
    if (m_power || m_launching)
        return;
    if (m_focusRow < gridBase()) {
        focusWindowAt(m_focusIndex);
        return;
    }
    activateGridAt((m_focusRow - gridBase()) * m_columns + m_focusIndex);
}

void Launcher::activateGridAt(int index)
{
    if (index < 0)
        return;
    const QVariantList list = m_favorites != nullptr ? m_favorites->favorites() : QVariantList();
    if (index < list.size()) {
        const QVariantMap tile = list.at(index).toMap();
        launch(tile.value(u"id"_s).toString(), tile.value(u"name"_s).toString());
        return;
    }
    if (index == list.size()) {
        openAllApps();
        return;
    }
    if (index == list.size() + 1)
        openStore();
}

void Launcher::launchApp(const QString &id)
{
    launch(id, m_apps->nameOf(id));
}

void Launcher::launch(const QString &id, const QString &name)
{
    if (id.isEmpty() || m_launching)
        return;
    m_launchPage = m_page;
    m_launchName = name;
    hideForReShow();
    setLaunching(true);
    m_launch->start(id);
}

void Launcher::openAllApps()
{
    if (m_launching)
        return;
    setPage(appsPage());
}

void Launcher::openStore()
{
    launch(storeAppId(), u"Store"_s);
}

void Launcher::closePage()
{
    setPage(homePage());
}

void Launcher::toggleFavorite(const QString &id)
{
    if (m_favorites != nullptr)
        m_favorites->toggle(id);
}

void Launcher::removeFavorite(const QString &id)
{
    if (m_favorites != nullptr)
        m_favorites->unstar(id);
}

void Launcher::uninstallApp(const QString &id)
{
    const AppEntry *entry = m_apps->find(id);
    const QString appid = entry != nullptr ? entry->flatpak : QString();
    if (appid.isEmpty()) {
        qWarning("launcher: %s has no X-Flatpak, not uninstalling", qPrintable(id));
        return;
    }
    const QString name = entry->name;
    m_uninstallId = id;
    m_uninstallApp = appid;
    m_uninstallName = name;
    if (startUninstall(appid))
        return;
    m_uninstallId.clear();
    m_uninstallApp.clear();
    m_uninstallName.clear();
    qWarning("launcher: no store backend to uninstall %s", qPrintable(appid));
    emit uninstallFailed(name, u"uninstall failed"_s);
}

bool Launcher::startUninstall(const QString &appid)
{
#ifdef MICROSET_STORE_CORE
    if (m_store == nullptr)
        return false;
    m_store->uninstall(appid);
    return true;
#else
    Q_UNUSED(appid);
    return false;
#endif
}

#ifdef MICROSET_STORE_CORE
void Launcher::setStore(StoreWorker *store)
{
    if (store == m_store)
        return;
    if (m_store != nullptr)
        disconnect(m_store, nullptr, this, nullptr);
    m_store = store;
    if (m_store != nullptr)
        connect(m_store, &StoreWorker::jobFinished, this, &Launcher::storeJobFinished);
}

void Launcher::storeJobFinished(StoreJob *job)
{
    if (job == nullptr || job->kind() != StoreJob::Uninstall || job->id() != m_uninstallApp)
        return;
    const QString id = m_uninstallId;
    const QString name = m_uninstallName;
    m_uninstallId.clear();
    m_uninstallApp.clear();
    m_uninstallName.clear();
    if (job->state() == StoreJob::Done) {
        if (m_favorites != nullptr)
            m_favorites->unstar(id);
        return;
    }
    if (job->state() == StoreJob::Failed)
        emit uninstallFailed(name, job->message());
}
#endif

void Launcher::setPage(const QString &page)
{
    if (page == m_page)
        return;
    m_page = page;
    emit pageChanged();
}

bool Launcher::handleAction(const QString &name, bool repeat)
{
    if (name == u"up"_s || name == u"down"_s || name == u"left"_s || name == u"right"_s) {
        if (repeat)
            return true;
        const int rowDelta = name == u"up"_s ? -1 : name == u"down"_s ? 1 : 0;
        const int indexDelta = name == u"left"_s ? -1 : name == u"right"_s ? 1 : 0;
        const int rows = rowCount();
        if (rows <= 0)
            return true;
        if (rowDelta != 0) {
            const int target = qBound(0, m_focusRow + rowDelta, rows - 1);
            const int index = qBound(0, m_focusIndex, qMax(0, countOf(target) - 1));
            if (target != m_focusRow || index != m_focusIndex) {
                m_focusRow = target;
                m_focusIndex = index;
                emit focusChanged();
            }
            return true;
        }
        const int count = countOf(m_focusRow);
        const int target = qBound(0, m_focusIndex + indexDelta, qMax(0, count - 1));
        if (target != m_focusIndex) {
            m_focusIndex = target;
            emit focusChanged();
        }
        return true;
    }
    if (name == u"a"_s) {
        if (!repeat)
            activateFocused();
        return true;
    }
    if (name == u"b"_s) {
        if (!repeat)
            leave();
        return true;
    }
    if (name == u"x"_s) {
        if (!repeat && !focusedFavorite().isEmpty())
            emit sheetRequested();
        return true;
    }
    if (name == u"y"_s) {
        if (!repeat)
            openAllApps();
        return true;
    }
    return false;
}
