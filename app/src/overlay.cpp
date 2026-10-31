#include "overlay.h"

#include "dbuspad.h"
#include "sleepinhibitor.h"

#include <QDebug>
#include <QtGlobal>

#include <utility>

using namespace Qt::StringLiterals;

namespace {

QString menuNote(const SessionMenuEntry &entry, const QString &current, bool inGame)
{
    if (entry.state == u"not-installed"_s)
        return entry.install ? u"not installed, a to install"_s : u"not installed"_s;
    if (entry.state == u"refused"_s)
        return u"not reviewed"_s;
    if (inGame && entry.name != current)
        return u"closes the game"_s;
    return {};
}

}

Overlay::Overlay(DbusPad *pad, SleepInhibitor *sleep, QObject *parent)
    : QObject(parent)
    , m_pad(pad)
    , m_sleep(sleep)
    , m_prepare([](QWindow *window) { window->create(); })
    , m_prepareToast([](QWindow *window) { window->create(); })
    , m_lower([](QWindow *window) { window->lower(); })
{
    m_fade.setStartValue(0.0);
    m_fade.setEndValue(0.0);
    connect(&m_fade, &QVariantAnimation::valueChanged, this,
            [this](const QVariant &value) { setOpacity(value.toReal()); });
    connect(&m_fade, &QVariantAnimation::finished, this, &Overlay::onFadeFinished);
    m_release.setSingleShot(true);
    m_release.setInterval(ReleaseFrameMs);
    connect(&m_release, &QTimer::timeout, this, [this] {
        if (m_phase != Phase::Releasing)
            return;
        step("no frame");
        finishRelease();
    });
    m_menuSwitch.setSingleShot(true);
    m_menuSwitch.setInterval(MenuSwitchMs);
    connect(&m_menuSwitch, &QTimer::timeout, this, [this] {
        qInfo("microset overlay: session menu: switch still running");
        hide();
    });
    m_menuToast.setSingleShot(true);
    m_menuToast.setInterval(MenuToastMs);
    connect(&m_menuToast, &QTimer::timeout, this, &Overlay::hide);
    connect(m_pad, &DbusPad::holderChanged, this, &Overlay::onHolder);
    connect(m_pad, &DbusPad::acquired, this, &Overlay::onAcquired);
    connect(m_pad, &DbusPad::arbiterLost, this, &Overlay::onArbiterLost);
    connect(m_pad, &DbusPad::releaseDone, this, &Overlay::onReleaseDone);
    connect(m_pad, &DbusPad::pressed, this, &Overlay::onPadPressed);
    connect(m_pad, &DbusPad::released, this, &Overlay::onPadReleased);
    connect(m_pad, &DbusPad::holdingChanged, this, &Overlay::clearPad);
    connect(this, &Overlay::mappedChanged, this, &Overlay::syncHint);
    connect(this, &Overlay::mappedChanged, this, &Overlay::checkUnused);
    connect(this, &Overlay::hintShownChanged, this, &Overlay::checkUnused);
    if (m_sleep)
        connect(m_sleep, &SleepInhibitor::prepareForSleep, this, &Overlay::onPrepareForSleep);
    m_clock.start();
}

void Overlay::setUi(std::function<void()> ensure)
{
    m_ensure = std::move(ensure);
}

void Overlay::setWindow(QWindow *window)
{
    if (m_window && m_frames)
        disconnect(m_window.data(), SIGNAL(frameSwapped()), this, SLOT(onFrameSwapped()));
    m_window = window;
    m_frames = m_window && m_window->metaObject()->indexOfSignal("frameSwapped()") >= 0;
    if (m_frames)
        connect(m_window.data(), SIGNAL(frameSwapped()), this, SLOT(onFrameSwapped()));
}

void Overlay::setToastWindow(QWindow *window)
{
    m_toast = window;
}

void Overlay::setHooks(WindowHook prepare, WindowHook prepareToast, WindowHook lower)
{
    m_prepare = std::move(prepare);
    m_prepareToast = std::move(prepareToast);
    m_lower = std::move(lower);
}

