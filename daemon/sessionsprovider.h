#pragma once

#include <QStringList>

class SessionsProvider
{
public:
    explicit SessionsProvider(const QString &root = {});

    QStringList defaults() const;

private:
    bool desktopExists(const QString &desktop) const;
    bool unitExists(const QString &unit) const;

    QString m_root;
};
