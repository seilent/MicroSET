#pragma once

#include <QDBusConnection>
#include <QDBusContext>
#include <QObject>
#include <QString>

class Auth;
class Overlay;

class OverlayService : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "net.seilent.MicroSet.Overlay1")
    Q_PROPERTY(bool Shown READ shown)

public:
    OverlayService(Overlay *overlay, const Auth &auth, QObject *parent = nullptr);

    static QString service();
    static QString path();
    static QString interface();

    bool publish(QDBusConnection bus);
    bool shown() const;

public slots:
    Q_SCRIPTABLE void Toggle();
    Q_SCRIPTABLE void Show();
    Q_SCRIPTABLE void Hide();

private:
    void run(const char *method, void (Overlay::*action)());
    void announce();

    Overlay *m_overlay;
    const Auth &m_auth;
    QDBusConnection m_bus;
};
