#pragma once

#include <QList>
#include <QMetaType>
#include <QString>

struct StoreEntry
{
    QString id;
    QString ref;
    QString name;
    QString summary;
    QString developer;
    QString icon;
    QString iconLarge;
    qint64 installedSize = 0;
    bool installed = false;
};

struct StoreProgress
{
    QString id;
    QString message;
    int percent = -1;
    bool installing = false;
};

Q_DECLARE_METATYPE(StoreEntry)
Q_DECLARE_METATYPE(StoreProgress)
Q_DECLARE_METATYPE(QList<StoreEntry>)
