#include "alsaapplier.h"
#include "alsalibcontrol.h"
#include "auth.h"
#include "powerbridge.h"
#include "schema.h"
#include "service.h"
#include "sessionsprovider.h"
#include "settings.h"
#include "store.h"

#include <QCoreApplication>
#include <QDBusConnection>

using namespace Qt::StringLiterals;

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    QDBusConnection bus = QDBusConnection::systemBus();
    if (!bus.isConnected()) {
        qCritical("no system bus: %s", qPrintable(bus.lastError().message()));
        return 1;
    }

    SessionsProvider sessions(qEnvironmentVariable("MICROSET_SESSIONS_ROOT"));
    Schema schema;
    schema.addApplier(u"power"_s);
    schema.addApplier(u"alsa"_s);
    schema.addProvider(u"sessions"_s, [&sessions] { return sessions.defaults(); });
    schema.load(qEnvironmentVariable("MICROSET_SCHEMA_DIR", QStringLiteral(MICROSET_SCHEMA_DIR)));

    Store store(qEnvironmentVariable("STATE_DIRECTORY", u"/var/lib/microset"_s));
    store.load();
    Settings settings(schema, store);
    PowerBridge::importState(qEnvironmentVariable("MICROSET_POWER_CONF", u"/var/lib/microos/power.conf"_s), settings);

    PowerBridge bridge(bus, settings);
    Auth auth(bus, qEnvironmentVariable("MICROOS_PROC", u"/proc"_s));
    Service service(settings, auth, bridge);
    if (!bus.registerObject(u"/net/seilent/MicroSet"_s, &service, QDBusConnection::ExportScriptableContents)) {
        qCritical("cannot export /net/seilent/MicroSet: %s", qPrintable(bus.lastError().message()));
        return 1;
    }
    if (!bus.registerService(u"net.seilent.MicroSet"_s)) {
        qCritical("cannot own net.seilent.MicroSet: %s", qPrintable(bus.lastError().message()));
        return 1;
    }
    bridge.start();
    AlsaLibControl control(qEnvironmentVariable("MICROSET_ALSA_CARD", u"PocketMICRO2"_s));
    AlsaApplier alsa(bus, settings, control, qEnvironmentVariable("MICROSET_ALSA_DEVICES", u"/dev/snd"_s));
    alsa.start();
    return app.exec();
}
