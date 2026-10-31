#pragma once

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusVirtualObject>
#include <QList>
#include <QStringList>
#include <QVariant>

class FakeManager : public QDBusVirtualObject
{
public:
    static inline const QString Service = QStringLiteral("org.freedesktop.systemd1");
    static inline const QString Path = QStringLiteral("/org/freedesktop/systemd1");
    static inline const QString Manager = QStringLiteral("org.freedesktop.systemd1.Manager");

    explicit FakeManager(const QDBusConnection &bus)
        : m_bus(bus)
    {
    }

    QString introspect(const QString &) const override { return {}; }

    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override
    {
        if (message.member() == QStringLiteral("Subscribe")) {
            calls << QStringLiteral("Subscribe");
            connection.send(message.createReply());
            return true;
        }
        if (message.member() != QStringLiteral("StartUnit"))
            return false;
        calls << QStringLiteral("StartUnit");
        units << message.arguments().value(0).toString();
        modes << message.arguments().value(1).toString();
        if (hold) {
            held << message;
            return true;
        }
        if (fail)
            refuse(message);
        else
            answer(message);
        return true;
    }

    void answer(const QDBusMessage &message)
    {
        if (!earlyResult.isEmpty()) {
            const QString result = earlyResult;
            earlyResult.clear();
            removed(job, units.last(), result);
        }
        m_bus.send(message.createReply(QVariant::fromValue(QDBusObjectPath(job))));
    }

    void refuse(const QDBusMessage &message)
    {
        m_bus.send(
            message.createErrorReply(QStringLiteral("org.freedesktop.DBus.Error.Failed"), QStringLiteral("failed")));
    }

    void removed(const QString &path, const QString &unit, const QString &result)
    {
        QDBusMessage signal = QDBusMessage::createSignal(Path, Manager, QStringLiteral("JobRemoved"));
        signal << uint(1) << QVariant::fromValue(QDBusObjectPath(path)) << unit << result;
        m_bus.send(signal);
    }

    QString job = QStringLiteral("/org/freedesktop/systemd1/job/4");
    QStringList calls;
    QStringList units;
    QStringList modes;
    QString earlyResult;
    QList<QDBusMessage> held;
    bool fail = false;
    bool hold = false;

private:
    QDBusConnection m_bus;
};
