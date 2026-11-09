#pragma once

#include <QDBusConnection>
#include <QObject>
#include <QString>

class PowerAction : public QObject
{
    Q_OBJECT

public:
    explicit PowerAction(QObject *parent = nullptr);

    virtual void run(const QString &verb) = 0;
};

class LogindPower : public PowerAction
{
    Q_OBJECT

public:
    static QString service();
    static QString path();
    static QString interface();

    explicit LogindPower(const QDBusConnection &bus, QObject *parent = nullptr);

    void run(const QString &verb) override;

private:
    QDBusConnection m_bus;
};
