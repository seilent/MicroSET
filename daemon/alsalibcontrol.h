#pragma once

#include "alsaapplier.h"

#include <QHash>
#include <QSocketNotifier>

typedef struct _snd_ctl snd_ctl_t;

class AlsaLibControl : public AlsaControl
{
    Q_OBJECT

public:
    explicit AlsaLibControl(const QString &card, QObject *parent = nullptr);
    ~AlsaLibControl() override;

    QString card() const override;
    bool open() override;
    void close() override;
    bool isOpen() const override;
    std::optional<QStringList> read(const QString &control) override;
    bool write(const QString &control, const QString &value) override;

private:
    struct Element
    {
        bool enumerated = false;
        unsigned count = 0;
        QStringList items;
    };

    const Element *element(const QString &control);
    void readEvents();

    QString m_card;
    snd_ctl_t *m_ctl = nullptr;
    QSocketNotifier *m_notifier = nullptr;
    QHash<QString, Element> m_elements;
};
