#pragma once

#include <QMap>
#include <QObject>
#include <QString>

class Activator : public QObject
{
    Q_OBJECT

public:
    explicit Activator(QObject *parent = nullptr);

    virtual void start(const QString &id) = 0;
    virtual void reportStarted(const QString &id, qint64 pid) = 0;
    virtual void reportFailed(const QString &id, const QString &message) = 0;
    virtual QMap<QString, QString> environment() const = 0;

signals:
    void started(const QString &id, qint64 pid);
    void failed(const QString &id, const QString &message);
};
