#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QString>

class Env : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QString fontDir READ fontDir CONSTANT)
    Q_PROPERTY(int landscapeTurn READ landscapeTurn CONSTANT)

public:
    explicit Env(QObject *parent = nullptr);

    QString fontDir() const;
    int landscapeTurn() const;
};
