#include <microset/client.h>

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

using namespace Qt::StringLiterals;

namespace {

enum Exit { Ok = 0, Other = 1, UnknownKey = 2, InvalidValue = 3, AccessDenied = 4, NoDaemon = 5 };

QTextStream &out()
{
    static QTextStream stream(stdout);
    return stream;
}

QTextStream &err()
{
    static QTextStream stream(stderr);
    return stream;
}

int usage()
{
    err() << "usage: microset get <key> | set <key> <value> | list [prefix] | schema [prefix] | watch [prefix]\n";
    return Other;
}

int fail(const MicroSet::Error &error, const MicroSet::Client &client)
{
    err() << "microset: " << (error.message.isEmpty() ? error.name : error.message) << Qt::endl;
    if (error.name == "net.seilent.MicroSet1.Error.UnknownKey"_L1)
        return UnknownKey;
    if (error.name == "net.seilent.MicroSet1.Error.InvalidValue"_L1 || error.name == "net.seilent.MicroSet1.Error.ReadOnly"_L1)
        return InvalidValue;
    if (error.name == "org.freedesktop.DBus.Error.AccessDenied"_L1)
        return AccessDenied;
    return client.available() ? Other : NoDaemon;
}

QString format(const QVariant &value)
{
    if (value.typeId() == QMetaType::QStringList)
        return value.toStringList().join(u',');
    if (value.typeId() == QMetaType::Bool)
        return value.toBool() ? u"true"_s : u"false"_s;
    return value.toString();
}

QVariant parse(const QString &type, const QString &text)
{
    bool ok = false;
    if (type == "s"_L1)
        return text;
    if (type == "b"_L1)
        return text == "true"_L1 ? QVariant(true) : text == "false"_L1 ? QVariant(false) : QVariant();
    if (type == "as"_L1)
        return text.isEmpty() ? QStringList() : text.split(u',');
    if (type == "u"_L1) {
        const uint n = text.toUInt(&ok);
        return ok ? QVariant(n) : QVariant();
    }
    if (type == "i"_L1) {
        const int n = text.toInt(&ok);
        return ok ? QVariant(n) : QVariant();
    }
    if (type == "d"_L1) {
        const double n = text.toDouble(&ok);
        return ok ? QVariant(n) : QVariant();
    }
    return {};
}

bool matches(const QString &key, const QString &prefix)
{
    if (prefix.isEmpty() || prefix.endsWith(u'.'))
        return key.startsWith(prefix);
    return key == prefix || key.startsWith(prefix + u'.');
}

}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QStringList args = app.arguments().mid(1);
    if (args.isEmpty())
        return usage();
    const QString verb = args.takeFirst();
    const bool known = (verb == "get"_L1 && args.size() == 1) || (verb == "set"_L1 && args.size() == 2)
        || ((verb == "list"_L1 || verb == "schema"_L1 || verb == "watch"_L1) && args.size() <= 1);
    if (!known)
        return usage();
    const QString key = args.value(0);

    MicroSet::Client client;
    if (!client.available()) {
        err() << "microset: microsetd is not running\n";
        return NoDaemon;
    }
    MicroSet::Error error;

    if (verb == "get"_L1) {
        const QVariant value = client.get(key, &error);
        if (!error.ok())
            return fail(error, client);
        out() << format(value) << Qt::endl;
        return Ok;
    }
    if (verb == "list"_L1) {
        const QVariantMap values = client.list(key, &error);
        if (!error.ok())
            return fail(error, client);
        for (auto it = values.cbegin(); it != values.cend(); ++it)
            out() << it.key() << '\t' << format(it.value()) << '\n';
        out().flush();
        return Ok;
    }
    if (verb == "schema"_L1) {
        const QString json = client.schema(key, &error);
        if (!error.ok())
            return fail(error, client);
        out() << QJsonDocument::fromJson(json.toUtf8()).toJson(QJsonDocument::Indented);
        out().flush();
        return Ok;
    }
    if (verb == "watch"_L1) {
        QObject::connect(&client, &MicroSet::Client::changed, &app, [key](const QString &name, const QVariant &value) {
            if (matches(name, key))
                out() << name << '\t' << format(value) << Qt::endl;
        });
        const QJsonObject keys = QJsonDocument::fromJson(client.schema(key, &error).toUtf8()).object();
        if (!error.ok())
            return fail(error, client);
        const QVariantMap values = client.list(key, &error);
        if (!error.ok())
            return fail(error, client);
        for (auto it = keys.constBegin(); it != keys.constEnd(); ++it) {
            if (it.value().toObject().value("live"_L1).toBool())
                client.subscribe(it.key());
        }
        for (auto it = values.cbegin(); it != values.cend(); ++it)
            out() << it.key() << '\t' << format(it.value()) << '\n';
        out().flush();
        return app.exec();
    }

    const QJsonObject entry = QJsonDocument::fromJson(client.schema(key, &error).toUtf8()).object().value(key).toObject();
    if (!error.ok())
        return fail(error, client);
    if (entry.isEmpty()) {
        err() << "microset: no key " << key << Qt::endl;
        return UnknownKey;
    }
    const QString type = entry.value("type"_L1).toString();
    const QVariant value = parse(type, args.value(1));
    if (!value.isValid()) {
        err() << "microset: " << args.value(1) << " is not a value of type " << type << Qt::endl;
        return InvalidValue;
    }
    int code = Ok;
    client.set(key, value, [&](const MicroSet::Error &result) {
        code = result.ok() ? Ok : fail(result, client);
        app.quit();
    });
    app.exec();
    return code;
}