void Overlay::setSessionMenu(SessionMenu *menu)
{
    if (m_menu)
        disconnect(m_menu, nullptr, this, nullptr);
    m_menu = menu;
    if (!m_menu)
        return;
    connect(m_menu, &SessionMenu::switchReplied, this, &Overlay::onSwitchReplied);
    connect(m_menu, &SessionMenu::switchJobRemoved, this, &Overlay::onSwitchJobRemoved);
    connect(m_menu, &SessionMenu::installReplied, this, &Overlay::onInstallReplied);
}

void Overlay::setMenuTimeouts(int switchMs, int toastMs)
{
    m_menuSwitch.setInterval(switchMs);
    m_menuToast.setInterval(toastMs);
}

bool Overlay::shown() const
{
    return m_shown;
}

bool Overlay::mapped() const
{
    return m_mapped;
}

bool Overlay::buried() const
{
    const QString holder = m_pad->holder();
    return m_shown && m_inStack && !m_acquiring && !holder.isEmpty() && holder != m_pad->name();
}

qreal Overlay::opacity() const
{
    return m_opacity;
}

bool Overlay::hintShown() const
{
    return m_hintShown;
}

bool Overlay::hint() const
{
    return m_hint;
}

QString Overlay::hintDevice() const
{
    return m_hintDevice;
}

QString Overlay::hintKind() const
{
    return m_hintKind;
}

bool Overlay::menuOpen() const
{
    return m_menuOpen;
}

bool Overlay::menuLeaving() const
{
    return m_menuLeaving;
}

bool Overlay::menuOnly() const
{
    return m_menuOnly;
}

bool Overlay::menuBusy() const
{
    return m_menuCall != MenuCall::None;
}

QStringList Overlay::menuItems() const
{
    return m_menuItems;
}

int Overlay::menuCurrent() const
{
    return m_menuCurrent;
}

QStringList Overlay::menuNotes() const
{
    return m_menuNotes;
}

int Overlay::created() const
{
    return m_created;
}

int Overlay::destroyed() const
{
    return m_destroyed;
}

QWindow *Overlay::window() const
{
    return m_window;
}

QWindow *Overlay::toastWindow() const
{
    return m_toast;
}

void Overlay::ensureUi()
{
    if (m_ensure)
        m_ensure();
}

void Overlay::show()
{
    if (m_shown) {
        if (buried())
            raise();
        return;
    }
    setMenuOnly(false);
    qInfo() << "microset overlay: show requested";
    reveal();
}

void Overlay::reveal()
{
    m_clock.start();
    ensureUi();
    m_release.stop();
    m_releaseArmed = false;
    m_phase = Phase::Showing;
    setShown(true);
    mapWindow();
    m_acquiring = true;
    if (m_pad->wanted())
        m_pad->acquire();
    else
        m_pad->setWanted(true);
    fadeTo(1.0, ShowMs, QEasingCurve::Linear);
}

void Overlay::hide()
{
    if (!m_shown)
        return;
    resetMenu();
    m_clock.start();
    qInfo() << "microset overlay: hide requested";
    setShown(false);
    m_acquiring = false;
    release();
    if (!m_window || !m_window->handle()) {
        m_fade.stop();
        setOpacity(0.0);
        m_phase = Phase::Hidden;
        setMapped(false);
        return;
    }
    m_phase = Phase::Hiding;
    fadeTo(0.0, HideMs, QEasingCurve::OutExpo);
}

void Overlay::toggle()
{
    if (!m_shown)
        show();
    else if (buried())
        raise();
    else
        hide();
}

