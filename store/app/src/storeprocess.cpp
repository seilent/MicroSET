#include "storeprocess.h"

#include "appmodel.h"
#include "idlepulse.h"
#include "launch.h"
#include "storecatalogmodel.h"
#include "storejob.h"
#include "storeworker.h"

#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDebug>
#include <QDir>
#include <QQuickWindow>
#include <QSettings>

using namespace Qt::StringLiterals;

namespace {

constexpr const char *FlathubExport = "/flatpak/exports/share/applications";

}

StoreApplication::StoreApplication(StoreProcess *process)
    : QDBusAbstractAdaptor(process)
    , m_process(process)
{
}

void StoreApplication::Activate(const QVariantMap &platformData)
{
    Q_UNUSED(platformData)
    m_process->show();
}

void StoreApplication::Open(const QStringList &uris, const QVariantMap &platformData)
{
    Q_UNUSED(uris)
    Q_UNUSED(platformData)
    m_process->show();
}

void StoreApplication::ActivateAction(const QString &action, const QVariantList &parameter,
                                      const QVariantMap &platformData)
{
    Q_UNUSED(action)
    Q_UNUSED(parameter)
    Q_UNUSED(platformData)
}

StoreProcess::StoreProcess(StoreWorker *worker, const QDBusConnection &session, QObject *parent)
    : QObject(parent)
    , m_worker(worker)
    , m_catalog(new StoreCatalogModel(this))
    , m_apps(std::make_unique<AppModel>())
    , m_launch(std::make_unique<LauncherLaunch>(session, m_apps.get()))
    , m_manager(std::make_unique<ManagerEnv>(session))
{
    m_launch->setManager(m_manager.get());
    m_manager->watch();
    new StoreApplication(this);
    m_idleCool.setSingleShot(true);
    m_idleCool.setInterval(IdlePulseMs);
    connect(m_worker, &StoreWorker::catalogReady, this, [this](const QList<StoreEntry> &entries) {
        if (m_shown)
            m_catalog->setEntries(entries);
    });
    connect(m_worker, &StoreWorker::installedReady, this, [this](const QList<StoreEntry> &installed) {
        if (m_shown)
            m_catalog->setInstalled(installed);
    });
    connect(m_worker, &StoreWorker::syncFinished, this, [this](bool ok, const QString &) {
        if (m_shown)
            m_catalog->setSynced(ok);
    });
    connect(m_worker, &StoreWorker::jobChanged, this, &StoreProcess::updateJob);
    connect(m_worker, &StoreWorker::jobFinished, this, &StoreProcess::onJobFinished);
    connect(m_catalog, &StoreCatalogModel::rowsChanged, this, &StoreProcess::refreshCurrent);
    connect(m_catalog, &StoreCatalogModel::rowsChanged, this, &StoreProcess::rowsChanged);
    connect(m_catalog, &StoreCatalogModel::searchChanged, this, &StoreProcess::searchChanged);
    connect(m_catalog, &StoreCatalogModel::syncedChanged, this, &StoreProcess::syncedChanged);
    connect(m_catalog, &StoreCatalogModel::offlineChanged, this, &StoreProcess::offlineChanged);
    connect(m_launch.get(), &Activator::failed, this, &StoreProcess::onAppFailed);
    connect(m_launch.get(), &Activator::started, this, [](const QString &appId, qint64 pid) {
        qInfo("store: %s started as pid %lld", qPrintable(appId), static_cast<long long>(pid));
    });
}

StoreProcess::~StoreProcess()
{
    m_job = nullptr;
    m_catalog->release();
}

QString StoreProcess::service()
{
    return u"net.seilent.MicroSet.Store"_s;
}

QString StoreProcess::path()
{
    return u"/net/seilent/MicroSet/Store"_s;
}

QString StoreProcess::interface()
{
    return u"net.seilent.MicroSet.Store1"_s;
}

bool StoreProcess::publish(QDBusConnection bus)
{
    if (!bus.registerObject(path(), this,
                            QDBusConnection::ExportAdaptors | QDBusConnection::ExportScriptableSlots))
        return false;
    return bus.interface()->registerService(service(), QDBusConnectionInterface::DontQueueService,
                                            QDBusConnectionInterface::DontAllowReplacement)
        == QDBusConnectionInterface::ServiceRegistered;
}

