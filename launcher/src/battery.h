#pragma once

#include <QDBusConnection>
#include <QObject>
#include <QString>

class Battery : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool present READ present NOTIFY presentChanged)
    Q_PROPERTY(int percent READ percent NOTIFY percentChanged)
    Q_PROPERTY(bool low READ low NOTIFY percentChanged)

public:
    static QString service();
    static QString path();
    static QString deviceInterface();
    static QString propertiesInterface();

    explicit Battery(const QDBusConnection &bus, QObject *parent = nullptr);

    bool present() const;
    int percent() const;
    bool low() const;

    void read();

signals:
    void presentChanged();
    void percentChanged();

private slots:
    void onProperties(const QString &interface, const QVariantMap &changed, const QStringList &invalidated);

private:
    void subscribe();
    void apply(const QVariantMap &values);
    void setPresent(bool present);
    void setPercent(int percent);

    QDBusConnection m_bus;
    bool m_present = false;
    int m_percent = 100;
};