void Overlay::showMenu(const QList<SessionMenuEntry> &entries, const QString &current, bool inGame, bool switching)
{
    if (switching) {
        if (m_menuLeaving) {
            qInfo() << "microset overlay: session menu: switch running";
            hide();
            return;
        }
        qInfo() << "microset overlay: session menu: switch already running";
        return;
    }
    if (m_menuCall != MenuCall::None) {
        qInfo() << "microset overlay: session menu: call running";
        return;
    }
    if (m_menuOpen) {
        if (buried()) {
            raise();
            return;
        }
        qInfo() << "microset overlay: session menu: closed";
        closeMenu();
        if (m_menuOnly)
            hide();
        return;
    }
    qInfo() << "microset overlay: session menu requested";
    m_menuEntries = entries;
    m_menuItems.clear();
    m_menuNotes.clear();
    m_menuCurrent = -1;
    for (qsizetype i = 0; i < entries.size(); ++i) {
        const SessionMenuEntry &entry = entries.at(i);
        m_menuItems << entry.label;
        m_menuNotes << menuNote(entry, current, inGame);
        if (!current.isEmpty() && entry.name == current)
            m_menuCurrent = int(i);
    }
    emit menuChanged();
    if (!m_shown) {
        if (m_window && m_window->handle()) {
            reveal();
        } else {
            setMenuOnly(true);
            reveal();
        }
    } else if (buried()) {
        raise();
    }
    openMenu();
}

void Overlay::menuAccept(int index)
{
    if (!m_menu || !m_menuOpen || m_menuCall != MenuCall::None)
        return;
    if (index < 0 || index >= m_menuEntries.size())
        return;
    if (index == m_menuCurrent) {
        menuClose();
        return;
    }
    const SessionMenuEntry entry = m_menuEntries.at(index);
    if (entry.state == u"not-installed"_s && entry.install) {
        setMenuCall(MenuCall::Install);
        m_menu->startInstall(entry.name);
        return;
    }
    if (entry.state != u"available"_s)
        return;
    setMenuCall(MenuCall::Switch);
    m_menu->startSwitch(entry.name);
}

void Overlay::setMenuCall(MenuCall call)
{
    if (m_menuCall == call)
        return;
    m_menuCall = call;
    emit menuCallChanged();
}

void Overlay::menuClose()
{
    if (!m_menuOpen || m_menuCall != MenuCall::None)
        return;
    closeMenu();
    armMenu(m_downB ? DbusPad::B : DbusPad::A);
}

void Overlay::openMenu()
{
    m_menuOpen = true;
    m_menuLeaving = false;
    m_menuArm = -1;
    m_menuSwitch.stop();
    m_menuToast.stop();
    emit menuStateChanged();
}

void Overlay::closeMenu()
{
    if (!m_menuOpen)
        return;
    m_menuOpen = false;
    emit menuStateChanged();
}

void Overlay::leaveMenu()
{
    m_menuOpen = false;
    m_menuLeaving = true;
    m_menuSwitch.start();
    emit menuStateChanged();
}

void Overlay::resetMenu()
{
    m_menuSwitch.stop();
    m_menuToast.stop();
    setMenuCall(MenuCall::None);
    m_menuArm = -1;
    if (m_menuOpen || m_menuLeaving) {
        m_menuOpen = false;
        m_menuLeaving = false;
        emit menuStateChanged();
    }
}

void Overlay::setMenuOnly(bool only)
{
    if (m_menuOnly == only)
        return;
    m_menuOnly = only;
    emit menuStateChanged();
}

void Overlay::armMenu(DbusPad::Action action)
{
    if (!m_menuOnly)
        return;
    armRelease(action);
}

void Overlay::armRelease(DbusPad::Action action)
{
    const bool down = action == DbusPad::A ? m_downA : m_downB;
    if (down) {
        m_menuArm = action;
        return;
    }
    hide();
}

void Overlay::onSwitchReplied(bool ok)
{
    if (m_menuCall != MenuCall::Switch)
        return;
    setMenuCall(MenuCall::None);
    if (!ok) {
        emit menuToast(u"switch failed"_s);
        return;
    }
    leaveMenu();
}

void Overlay::onSwitchJobRemoved(const QString &result)
{
    if (!m_menuLeaving || result == u"done"_s)
        return;
    emit menuToast(u"switch failed"_s);
    m_menuSwitch.stop();
    m_menuToast.start();
}

void Overlay::onInstallReplied(bool ok)
{
    if (m_menuCall != MenuCall::Install)
        return;
    setMenuCall(MenuCall::None);
    if (!ok) {
        emit menuToast(u"install failed"_s);
        return;
    }
    closeMenu();
    armRelease(DbusPad::A);
}

