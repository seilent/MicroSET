#include "protonapplier.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace Qt::StringLiterals;

namespace {

const QString DefaultFile = u"default.json"_s;
constexpr qsizetype MaxFile = 4096;
const QString Owner = u"proton"_s;
const QString HookPrefix = u"MICROOS_"_s;
const QString NoNtsync = u"PROTON_NO_NTSYNC"_s;
const QRegularExpression FilePattern(u"^([1-9][0-9]{0,9}|default)\\.json$"_s);
const QRegularExpression TempPattern(u"^\\.([1-9][0-9]{0,9}|default)\\.json\\.[A-Za-z0-9]{6}$"_s);
const QRegularExpression FeaturePattern(u"^[a-z][a-z0-9]*$"_s);
const QRegularExpression NamePattern(u"^[A-Z][A-Z0-9_]*$"_s);
const QRegularExpression FragmentPattern(u"^[A-Za-z][A-Za-z0-9_.]*$"_s);

const QMap<QString, QString> FexEnv = {
    {u"fex.tso"_s, u"TSOENABLED"_s},
    {u"fex.half_barrier"_s, u"HALFBARRIERTSOENABLED"_s},
    {u"fex.vector_tso"_s, u"VECTORTSOENABLED"_s},
    {u"fex.memcpy_tso"_s, u"MEMCPYSETTSOENABLED"_s},
    {u"fex.multiblock"_s, u"MULTIBLOCK"_s},
    {u"fex.max_inst"_s, u"MAXINST"_s},
    {u"fex.x87_reduced"_s, u"X87REDUCEDPRECISION"_s},
    {u"fex.smc"_s, u"SMCCHECKS"_s},
    {u"fex.volatile_metadata"_s, u"VOLATILEMETADATA"_s},
    {u"fex.mono_hacks"_s, u"MONOHACKS"_s},
};

bool writeData(int fd, const QByteArray &data)
{
    qsizetype done = 0;
    while (done < data.size()) {
        const ssize_t n = ::write(fd, data.constData() + done, size_t(data.size() - done));
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return false;
        done += n;
    }
    return true;
}

QJsonArray sorted(QStringList names)
{
    names.removeDuplicates();
    names.sort();
    return QJsonArray::fromStringList(names);
}

class FexFeature : public ProtonFeature
{
public:
    explicit FexFeature(const ProtonResolver &resolver)
        : m_resolver(resolver)
    {
    }

    QString name() const override
    {
        return u"fex"_s;
    }

    QMap<QString, QString> names() const override
    {
        QMap<QString, QString> out;
        for (const QString &option : FexEnv)
            out.insert(u"FEX_"_s + option, u"[0-9a-z]{1,16}"_s);
        return out;
    }

    ProtonSection section(const QString &appid, bool profile) const override
    {
        ProtonSection out;
        for (const QString &option : m_resolver.options(name())) {
            const ProtonResolver::Resolved resolved = m_resolver.resolve(appid, option, profile);
            if (!FexEnv.contains(option) || resolved.value.isEmpty())
                continue;
            const QString env = u"FEX_"_s + FexEnv.value(option);
            if (resolved.value == "on"_L1 || resolved.value == "off"_L1)
                out.env.insert(env, resolved.value == "on"_L1 ? u"1"_s : u"0"_s);
            else
                out.env.insert(env, resolved.value);
            if (resolved.source == "profile"_L1) {
                out.profile << env;
                out.yield << env;
            }
        }
        return out;
    }

private:
    const ProtonResolver &m_resolver;
};

class WineFeature : public ProtonFeature
{
public:
    explicit WineFeature(const ProtonResolver &resolver)
        : m_resolver(resolver)
    {
    }

    QString name() const override
    {
        return u"wine"_s;
    }

    QMap<QString, QString> names() const override
    {
        return {{NoNtsync, u"[01]"_s}};
    }

    ProtonSection section(const QString &appid, bool profile) const override
    {
        ProtonSection out;
        const ProtonResolver::Resolved resolved = m_resolver.resolve(appid, u"wine.sync"_s, profile);
        if (resolved.value == "fsync"_L1) {
            out.env.insert(NoNtsync, u"1"_s);
            if (resolved.source == "profile"_L1)
                out.profile << NoNtsync;
        } else if (resolved.source == "set"_L1) {
            out.env.insert(NoNtsync, u"0"_s);
        }
        return out;
    }

private:
    const ProtonResolver &m_resolver;
};

}

QMap<QString, QString> ProtonFeature::fragments() const
{
    return {};
}

std::unique_ptr<ProtonFeature> fexFeature(const ProtonResolver &resolver)
{
    return std::make_unique<FexFeature>(resolver);
}

