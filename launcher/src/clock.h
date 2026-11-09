#pragma once

#include <QDateTime>
#include <QObject>
#include <QString>

class QSocketNotifier;

class Clock : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString text READ text NOTIFY textChanged)

public:
    static constexpr int SettleMs = 20;

    explicit Clock(QObject *parent = nullptr);
    ~Clock() override;

    static QString format(const QDateTime &now);

    QString text() const;
    int armCount() const;
    int cancelCount() const;

    void start();

signals:
    void textChanged();

private:
    void arm();
    void fire();
    void publish();

    QSocketNotifier *m_notifier = nullptr;
    int m_fd = -1;
    QString m_text;
    int m_arms = 0;
    int m_cancels = 0;
};