void Overlay::onPadPressed(DbusPad::Action action)
{
    if (action != DbusPad::A && action != DbusPad::B)
        return;
    if (action == DbusPad::A)
        m_downA = true;
    else
        m_downB = true;
    if (m_menuLeaving && action == DbusPad::B)
        hide();
}

void Overlay::onPadReleased(DbusPad::Action action)
{
    if (action != DbusPad::A && action != DbusPad::B)
        return;
    if (action == DbusPad::A)
        m_downA = false;
    else
        m_downB = false;
    if (m_menuArm != int(action))
        return;
    m_menuArm = -1;
    hide();
}

void Overlay::clearPad()
{
    if (m_pad->holding())
        return;
    m_downA = false;
    m_downB = false;
    m_menuArm = -1;
}

void Overlay::raise()
{
    qInfo() << "microset overlay: back to the top";
    m_clock.start();
    m_acquiring = true;
    m_pad->acquire();
}

void Overlay::release()
{
    m_inStack = false;
    m_releasing = true;
    if (m_pad->wanted())
        m_pad->setWanted(false);
    else
        onReleaseDone();
}

void Overlay::onHolder()
{
    if (!m_shown)
        return;
    const QString holder = m_pad->holder();
    if (holder == m_pad->name()) {
        if (m_window && m_window->handle() && m_window->isVisible())
            return;
        m_fade.stop();
        setOpacity(1.0);
        m_phase = Phase::Shown;
        mapWindow();
        step("holder again");
        return;
    }
    if (!buried())
        return;
    m_fade.stop();
    m_release.stop();
    setOpacity(1.0);
    m_phase = Phase::Shown;
    destroyWindow();
    step("buried");
}

void Overlay::onAcquired(bool ok)
{
    if (!m_shown || !m_pad->wanted())
        return;
    m_acquiring = false;
    if (!ok) {
        qWarning() << "microset overlay: intercept failed";
        return;
    }
    m_inStack = true;
    onHolder();
}

void Overlay::onArbiterLost()
{
    m_inStack = false;
    if (!m_shown || (m_window && m_window->handle()))
        return;
    qInfo() << "microset overlay: arbiter gone while buried, hidden";
    setShown(false);
    m_acquiring = false;
    m_phase = Phase::Hidden;
    setOpacity(0.0);
    resetMenu();
    setMenuOnly(false);
    release();
}

void Overlay::onReleaseDone()
{
    m_releasing = false;
    if (!m_sleepWait)
        return;
    m_sleepWait = false;
    m_sleep->ready();
}

void Overlay::onPrepareForSleep(bool start)
{
    if (!start) {
        syncHint();
        return;
    }
    m_pageHinted = false;
    dropToast();
    resetMenu();
    const bool window = m_window && m_window->handle();
    if (!m_shown && !window && !m_releasing && !m_pad->wanted())
        return;
    m_sleep->defer();
    m_sleepWait = true;
    qInfo() << "microset overlay: hidden for sleep";
    setShown(false);
    m_acquiring = false;
    m_fade.stop();
    m_release.stop();
    setOpacity(0.0);
    m_phase = Phase::Hidden;
    destroyWindow();
    if (m_pad->wanted() || !m_releasing)
        release();
}

void Overlay::fadeTo(qreal to, int ms, QEasingCurve::Type curve)
{
    m_fade.stop();
    m_fade.setStartValue(m_opacity);
    m_fade.setEndValue(to);
    m_fade.setDuration(ms);
    m_fade.setEasingCurve(curve);
    m_fade.start();
}

void Overlay::onFadeFinished()
{
    setOpacity(m_fade.endValue().toReal());
    step("fade done");
    if (m_shown) {
        m_phase = Phase::Shown;
        return;
    }
    if (m_opacity > 0.0)
        return;
    beginRelease();
}

