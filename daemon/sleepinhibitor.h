#pragma once

#include <QDBusConnection>
#include <QObject>
#include <QString>

class SleepInhibitor : public QObject
{
    Q_OBJECT

public:
    SleepInhibitor(const QDBusConnection &bus, const QString &why, QObject *parent = nullptr);
    ~SleepInhibitor() override;

    static QString service();
    static QString path();
    static QString interface();

    bool subscribed() const;
    bool held() const;
    int takeCount() const;

    void take();
    void release();
    void defer();
    void ready();

signals:
    void prepareForSleep(bool start);
    void heldChanged();

private slots:
    void onPrepareForSleep(bool start);

private:
    QDBusConnection m_bus;
    QString m_why;
    bool m_subscribed = false;
    bool m_taking = false;
    bool m_sleeping = false;
    int m_pending = 0;
    int m_fd = -1;
    int m_takeCount = 0;
};
