#pragma once

#include <QDBusConnection>
#include <QDBusVariant>
#include <QObject>
#include <QtQml/qqmlregistration.h>

class AccentWatch : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("AccentWatch comes from C++")
    Q_PROPERTY(int hue READ hue NOTIFY changed)
    Q_PROPERTY(bool off READ off NOTIFY changed)

public:
    static inline const QString HueKey = QStringLiteral("microdap.accent_hue");
    static inline const QString OffKey = QStringLiteral("microdap.accent_off");

    explicit AccentWatch(QObject *parent = nullptr);
    explicit AccentWatch(const QDBusConnection &bus, QObject *parent = nullptr);

    int hue() const;
    bool off() const;
    void setFixture(int hue, bool off);

signals:
    void changed();

private slots:
    void onChanged(const QString &key, const QDBusVariant &value);

private:
    void read();
    void take(const QString &key, const QVariant &value);

    QDBusConnection m_bus;
    int m_hue = -1;
    bool m_off = false;
};
