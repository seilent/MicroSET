#pragma once

#include <QDBusConnection>
#include <QDBusVirtualObject>
#include <QList>
#include <QStringList>

class FakeLogind : public QDBusVirtualObject
{
public:
    explicit FakeLogind(const QString &address);
    ~FakeLogind() override;

    bool start();
    void prepareForSleep(bool start);
    int open();

    QString introspect(const QString &path) const override;
    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override;

    QStringList inhibits;
    bool refuse = false;

private:
    QString m_address;
    QList<int> m_reads;
};
