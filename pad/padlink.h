#pragma once

#include <QObject>

class PadLink : public QObject
{
    Q_OBJECT

public:
    explicit PadLink(QObject *parent = nullptr);

    virtual bool wanted() const = 0;
    virtual void setWanted(bool wanted) = 0;

signals:
    void releaseDone();
};
