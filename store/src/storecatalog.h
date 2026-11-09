#pragma once

#include "storeentry.h"

#include <QList>
#include <QString>

QList<StoreEntry> parseAppStreamCatalog(const QString &file, const QString &icons64, const QString &icons128,
                                        QString *error = nullptr);
