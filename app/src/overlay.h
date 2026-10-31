#pragma once

#include "dbuspad.h"
#include "sessionmenu.h"

#include <QEasingCurve>
#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QTimer>
#include <QVariantAnimation>
#include <QWindow>

#include <functional>

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
    Q_PROPERTY(bool menuOpen READ menuOpen NOTIFY menuStateChanged)
    Q_PROPERTY(bool menuLeaving READ menuLeaving NOTIFY menuStateChanged)
    Q_PROPERTY(bool menuOnly READ menuOnly NOTIFY menuStateChanged)
    Q_PROPERTY(bool menuBusy READ menuBusy NOTIFY menuCallChanged)
    Q_PROPERTY(QStringList menuItems READ menuItems NOTIFY menuChanged)
    Q_PROPERTY(int menuCurrent READ menuCurrent NOTIFY menuChanged)
    Q_PROPERTY(QStringList menuNotes READ menuNotes NOTIFY menuChanged)

public:
    static constexpr int ShowMs = 200;
    static constexpr int HideMs = 150;
    static constexpr int ReleaseFrameMs = 100;
    static constexpr int MenuSwitchMs = 65000;
    static constexpr int MenuToastMs = 3500;

    using WindowHook = std::function<void(QWindow *)>;

    Overlay(DbusPad *pad, SleepInhibitor *sleep, QObject *parent = nullptr);

    void setUi(std::function<void()> ensure);
    void setWindow(QWindow *window);
    void setToastWindow(QWindow *window);
    void setHooks(WindowHook prepare, WindowHook prepareToast, WindowHook lower);
    void setSessionMenu(SessionMenu *menu);
    void setMenuTimeouts(int switchMs, int toastMs);

    bool shown() const;
    bool mapped() const;
    bool buried() const;
    qreal opacity() const;
    bool hintShown() const;
    bool hint() const;
    QString hintDevice() const;
    QString hintKind() const;
    bool menuOpen() const;
    bool menuLeaving() const;
    bool menuOnly() const;
    bool menuBusy() const;
    QStringList menuItems() const;
    int menuCurrent() const;
    QStringList menuNotes() const;
    int created() const;
    int destroyed() const;
    QWindow *window() const;
    QWindow *toastWindow() const;

    Q_INVOKABLE void show();
    Q_INVOKABLE void hide();
    Q_INVOKABLE void toggle();
    Q_INVOKABLE void hintDone();
    Q_INVOKABLE void menuAccept(int index);
    Q_INVOKABLE void menuClose();
    void setHint(bool active, const QString &device = {}, const QString &kind = {});

signals:
    void shownChanged();
    void mappedChanged();
    void opacityChanged();
    void hintShownChanged();
    void hintChanged();
    void menuStateChanged();
    void menuCallChanged();
    void menuChanged();
    void menuToast(const QString &text);
    void pageHint();
    void unused();

public slots:
    void showMenu(const QList<SessionMenuEntry> &entries, const QString &current, bool inGame, bool switching);

private slots:
    void onFrameSwapped();
    void onSwitchReplied(bool ok);
    void onSwitchJobRemoved(const QString &result);
    void onInstallReplied(bool ok);

private:
    enum class Phase { Hidden, Showing, Shown, Hiding, Releasing };
    enum class MenuCall { None, Install, Switch };

    void ensureUi();
    void reveal();
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
    void openMenu();
    void closeMenu();
    void leaveMenu();
    void resetMenu();
    void setMenuOnly(bool only);
    void armMenu(DbusPad::Action action);
    void armRelease(DbusPad::Action action);
    void setMenuCall(MenuCall call);
    void onPadPressed(DbusPad::Action action);
    void onPadReleased(DbusPad::Action action);
    void clearPad();

    DbusPad *m_pad;
    SleepInhibitor *m_sleep;
    SessionMenu *m_menu = nullptr;
    std::function<void()> m_ensure;
    QPointer<QWindow> m_window;
    QPointer<QWindow> m_toast;
    WindowHook m_prepare;
    WindowHook m_prepareToast;
    WindowHook m_lower;
    QVariantAnimation m_fade;
    QTimer m_release;
    QTimer m_menuSwitch;
    QTimer m_menuToast;
    QElapsedTimer m_clock;
    Phase m_phase = Phase::Hidden;
    MenuCall m_menuCall = MenuCall::None;
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
    bool m_menuOpen = false;
    bool m_menuLeaving = false;
    bool m_menuOnly = false;
    bool m_downA = false;
    bool m_downB = false;
    int m_menuArm = -1;
    int m_menuCurrent = -1;
    QStringList m_menuItems;
    QStringList m_menuNotes;
    QList<SessionMenuEntry> m_menuEntries;
    QString m_hintDevice;
    QString m_hintKind;
    qreal m_opacity = 0.0;
    int m_created = 0;
    int m_destroyed = 0;
};