std::unique_ptr<ProtonFeature> wineFeature(const ProtonResolver &resolver)
{
    return std::make_unique<WineFeature>(resolver);
}

ProtonApplier::ProtonApplier(const Settings &settings, const ProtonResolver &resolver, const QString &dir,
                             QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_resolver(resolver)
    , m_dir(dir)
{
}

ProtonApplier::~ProtonApplier() = default;

bool ProtonApplier::addFeature(std::unique_ptr<ProtonFeature> feature)
{
    const QString name = feature->name();
    const auto claimed = [&](const QString &item) {
        return std::any_of(m_features.cbegin(), m_features.cend(), [&](const Registered &other) {
            return other.names.contains(item) || other.fragments.contains(item);
        });
    };
    QString why;
    if (!FeaturePattern.match(name).hasMatch() || name == Owner
        || std::any_of(m_features.cbegin(), m_features.cend(),
                       [&](const Registered &other) { return other.feature->name() == name; }))
        why = u"bad or taken feature name"_s;
    Registered entry;
    const auto take = [&](const QMap<QString, QString> &items, const QRegularExpression &shape,
                          QHash<QString, QRegularExpression> *into) {
        for (auto it = items.cbegin(); why.isEmpty() && it != items.cend(); ++it) {
            const QRegularExpression pattern(QRegularExpression::anchoredPattern(it.value()));
            if (!shape.match(it.key()).hasMatch() || it.key().startsWith(HookPrefix) || !pattern.isValid())
                why = u"bad name %1"_s.arg(it.key());
            else if (claimed(it.key()) || entry.names.contains(it.key()))
                why = u"%1 claimed twice"_s.arg(it.key());
            into->insert(it.key(), pattern);
        }
    };
    take(feature->names(), NamePattern, &entry.names);
    take(feature->fragments(), FragmentPattern, &entry.fragments);
    if (!why.isEmpty()) {
        qWarning("proton feature %s: %s, refused", qPrintable(name.left(32)), qPrintable(why));
        return false;
    }
    entry.feature = std::move(feature);
    m_features.push_back(std::move(entry));
    return true;
}

bool ProtonApplier::start()
{
    const QByteArray path = QFile::encodeName(m_dir);
    struct stat st;
    if (::mkdir(path.constData(), 0755) != 0 && errno != EEXIST) {
        qWarning("%s: cannot create: %s, no proton env files", path.constData(), strerror(errno));
        return false;
    }
    if (::lstat(path.constData(), &st) != 0 || !S_ISDIR(st.st_mode) || ::chmod(path.constData(), 0755) != 0) {
        qWarning("%s: not a directory, no proton env files", path.constData());
        return false;
    }
    m_ready = true;
    const QStringList apps = m_resolver.apps();
    for (const QString &name : QDir(m_dir).entryList(QDir::Files | QDir::Hidden | QDir::System)) {
        const QRegularExpressionMatch file = FilePattern.match(name);
        const bool kept = file.hasMatch() && (file.captured(1) == "default"_L1 || apps.contains(file.captured(1)));
        if (!kept && (file.hasMatch() || TempPattern.match(name).hasMatch()))
            ::unlink(QFile::encodeName(m_dir + u'/' + name).constData());
    }
    writeAll();
    connect(&m_settings, &Settings::changed, this, &ProtonApplier::onChanged);
    return true;
}

QJsonObject ProtonApplier::document(const QString &appid) const
{
    QJsonObject features;
    QStringList below;
    QStringList names;
    QStringList yield;
    for (const Registered &entry : m_features) {
        const QJsonObject built = section(entry, appid, &below, &names, &yield);
        if (!built.isEmpty())
            features.insert(entry.feature->name(), built);
    }
    QJsonObject out{{u"version"_s, 2}, {u"features"_s, features}, {u"below_local"_s, sorted(below)}};
    if (names.isEmpty())
        return out;
    QJsonObject profile{{u"names"_s, sorted(names)}, {u"yield"_s, sorted(yield)}};
    const ProtonProfiles::Entry *own = m_resolver.profiles().entry(appid);
    if (own && !own->tool.isEmpty())
        profile.insert(u"tool"_s, own->tool);
    out.insert(u"profile"_s, profile);
    return out;
}

QJsonObject ProtonApplier::env(const Registered &entry, const QMap<QString, QString> &values) const
{
    QJsonObject out;
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        const auto pattern = entry.names.constFind(it.key());
        if (pattern != entry.names.cend() && pattern->match(it.value()).hasMatch())
            out.insert(it.key(), it.value());
        else
            qWarning("proton feature %s: %s=%s is not in its allowlist, left out", qPrintable(entry.feature->name()),
                     qPrintable(it.key().left(64)), qPrintable(it.value().left(64)));
    }
    return out;
}

