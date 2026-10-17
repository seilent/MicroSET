#pragma once

#include "updateinfo.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QJsonObject>
#include <QJsonValue>
#include <QMap>
#include <QObject>
#include <QQmlEngine>
#include <QSet>
#include <QVariant>

#include <functional>
#include <memory>
#include <optional>

namespace MicroSet {
class Client;
}

class SettingsModel : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("SettingsModel comes from C++")
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
    Q_PROPERTY(int accentHue READ accentHue NOTIFY revisionChanged)
    Q_PROPERTY(QString updatesGroup READ updatesGroup CONSTANT)

public:
    static constexpr uint ColorMax = 0xffffff;
    static inline const QString UpdatesGroup = QStringLiteral("updates");
    static inline const QString AccentKey = QStringLiteral("microdap.accent_hue");
    static inline const QString SetFailed = QStringLiteral("set failed");
    static inline const QString NotAvailable = QStringLiteral("not available");

    explicit SettingsModel(QObject *parent = nullptr);
    explicit SettingsModel(const QDBusConnection &bus, QObject *parent = nullptr);
    ~SettingsModel() override;

    void load();
    void setFixture(const QJsonObject &schema, const QVariantMap &values, const QVariantMap &stored);
    UpdateInfo *updates();

    bool available() const;
    int revision() const;
    int accentHue() const;
    QString updatesGroup() const;

    Q_INVOKABLE QVariantList groups(int revision) const;
    Q_INVOKABLE QVariantList rows(const QString &group, int revision) const;
    Q_INVOKABLE bool beginEdit(const QString &key);
    Q_INVOKABLE QVariant next(const QString &key, const QVariant &from, int delta) const;
    Q_INVOKABLE QString text(const QString &key, const QVariant &value) const;
    Q_INVOKABLE bool set(const QString &key, const QVariant &value);
    Q_INVOKABLE uint colorFor(int hue) const;
    Q_INVOKABLE void reloadUpdates();
    Q_INVOKABLE void enter(const QString &group);
    Q_INVOKABLE void leave(const QString &group);

    static int normalizeHue(int hue);
    static int hueOf(uint rgb);
    static QString toastText(const QString &error);

signals:
    void availableChanged();
    void revisionChanged();
    void refused(const QString &title, const QString &text);

private:
    struct Entry
    {
        QString key;
        QString owner;
        QString group;
        std::optional<int> groupOrder;
        QString title;
        QString type;
        QString widget;
        QString unit;
        std::optional<int> order;
        QVariantList choices;
        QVariantList steps;
        std::optional<double> min;
        std::optional<double> max;
        double step = 1;
        QJsonObject labels;
        bool writable = true;
        bool overridable = false;
        bool confirm = false;
        QString lockKey;
        QJsonValue lockIs;
        QString lockText;
        QString lockShow;
        QVariant defaultValue;
        QVariant store;
        QVariant shown;
        int inflight = 0;
    };

    using Reply = std::function<void(const QDBusMessage &)>;

    void call(const QString &method, const QVariantList &arguments, Reply done);
    void build(const QJsonObject &schema, const QVariantMap &values, const QVariantMap &stored);
    void setAvailable(bool available);
    void bump();
    void onChanged(const QString &key, const QVariant &value);
    void onOwnerChanged(const QString &name, const QString &oldOwner, const QString &newOwner);
    void settle(const QString &key, const QString &error);
    void refreshStored(const QString &key);
    QString titleOf(const QString &group) const;
    bool editable(const Entry &entry) const;
    bool locked(const Entry &entry) const;
    QString kindOf(const Entry &entry) const;
    QString textOf(const Entry &entry, const QVariant &value) const;
    QVariantMap rowOf(const Entry &entry) const;
    QList<const Entry *> visible(const QString &group) const;
    QVariantList updateRows() const;
    QStringList liveKeys(const QString &group) const;
    static QVariant coerce(const QString &type, const QVariant &value);
    static QVariantMap label(const QString &owner);

    std::unique_ptr<MicroSet::Client> m_client;
    QDBusConnection m_bus;
    UpdateInfo m_updates;
    QMap<QString, Entry> m_entries;
    QMap<QString, QString> m_groupLabels;
    QVariantMap m_values;
    QSet<QString> m_live;
    QSet<QString> m_referenced;
    QMap<QString, int> m_watching;
    bool m_available = false;
    bool m_fixture = false;
    int m_revision = 0;
    int m_generation = 0;
};