void StoreProcess::setWindow(QQuickWindow *window)
{
    m_window = window;
}

void StoreProcess::setPad(PadLink *pad)
{
    m_pad = pad;
}

void StoreProcess::setIdle(IdlePulse *idle)
{
    m_idle = idle;
}

QVariantList StoreProcess::rows() const
{
    return m_catalog->rows();
}

int StoreProcess::count() const
{
    return m_catalog->count();
}

QString StoreProcess::search() const
{
    return m_catalog->search();
}

bool StoreProcess::synced() const
{
    return m_catalog->synced();
}

bool StoreProcess::offline() const
{
    return m_catalog->offline();
}

QVariantMap StoreProcess::current() const
{
    return m_current;
}

bool StoreProcess::shown() const
{
    return m_shown;
}

bool StoreProcess::busy() const
{
    return m_job != nullptr && !m_id.isEmpty() && m_job->id() == m_id && m_job->running();
}

bool StoreProcess::queued() const
{
    return busy() && m_job->state() == StoreJob::Queued;
}

bool StoreProcess::installing() const
{
    return busy() && (m_job->state() == StoreJob::Downloading || m_job->state() == StoreJob::Installing);
}

int StoreProcess::percent() const
{
    return installing() ? m_job->percent() : -1;
}

QString StoreProcess::action() const
{
    if (m_id.isEmpty())
        return {};
    if (busy())
        return u"cancel"_s;
    return installed() ? u"open"_s : u"install"_s;
}

QString StoreProcess::kind() const
{
    if (m_job == nullptr)
        return {};
    return m_job->kind() == StoreJob::Install ? u"install"_s : u"uninstall"_s;
}

int StoreProcess::padTurn() const
{
    return m_padTurn;
}

QString StoreProcess::clock() const
{
    return m_clock;
}

bool StoreProcess::batteryPresent() const
{
    return m_batteryPresent;
}

int StoreProcess::batteryPercent() const
{
    return m_batteryPercent;
}

bool StoreProcess::wifi() const
{
    return m_wifi;
}

QString StoreProcess::ssid() const
{
    return m_ssid;
}

void StoreProcess::setSearch(const QString &search)
{
    m_catalog->setSearch(search);
}

void StoreProcess::setPadTurn(int turn)
{
    if (turn == m_padTurn)
        return;
    m_padTurn = turn;
    emit padTurnChanged();
}

void StoreProcess::setClock(const QString &clock)
{
    if (clock == m_clock)
        return;
    m_clock = clock;
    emit clockChanged();
}

void StoreProcess::setBattery(bool present, int percent)
{
    const int clamped = qBound(0, percent, 100);
    if (present == m_batteryPresent && clamped == m_batteryPercent)
        return;
    m_batteryPresent = present;
    m_batteryPercent = clamped;
    emit batteryChanged();
}

void StoreProcess::setWifi(bool connected, const QString &ssid)
{
    if (connected == m_wifi && ssid == m_ssid)
        return;
    m_wifi = connected;
    m_ssid = ssid;
    emit wifiChanged();
}

QString StoreProcess::desktopId(const QString &id) const
{
    const QString direct = id + u".desktop"_s;
    if (id.isEmpty())
        return {};
    QString base = qEnvironmentVariable("XDG_DATA_HOME");
    if (base.isEmpty())
        base = QDir::homePath() + u"/.local/share"_s;
    const QDir dir(base + QLatin1String(FlathubExport));
    if (dir.exists(direct))
        return direct;
    const QStringList files = dir.entryList({u"*.desktop"_s}, QDir::Files);
    for (const QString &name : files) {
        const QSettings entry(dir.filePath(name), QSettings::IniFormat);
        if (entry.value(u"Desktop Entry/X-Flatpak"_s).toString() == id)
            return name;
    }
    return direct;
}

void StoreProcess::show()
{
    if (m_shown) {
        if (m_window != nullptr && !m_window->isVisible())
            m_window->show();
        return;
    }
    if (m_window != nullptr)
        m_window->show();
    if (m_pad != nullptr)
        m_pad->setWanted(true);
    m_shown = true;
    emit shownChanged();
    reload();
}

