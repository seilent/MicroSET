#pragma once

#include "sscwire.h"

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QTimer>

#include <optional>

class SleepInhibitor;
class SscLink;

class SscRotation : public QObject
{
    Q_OBJECT

public:
    static constexpr int settleMs = 500;
    static constexpr int holdMs = 60000;
    static constexpr int retryMs = 3000;
    static constexpr int maxRetryMs = 60000;
    static constexpr int sleepReleaseMs = 500;

    SscRotation(SscLink *link, SleepInhibitor *sleep, int retry = retryMs, int hold = holdMs,
                QObject *parent = nullptr);
    ~SscRotation() override;

    bool supported() const;
    int turn() const;
    int raw() const;
    bool wanted() const;
    bool claimed() const;

    void setWanted(bool wanted);

signals:
    void supportedChanged();
    void turnChanged();

private:
    enum class Request { Suid, Enable, Flush, Disable };

    void begin();
    void onReceived(quint32 node, quint32 port, const QByteArray &bytes);
    void onLinkFailed();
    void onReopen();
    void onPrepareForSleep(bool start);
    void handleControl(const QByteArray &bytes);
    void handleResponse(const ssc::Message &message);
    void handleReport(const ssc::Report &report);
    void handleSuid(const QByteArray &payload);
    void handleValue(const QByteArray &payload);
    void sendLookup();
    quint16 send(Request kind, const QByteArray &request);
    void requestSuid();
    bool suidPending() const;
    void scheduleRetry();
    void backoff(QTimer &timer, int &delay);
    void enableFailed();
    void maybeEnable();
    quint16 disable();
    void dropServer();
    void releaseSleep();
    void lose();
    void take(int value);
    void settle();
    void updateSupported();
    void setTurn(int turn);

    SscLink *m_link = nullptr;
    SleepInhibitor *m_sleep = nullptr;
    QTimer m_settle;
    QTimer m_hold;
    QTimer m_retry;
    QTimer m_enableRetry;
    QTimer m_reopen;
    QTimer m_sleepRelease;
    QHash<quint16, Request> m_requests;
    std::optional<ssc::Control> m_server;
    std::optional<ssc::Suid> m_suid;
    int m_retryBase = retryMs;
    int m_retryDelay = retryMs;
    int m_enableDelay = retryMs;
    int m_reopenDelay = retryMs;
    quint16 m_txn = 0;
    quint16 m_enableTxn = 0;
    quint16 m_sleepTxn = 0;
    bool m_begun = false;
    bool m_holding = false;
    bool m_supported = false;
    bool m_wanted = false;
    bool m_claimed = false;
    bool m_asleep = false;
    bool m_deferred = false;
    int m_turn = -1;
    int m_last = -1;
    int m_pending = -1;
    int m_raw = -1;
};
