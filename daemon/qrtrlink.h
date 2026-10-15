#pragma once

#include "ssclink.h"

#include <QByteArray>

class QSocketNotifier;

class QrtrLink : public SscLink
{
    Q_OBJECT

public:
    explicit QrtrLink(QObject *parent = nullptr);
    ~QrtrLink() override;

    bool open() const override;
    quint32 localNode() const override;
    bool send(quint32 node, quint32 port, const QByteArray &bytes) override;
    bool reopen() override;

private:
    bool openSocket();
    void closeSocket();
    void drain();

    int m_fd = -1;
    quint32 m_node = 0;
    QSocketNotifier *m_notifier = nullptr;
    QByteArray m_buffer;
};
