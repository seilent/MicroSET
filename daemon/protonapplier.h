#pragma once

#include "protonresolver.h"
#include "settings.h"

#include <QHash>
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QRegularExpression>
#include <QStringList>

#include <memory>
#include <vector>

struct ProtonSection
{
    QMap<QString, QString> env;
    QStringList dxvk;
    QString token;
    QMap<QString, QString> otherwise;
    QString state;
    QStringList profile;
    QStringList rules;
    QStringList yield;
};

class ProtonFeature
{
public:
    virtual ~ProtonFeature() = default;
    virtual QString name() const = 0;
    virtual QMap<QString, QString> names() const = 0;
    virtual QMap<QString, QString> fragments() const;
    virtual ProtonSection section(const QString &appid, bool profile) const = 0;
};

std::unique_ptr<ProtonFeature> fexFeature(const ProtonResolver &resolver);
std::unique_ptr<ProtonFeature> wineFeature(const ProtonResolver &resolver);
std::unique_ptr<ProtonFeature> lsfgFeature(const ProtonResolver &resolver);

class ProtonApplier : public QObject
{
    Q_OBJECT

public:
    ProtonApplier(const Settings &settings, const ProtonResolver &resolver, const QString &dir,
                  QObject *parent = nullptr);
    ~ProtonApplier() override;

    bool addFeature(std::unique_ptr<ProtonFeature> feature);
    bool start();
    QJsonObject document(const QString &appid) const;

private:
    struct Registered
    {
        std::unique_ptr<ProtonFeature> feature;
        QHash<QString, QRegularExpression> names;
        QHash<QString, QRegularExpression> fragments;
    };

    QJsonObject section(const Registered &entry, const QString &appid, QStringList *below, QStringList *profile,
                        QStringList *yield) const;
    QJsonObject env(const Registered &entry, const QMap<QString, QString> &values) const;
    void onChanged(const QString &key);
    void writeAll();
    void writeApp(const QString &appid);
    void write(const QString &name, const QByteArray &data);
    void remove(const QString &name);

    const Settings &m_settings;
    const ProtonResolver &m_resolver;
    QString m_dir;
    bool m_ready = false;
    std::vector<Registered> m_features;
    QHash<QString, QByteArray> m_written;
};
