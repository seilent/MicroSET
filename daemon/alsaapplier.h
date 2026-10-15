#pragma once

#include "settings.h"

#include <QDBusConnection>
#include <QFileSystemWatcher>
#include <QObject>
#include <QSet>
#include <QTimer>

#include <optional>

class AlsaControl : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    virtual QString card() const = 0;
    virtual bool open() = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;
    virtual std::optional<QStringList> read(const QString &control) = 0;
    virtual bool write(const QString &control, const QString &value) = 0;

signals:
    void changed(const QString &control);
    void lost();
};

class AlsaApplier : public QObject
{
    Q_OBJECT

public:
    using Values = QList<std::pair<QString, QString>>;

    AlsaApplier(const QDBusConnection &bus, Settings &settings, AlsaControl &control,
                const QString &devices = QStringLiteral("/dev/snd"), int settleMs = 5000, QObject *parent = nullptr);

    void start();
    Values wanted(const QString &key) const;

private slots:
    void prepareForSleep(bool start);

private:
    static const QStringList &keys();
    static const Values &fixed();
    void watch();
    void rescan();
    void apply(const Values &values, const char *cause);
    Values all() const;
    void check(const QStringList &controls, const QString &cause);
    QStringList controls() const;
    QString wantedValue(const QString &control) const;
    void missing(const QString &control);
    bool write(const QString &control, const QString &value);
    void writeFailed(const QString &control, const QString &line);

    QDBusConnection m_bus;
    Settings &m_settings;
    AlsaControl &m_control;
    QString m_devices;
    QFileSystemWatcher m_watcher;
    QTimer m_settle;
    QSet<QString> m_missing;
    QSet<QString> m_guarded;
    QSet<QString> m_failed;
};
