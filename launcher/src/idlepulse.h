#pragma once

#include <QObject>

class IdlePulse : public QObject
{
    Q_OBJECT

public:
    explicit IdlePulse(QObject *parent = nullptr);

    virtual void pulse();
    virtual bool ready() const = 0;
    virtual int pulseCount() const;
    virtual void reset();

protected:
    void counted();

private:
    int m_count = 0;
};