QJsonObject ProtonApplier::section(const Registered &entry, const QString &appid, QStringList *below,
                                   QStringList *profile, QStringList *yield) const
{
    const ProtonSection built = entry.feature->section(appid, true);
    const QJsonObject values = env(entry, built.env);
    const QJsonObject otherwise = env(entry, built.otherwise);
    QStringList present = values.keys() + otherwise.keys();
    QJsonArray dxvk;
    for (const QString &fragment : built.dxvk) {
        const QString key = fragment.section(u" = "_s, 0, 0);
        const QString value = fragment.section(u" = "_s, 1);
        const auto pattern = entry.fragments.constFind(key);
        if (pattern != entry.fragments.cend() && pattern->match(value).hasMatch()) {
            dxvk << fragment;
            present << key;
        } else {
            qWarning("proton feature %s: fragment %s is not in its allowlist, left out",
                     qPrintable(entry.feature->name()), qPrintable(fragment.left(64)));
        }
    }
    const auto here = [&](const QStringList &names) {
        QStringList out;
        for (const QString &name : names)
            if (present.contains(name))
                out << name;
        return out;
    };
    if (values.isEmpty() && dxvk.isEmpty() && built.token.isEmpty() && built.state.isEmpty())
        return {};
    const QStringList own = here(built.profile);
    *below << own << here(built.rules);
    *profile << own;
    for (const QString &name : here(built.yield))
        if (own.contains(name))
            *yield << name;
    QJsonObject out{{u"env"_s, values}};
    if (!dxvk.isEmpty())
        out.insert(u"dxvk"_s, dxvk);
    if (!built.token.isEmpty()) {
        out.insert(u"requires"_s, built.token);
        out.insert(u"otherwise"_s, otherwise);
    }
    if (!own.isEmpty()) {
        const ProtonSection off = entry.feature->section(appid, false);
        QMap<QString, QString> kept;
        for (const QString &name : own)
            if (off.env.contains(name))
                kept.insert(name, off.env.value(name));
        out.insert(u"fallback"_s, env(entry, kept));
    }
    if (!built.state.isEmpty())
        out.insert(u"state"_s, built.state);
    return out;
}

void ProtonApplier::onChanged(const QString &key)
{
    QString appid;
    if (Schema::splitApp(key, &appid, nullptr)) {
        writeApp(appid);
        return;
    }
    const QString owner = key.section(u'.', 0, 0);
    if (owner == Owner || std::any_of(m_features.cbegin(), m_features.cend(), [&](const Registered &entry) {
            return entry.feature->name() == owner;
        }))
        writeAll();
}

void ProtonApplier::writeAll()
{
    const QStringList apps = m_resolver.apps();
    write(DefaultFile, QJsonDocument(document({})).toJson(QJsonDocument::Compact));
    for (const QString &appid : apps)
        write(appid + u".json"_s, QJsonDocument(document(appid)).toJson(QJsonDocument::Compact));
    const QStringList written = m_written.keys();
    for (const QString &name : written)
        if (name != DefaultFile && !apps.contains(name.chopped(5)))
            remove(name);
}

void ProtonApplier::writeApp(const QString &appid)
{
    if (m_settings.hasApp(appid) || m_resolver.profiles().entry(appid))
        write(appid + u".json"_s, QJsonDocument(document(appid)).toJson(QJsonDocument::Compact));
    else
        remove(appid + u".json"_s);
}

void ProtonApplier::write(const QString &name, const QByteArray &data)
{
    if (!m_ready || m_written.value(name) == data)
        return;
    const QString path = m_dir + u'/' + name;
    if (data.size() > MaxFile) {
        qWarning("%s: over the hook's 4 KiB cap", qPrintable(path));
        return;
    }
    QByteArray temp = QFile::encodeName(m_dir + u"/."_s + name + u".XXXXXX"_s);
    const int fd = ::mkostemp(temp.data(), O_CLOEXEC);
    if (fd < 0) {
        qWarning("%s: cannot create a temp file: %s", qPrintable(path), strerror(errno));
        return;
    }
    bool ok = ::fchmod(fd, 0644) == 0 && writeData(fd, data);
    ok = ::close(fd) == 0 && ok;
    ok = ok && ::rename(temp.constData(), QFile::encodeName(path).constData()) == 0;
    if (!ok) {
        qWarning("%s: write failed: %s", qPrintable(path), strerror(errno));
        ::unlink(temp.constData());
        return;
    }
    m_written.insert(name, data);
}

void ProtonApplier::remove(const QString &name)
{
    if (::unlink(QFile::encodeName(m_dir + u'/' + name).constData()) != 0 && errno != ENOENT) {
        qWarning("%s/%s: cannot remove: %s", qPrintable(m_dir), qPrintable(name), strerror(errno));
        return;
    }
    m_written.remove(name);
}
