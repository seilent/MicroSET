#pragma once

#include <QEasingCurve>
#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantAnimation>
#include <QWindow>

#include <functional>

class DbusPad;
class SleepInhibitor;

class Overlay : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool shown READ shown NOTIFY shownChanged)
    Q_PROPERTY(bool mapped READ mapped NOTIFY mappedChanged)
    Q_PROPERTY(qreal opacity READ opacity NOTIFY opacityChanged)
    Q_PROPERTY(bool hintShown READ hintShown NOTIFY hintShownChanged)
    Q_PROPERTY(bool hint READ hint NOTIFY hintChanged)
    Q_PROPERTY(QString hintDevice READ hintDevice NOTIFY hintChanged)
    Q_PROPERTY(QString hintKind READ hintKind NOTIFY hintChanged)

public:
    static constexpr int ShowMs = 200;
    static constexpr int HideMs = 150;
    static constexpr int ReleaseFrameMs = 100;

    using WindowHook = std::function<void(QWindow *)>;

    Overlay(DbusPad *pad, SleepInhibitor *sleep, QObject *parent = nullptr);

    void setUi(std::function<void()> ensure);
    void setWindow(QWindow *window);
    void setToastWindow(QWindow *window);
    void setHooks(WindowHook prepare, WindowHook prepareToast, WindowHook lower);

    bool shown() const;
    bool mapped() const;
    bool buried() const;
    qreal opacity() const;
    bool hintShown() const;
    bool hint() const;
    QString hintDevice() const;
    QString hintKind() const;
    int created() const;
    int destroyed() const;
    QWindow *window() const;
    QWindow *toastWindow() const;

    Q_INVOKABLE void show();
    Q_INVOKABLE void hide();
    Q_INVOKABLE void toggle();
    Q_INVOKABLE void hintDone();
    void setHint(bool active, const QString &device = {}, const QString &kind = {});

signals:
    void shownChanged();
    void mappedChanged();
    void opacityChanged();
    void hintShownChanged();
    void hintChanged();
    void pageHint();
    void unused();

private slots:
    void onFrameSwapped();

private:
    enum class Phase { Hidden, Showing, Shown, Hiding, Releasing };

    void ensureUi();
    void raise();
    void onHolder();
    void onAcquired(bool ok);
    void onArbiterLost();
    void onReleaseDone();
    void onPrepareForSleep(bool start);
    void release();
    void fadeTo(qreal to, int ms, QEasingCurve::Type curve);
    void onFadeFinished();
    void beginRelease();
    void finishRelease();
    void mapWindow();
    void destroyWindow();
    void dropToast();
    void syncHint();
    void checkUnused();
    void setShown(bool shown);
    void setMapped(bool mapped);
    void setOpacity(qreal value);
    void setHintShown(bool shown);
    void step(const char *name) const;

    DbusPad *m_pad;
    SleepInhibitor *m_sleep;
    std::function<void()> m_ensure;
    QPointer<QWindow> m_window;
    QPointer<QWindow> m_toast;
    WindowHook m_prepare;
    WindowHook m_prepareToast;
    WindowHook m_lower;
    QVariantAnimation m_fade;
    QTimer m_release;
    QElapsedTimer m_clock;
    Phase m_phase = Phase::Hidden;
    quint64 m_releaseGeneration = 0;
    bool m_releaseArmed = false;
    bool m_frames = false;
    bool m_firstFrame = false;
    bool m_shown = false;
    bool m_mapped = false;
    bool m_acquiring = false;
    bool m_inStack = false;
    bool m_releasing = false;
    bool m_sleepWait = false;
    bool m_hint = false;
    bool m_hintShown = false;
    bool m_pageHinted = false;
    QString m_hintDevice;
    QString m_hintKind;
    qreal m_opacity = 0.0;
    int m_created = 0;
    int m_destroyed = 0;
};
