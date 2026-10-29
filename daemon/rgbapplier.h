#pragma once

#include "settings.h"

#include <QByteArray>
#include <QObject>
#include <QTimer>

class QSocketNotifier;
class SleepInhibitor;

class RgbApplier : public QObject
{
    Q_OBJECT

public:
    static constexpr int repeats = 3;
    static constexpr int repeatMs = 40;
    static constexpr int maxTries = 25;
    static constexpr int powerMs = 1000;

    RgbApplier(Settings &settings, SleepInhibitor *sleep, const QString &device = QStringLiteral("/dev/ttyHS2"),
               const QString &backlight = QStringLiteral("/sys/class/backlight/backlight"), QObject *parent = nullptr);
    ~RgbApplier() override;

    static QByteArray frame(uint rgb);

    void start();
    void refresh();
    QByteArray wanted() const;
    bool watchingPower() const;

signals:
    void frameWritten();

private:
    void onPrepareForSleep(bool start);
    void schedule();
    void recompute();
    void readPower();
    bool openBacklight();
    bool readScreen();
    void closeBacklight(const QString &reason);
    void send(const QByteArray &frame);
    void writeNext();
    void finish();
    bool openDevice();
    void closeDevice();
    void warn(bool &flag, const QString &line);

    Settings &m_settings;
    SleepInhibitor *m_sleep = nullptr;
    QString m_device;
    QString m_backlight;
    QTimer m_repeat;
    QTimer m_recompute;
    QTimer m_power;
    QSocketNotifier *m_notifier = nullptr;
    QByteArray m_frame;
    int m_left = 0;
    int m_tries = 0;
    int m_fd = -1;
    int m_screenFd = -1;
    quint64 m_screen = 0;
    quint64 m_max = 0;
    bool m_powerOff = false;
    bool m_asleep = false;
    bool m_deferred = false;
    bool m_warned = false;
    bool m_lightWarned = false;
};
