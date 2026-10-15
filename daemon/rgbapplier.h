#pragma once

#include "settings.h"

#include <QByteArray>
#include <QObject>
#include <QTimer>

class SleepInhibitor;

class RgbApplier : public QObject
{
    Q_OBJECT

public:
    static constexpr int repeats = 3;
    static constexpr int repeatMs = 40;
    static constexpr int maxTries = 25;

    RgbApplier(Settings &settings, SleepInhibitor *sleep, const QString &device = QStringLiteral("/dev/ttyHS2"),
               QObject *parent = nullptr);
    ~RgbApplier() override;

    static QByteArray frame(uint color, uint brightness);

    void start();
    QByteArray wanted() const;

private:
    void onPrepareForSleep(bool start);
    void send(const QByteArray &frame);
    void writeNext();
    void finish();
    bool openDevice();
    void closeDevice();
    void warn(const QString &line);

    Settings &m_settings;
    SleepInhibitor *m_sleep = nullptr;
    QString m_device;
    QTimer m_repeat;
    QByteArray m_frame;
    int m_left = 0;
    int m_tries = 0;
    int m_fd = -1;
    bool m_asleep = false;
    bool m_deferred = false;
    bool m_warned = false;
};