void Overlay::onFrameSwapped()
{
    if (m_firstFrame) {
        m_firstFrame = false;
        step("first frame");
    }
    if (m_phase != Phase::Releasing || !m_releaseArmed)
        return;
    m_releaseArmed = false;
    const quint64 generation = m_releaseGeneration;
    QMetaObject::invokeMethod(this, [this, generation] {
        if (m_phase != Phase::Releasing || generation != m_releaseGeneration)
            return;
        step("last frame");
        finishRelease();
    }, Qt::QueuedConnection);
}

void Overlay::beginRelease()
{
    if (!m_window || !m_window->handle() || !m_window->isVisible() || !m_frames) {
        finishRelease();
        return;
    }
    m_phase = Phase::Releasing;
    m_releaseArmed = false;
    const quint64 generation = ++m_releaseGeneration;
    QMetaObject::invokeMethod(this, [this, generation] {
        if (m_phase == Phase::Releasing && generation == m_releaseGeneration)
            m_releaseArmed = true;
    }, Qt::QueuedConnection);
    m_release.start();
    QMetaObject::invokeMethod(m_window.data(), "update", Qt::DirectConnection);
}

void Overlay::finishRelease()
{
    m_release.stop();
    m_releaseArmed = false;
    m_phase = Phase::Hidden;
    destroyWindow();
}

void Overlay::mapWindow()
{
    if (!m_window)
        return;
    if (!m_window->handle()) {
        m_prepare(m_window);
        ++m_created;
        step("created");
    }
    if (m_window->isVisible())
        return;
    m_firstFrame = m_frames;
    m_window->setVisible(true);
    m_lower(m_window);
    setMapped(true);
    step("mapped");
}

void Overlay::destroyWindow()
{
    if (!m_window || !m_window->handle()) {
        setMapped(false);
        return;
    }
    m_window->setVisible(false);
    m_window->destroy();
    ++m_destroyed;
    setMapped(false);
    step("destroyed");
}

void Overlay::setHint(bool active, const QString &device, const QString &kind)
{
    if (active == m_hint && device == m_hintDevice && kind == m_hintKind)
        return;
    m_hint = active;
    m_hintDevice = device;
    m_hintKind = kind;
    if (!active)
        m_pageHinted = false;
    emit hintChanged();
    syncHint();
}

void Overlay::syncHint()
{
    if (!m_hint || m_sleepWait)
        return;
    if (m_mapped) {
        dropToast();
        if (!m_pageHinted) {
            m_pageHinted = true;
            emit pageHint();
        }
        return;
    }
    m_pageHinted = false;
    if (m_hintShown)
        return;
    ensureUi();
    if (!m_toast)
        return;
    if (!m_toast->handle())
        m_prepareToast(m_toast);
    m_toast->setVisible(true);
    m_lower(m_toast);
    setHintShown(true);
}

void Overlay::checkUnused()
{
    QTimer::singleShot(0, this, [this] {
        if (m_shown || m_mapped || m_hintShown || (!m_window && !m_toast))
            return;
        step("ui unused");
        emit unused();
    });
}

void Overlay::hintDone()
{
    dropToast();
}

void Overlay::dropToast()
{
    if (m_toast && m_toast->handle()) {
        m_toast->setVisible(false);
        m_toast->destroy();
    }
    setHintShown(false);
}

void Overlay::setShown(bool shown)
{
    if (m_shown == shown)
        return;
    m_shown = shown;
    emit shownChanged();
}

void Overlay::setMapped(bool mapped)
{
    if (m_mapped == mapped)
        return;
    m_mapped = mapped;
    emit mappedChanged();
    if (!mapped && !m_shown)
        setMenuOnly(false);
}

void Overlay::setOpacity(qreal value)
{
    if (qFuzzyCompare(m_opacity + 1.0, value + 1.0))
        return;
    m_opacity = value;
    emit opacityChanged();
}

void Overlay::setHintShown(bool shown)
{
    if (m_hintShown == shown)
        return;
    m_hintShown = shown;
    emit hintShownChanged();
}

void Overlay::step(const char *name) const
{
    qInfo().nospace() << "microset overlay: " << name << " after " << m_clock.elapsed() << " ms";
}
