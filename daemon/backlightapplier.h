#pragma once

#include "settings.h"

#include <QObject>

class QSocketNotifier;

class BacklightApplier : public QObject
{
    Q_OBJECT

public:
    static inline const QString Name = QStringLiteral("display.brightness");

    BacklightApplier(Settings &settings, const QString &backlight = QStringLiteral("/sys/class/backlight/backlight"),
                     const QString &runtime = QStringLiteral("/run/microset"), QObject *parent = nullptr);
    ~BacklightApplier() override;

    static quint64 raw(uint basisPoints, quint64 max);
    static uint basisPoints(quint64 raw, quint64 max);

    void start();
    bool apply(uint basisPoints);
    void refresh();

private:
    bool write(quint64 raw, bool force);
    bool openWatch();
    uint floor() const;
    void warn(bool &flag, const QString &line);

    Settings &m_settings;
    QString m_backlight;
    QString m_marker;
    QSocketNotifier *m_notifier = nullptr;
    int m_watchFd = -1;
    quint64 m_max = 0;
    quint64 m_last = 0;
    bool m_ok = false;
    bool m_writeWarned = false;
    bool m_readWarned = false;
};