void StoreProcess::hide()
{
    if (m_window != nullptr)
        m_window->hide();
    if (m_pad != nullptr)
        m_pad->setWanted(false);
    const bool wasShown = m_shown;
    m_shown = false;
    m_id.clear();
    m_catalog->setSearch(QString());
    m_catalog->release();
    refreshCurrent();
    if (wasShown)
        emit shownChanged();
}

void StoreProcess::Show()
{
    show();
}

void StoreProcess::open(const QString &id)
{
    if (id.isEmpty() || id == m_id)
        return;
    m_id = id;
    refreshCurrent();
}

void StoreProcess::back()
{
    if (m_id.isEmpty())
        return;
    m_id.clear();
    refreshCurrent();
}

void StoreProcess::activate()
{
    if (m_id.isEmpty() || !m_shown)
        return;
    if (busy()) {
        m_worker->cancelFor(m_id);
        return;
    }
    if (m_worker->jobFor(m_id) != nullptr)
        return;
    if (installed()) {
        const QString appName = nameOf(m_id);
        const QString desktop = desktopId(m_id);
        m_launchName = appName;
        hide();
        if (!startApp(desktop))
            launchFailed(appName, u"open failed"_s);
        return;
    }
    m_worker->install(m_id);
}

void StoreProcess::uninstall()
{
    if (m_id.isEmpty() || !m_shown || m_worker->jobFor(m_id) != nullptr)
        return;
    m_worker->uninstall(m_id);
}

void StoreProcess::pulse()
{
    if (m_idle == nullptr || m_idleCool.isActive())
        return;
    m_idleCool.start();
    m_idle->pulse();
}

void StoreProcess::quit()
{
    m_leaving = true;
    hide();
    leaveWhenIdle();
}

void StoreProcess::leaveWhenIdle()
{
    if (!m_leaving)
        return;
    if (!m_worker->idle())
        return;
    m_leaving = false;
    emit quitRequested();
}

bool StoreProcess::quitting() const
{
    return m_leaving;
}

bool StoreProcess::startApp(const QString &desktopId)
{
    m_launch->start(desktopId);
    return true;
}

bool StoreProcess::installed() const
{
    return m_current.value(u"installed"_s).toBool();
}

QString StoreProcess::nameOf(const QString &id) const
{
    const QString name = m_catalog->row(id).value(u"name"_s).toString();
    return name.isEmpty() ? id : name;
}

void StoreProcess::reload()
{
    m_catalog->setSearch(QString());
    m_worker->load();
    m_worker->refresh();
}

void StoreProcess::refreshCurrent()
{
    QVariantMap map;
    if (!m_id.isEmpty())
        map = m_catalog->row(m_id);
    if (map != m_current) {
        m_current = map;
        emit currentChanged();
    }
    updateJob();
}

void StoreProcess::updateJob()
{
    StoreJob *job = m_worker->jobFor(m_id);
    if (job != m_job) {
        if (m_job != nullptr)
            disconnect(m_job, nullptr, this, nullptr);
        m_job = job;
        if (m_job != nullptr) {
            connect(m_job, &StoreJob::stateChanged, this, &StoreProcess::stateChanged);
            connect(m_job, &StoreJob::percentChanged, this, &StoreProcess::stateChanged);
        }
    }
    emit stateChanged();
}

void StoreProcess::onJobFinished(StoreJob *job)
{
    if (job == nullptr)
        return;
    const QString name = nameOf(job->id());
    const bool install = job->kind() == StoreJob::Install;
    if (job->state() == StoreJob::Done) {
        emit toast(name, install ? u"installed"_s : u"removed"_s);
    } else if (job->state() == StoreJob::Cancelled) {
        emit toast(name, u"cancelled"_s);
    } else if (job->state() == StoreJob::Failed) {
        if (job->message().isEmpty())
            emit toast(name, install ? u"install failed"_s : u"uninstall failed"_s);
        else
            emit toast(name, job->message());
    }
    refreshCurrent();
    leaveWhenIdle();
}

void StoreProcess::onAppFailed(const QString &appId, const QString &message)
{
    Q_UNUSED(appId)
    const QString name = m_launchName.isEmpty() ? nameOf(m_id) : m_launchName;
    launchFailed(name, message);
}

void StoreProcess::launchFailed(const QString &name, const QString &message)
{
    m_launchName.clear();
    show();
    emit toast(name, message);
}
