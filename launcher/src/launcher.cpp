#include "launcher.h"

#include "activator.h"
#include "appmodel.h"
#include "desktophome.h"
#include "idlepulse.h"
#include "launch.h"
#include "poweraction.h"
#include "sleepinhibitor.h"

#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDebug>

using namespace Qt::StringLiterals;

Launcher::Launcher(DesktopHome *home, AppModel *apps, Activator *launch, PowerAction *power, PadLink *pad,
                   SleepInhibitor *sleep, int landscapeTurn, int reShowMs, QObject *parent)
    : QObject(parent)
    , m_home(home)
    , m_apps(apps)
    , m_launch(launch)
    , m_powerRunner(power)
    , m_pad(pad)
    , m_sleep(sleep)
    , m_landscapeTurn(landscapeTurn)
    , m_reShowMs(reShowMs)
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
    });
    connect(m_launch, &Activator::started, this, [this](const QString &appId, qint64 pid) {
        qInfo("launcher: %s started as pid %lld", qPrintable(appId), static_cast<long long>(pid));
        setLaunching(false);
    });
    connect(m_launch, &Activator::failed, this, [this](const QString &appId, const QString &message) {
        qWarning("launcher: %s did not start: %s", qPrintable(appId), qPrintable(message));
        setLaunching(false);
        if (m_hiddenByUs)
            reShowNow();
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
    return m_apps->entries();
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
    if (m_power || index < 0 || index >= m_apps->count())
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

int Launcher::gridRows() const
{
    return (m_apps->count() + m_columns - 1) / m_columns;
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
    return qBound(0, m_apps->count() - (row - base) * m_columns, m_columns);
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
    launchAt((m_focusRow - gridBase()) * m_columns + m_focusIndex);
}

void Launcher::launchAt(int index)
{
    const QVariantList entries = m_apps->entries();
    if (index < 0 || index >= entries.size())
        return;
    const QString id = entries.at(index).toMap().value(u"id"_s).toString();
    if (id.isEmpty())
        return;
    hideForReShow();
    setLaunching(true);
    m_launch->start(id);
}

bool Launcher::handleAction(const QString &name, bool repeat)
{
    if (m_launching)
        return true;
    if (!m_content)
        return false;
    pulse();
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
    return false;
}
