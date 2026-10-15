#pragma once

#include "alsaapplier.h"

#include <QMap>

class FakeAlsa : public AlsaControl
{
public:
    struct Control
    {
        QStringList values;
        QStringList items;
        bool events = true;
    };

    FakeAlsa();

    QString card() const override;
    bool open() override;
    void close() override;
    bool isOpen() const override;
    std::optional<QStringList> read(const QString &control) override;
    bool write(const QString &control, const QString &value) override;

    void foreign(const QString &control, const QString &value);
    void unplug();
    QString value(const QString &control) const;

    QMap<QString, Control> controls;
    QStringList writes;
    bool present = true;
    bool broken = false;
    int opens = 0;

private:
    bool set(const QString &control, const QString &value);

    bool m_open = false;
};
