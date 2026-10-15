#pragma once

#include <QByteArray>
#include <QObject>

class SscLink : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    virtual bool open() const = 0;
    virtual quint32 localNode() const = 0;
    virtual bool send(quint32 node, quint32 port, const QByteArray &bytes) = 0;
    virtual bool reopen() = 0;

signals:
    void received(quint32 node, quint32 port, const QByteArray &bytes);
    void failed();
};
