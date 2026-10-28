#pragma once

#include "steamlibrary.h"
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
    static inline const QString UpdatesGroup = QStringLiteral("updates");
    static inline const QString GamesGroup = QStringLiteral("games");
    static inline const QString AccentKey = QStringLiteral("microdap.accent_hue");
    static inline const QString LedHueKey = QStringLiteral("rgb.hue");
    static inline const QString UndervoltKey = QStringLiteral("power.gpu_undervolt");
    static inline const QString UndervoltActiveKey = QStringLiteral("power.gpu_undervolt_active");
    static inline const QString TuningFallbackKey = QStringLiteral("power.gpu_tuning_fallback");
    static inline const QString TuningTripKey = QStringLiteral("power.gpu_tuning_trip");
    static inline const QString HoldPending = QStringLiteral("net.seilent.MicroSet1.Error.HoldPending");
    static inline const QString SetFailed = QStringLiteral("set failed");
    static inline const QString NotAvailable = QStringLiteral("not available");
    static inline const QString TryLater = QStringLiteral("try again shortly");

    explicit SettingsModel(QObject *parent = nullptr);
    explicit SettingsModel(const QDBusConnection &bus, QObject *parent = nullptr);
    ~SettingsModel() override;

    void load();
    void setFixture(const QJsonObject &schema, const QVariantMap &values, const QVariantMap &stored);
    void setProtonFixture(const QMap<QString, QJsonObject> &effective);
    UpdateInfo *updates();

    bool available() const;
    int revision() const;
    int accentHue() const;
    QString updatesGroup() const;

    Q_INVOKABLE QVariantList groups(int revision) const;
    Q_INVOKABLE QVariantList rows(const QString &group, int revision) const;
    Q_INVOKABLE QString pageTitle(const QString &group, int revision) const;
    Q_INVOKABLE bool beginEdit(const QString &key);
    Q_INVOKABLE bool busy(const QString &key) const;
    Q_INVOKABLE QVariantMap sheet(const QString &key) const;
    Q_INVOKABLE bool preview(const QVariant &value);
    Q_INVOKABLE void endPreview(const QVariant &value);
    Q_INVOKABLE void cancelPreview();
    Q_INVOKABLE void endPreviews();
    Q_INVOKABLE QVariant next(const QString &key, const QVariant &from, int delta) const;
    Q_INVOKABLE QString text(const QString &key, const QVariant &value) const;
    Q_INVOKABLE bool set(const QString &key, const QVariant &value);
    Q_INVOKABLE void reloadUpdates();
    Q_INVOKABLE void enter(const QString &group);
    Q_INVOKABLE void leave(const QString &group);
    Q_INVOKABLE QVariant value(const QString &key, int revision) const;
    Q_INVOKABLE void hold(const QString &key);
    Q_INVOKABLE void drop(const QString &key);

    static int normalizeHue(int hue);
    static QString toastText(const QString &error);

signals:
    void availableChanged();
    void revisionChanged();
    void refused(const QString &title, const QString &text);
    void previewEnded(const QString &key);

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
        std::optional<double> floor;
        double step = 1;
        double scale = 0;
        QJsonObject labels;
        bool writable = true;
        bool overridable = false;
        bool confirm = false;
        bool bounded = false;
        QString lockKey;
        QJsonValue lockIs;
        QString lockText;
        QString lockShow;
        QVariant defaultValue;
        QVariant store;
        QVariant shown;
        int inflight = 0;
        QString appid;
        QString option;
    };

    struct Preview
    {
        QString key;
        QVariant start;
        QVariant last;
        bool override = false;
        bool draft = false;
        QVariantList ours;
    };

    using Reply = std::function<void(const QDBusMessage &)>;

    void call(const QString &method, const QVariantList &arguments, Reply done);
    void build(const QJsonObject &schema, const QVariantMap &values, const QVariantMap &stored);
    static std::optional<Entry> entryOf(const QString &key, const QJsonObject &object);
    void restore(Entry &entry, const QVariant &raw) const;
    void instantiate(QMap<QString, Entry> &entries, const QString &appid) const;
    void setAvailable(bool available);
    void bump();
    void onChanged(const QString &key, const QVariant &value);
    void onHoldRefused(const QString &key, const QString &reason);
    void onOwnerChanged(const QString &name, const QString &oldOwner, const QString &newOwner);
    void settle(const QString &key, const QString &error);
    void refreshStored(const QString &key);
    QString titleOf(const QString &group) const;
    bool editable(const Entry &entry) const;
    bool locked(const Entry &entry) const;
    QString kindOf(const Entry &entry) const;
    QString pickOf(const Entry &entry) const;
    QVariantList sheetValues(const Entry &entry) const;
    QVariant shownOf(const Entry &entry) const;
    bool write(const QString &key, const QVariant &value);
    bool overrideTo(const QString &key, const QVariant &value);
    void clearOverride(const QString &key);
    void closePreview(const QString &key);
    void dropPreview();
    void watchPreview(const Entry &entry);
    QString textOf(const Entry &entry, const QVariant &value) const;
    QString rowText(const Entry &entry, const QVariant &value) const;
    QString autoText(const Entry &entry) const;
    QString profileText(const Entry &entry, const QVariant &value) const;
    QStringList templateOrder() const;
    QString undervoltText(const Entry &entry) const;
    QVariantMap rowOf(const Entry &entry) const;
    QList<const Entry *> visible(const QString &group) const;
    QVariantList updateRows() const;
    QStringList liveKeys(const QString &group) const;
    QStringList storedApps() const;
    QVariantList gameRows() const;
    void fetchEffective(const QString &appid);
    void refreshEffective(const QString &key);
    static QVariant coerce(const QString &type, const QVariant &value);
    static QVariantMap label(const QString &owner);

    std::unique_ptr<MicroSet::Client> m_client;
    QDBusConnection m_bus;
    UpdateInfo m_updates;
    QMap<QString, Entry> m_entries;
    QMap<QString, QJsonObject> m_templates;
    QSet<QString> m_apps;
    QMap<QString, QString> m_names;
    QList<SteamLibrary::Game> m_library;
    bool m_libraryRead = false;
    QByteArray m_libraryStamp;
    QMap<QString, QJsonObject> m_effective;
    QMap<QString, QJsonObject> m_effectiveFixture;
    int m_effectivePending = 0;
    bool m_effectiveMoved = false;
    QMap<QString, QString> m_groupLabels;
    QVariantMap m_values;
    QSet<QString> m_live;
    QSet<QString> m_referenced;
    QMap<QString, int> m_watching;
    std::optional<Preview> m_preview;
    bool m_available = false;
    bool m_fixture = false;
    int m_revision = 0;
    int m_generation = 0;
};
