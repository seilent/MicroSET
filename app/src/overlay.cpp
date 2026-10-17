#include "overlay.h"

#include "dbuspad.h"
#include "sleepinhibitor.h"

#include <QDebug>
#include <QtGlobal>

#include <utility>

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
    connect(m_pad, &DbusPad::holderChanged, this, &Overlay::onHolder);
    connect(m_pad, &DbusPad::acquired, this, &Overlay::onAcquired);
    connect(m_pad, &DbusPad::arbiterLost, this, &Overlay::onArbiterLost);
    connect(m_pad, &DbusPad::releaseDone, this, &Overlay::onReleaseDone);
    connect(this, &Overlay::mappedChanged, this, &Overlay::syncHint);
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
    ensureUi();
    m_clock.start();
    m_release.stop();
    m_releaseArmed = false;
    m_phase = Phase::Showing;
    qInfo() << "microset overlay: show requested";
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

void Overlay::setHint(bool active, const QString &device)
{
    if (active == m_hint && device == m_hintDevice)
        return;
    m_hint = active;
    m_hintDevice = device;
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
