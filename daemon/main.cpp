#include "alsaapplier.h"
#include "alsalibcontrol.h"
#include "auth.h"
#include "powerbridge.h"
#include "qrtrlink.h"
#include "rgbapplier.h"
#include "schema.h"
#include "service.h"
#include "sessionsprovider.h"
#include "settings.h"
#include "sleepinhibitor.h"
#include "sscapplier.h"
#include "sscrotation.h"
#include "store.h"
#include "undervoltsprovider.h"

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
    schema.addApplier(u"ssc"_s);
    schema.addApplier(u"rgb"_s);
    UndervoltsProvider undervolts(
        qEnvironmentVariable("MICROSET_GPU_UNDERVOLTS", u"/usr/lib/microos/gpu-undervolt.conf"_s),
        qEnvironmentVariable("MICROSET_GPU_FREQUENCIES", u"/sys/class/devfreq/3d00000.gpu/available_frequencies"_s));
    schema.addProvider(u"sessions"_s, [&sessions] { return sessions.defaults(); });
    schema.addProvider(u"gpu_undervolts"_s, [&undervolts] { return undervolts.names(); },
                       [&undervolts] { return undervolts.labels(); });
    schema.load(qEnvironmentVariable("MICROSET_SCHEMA_DIR", QStringLiteral(MICROSET_SCHEMA_DIR)));

    Store store(qEnvironmentVariable("STATE_DIRECTORY", u"/var/lib/microset"_s));
    store.load();
    Settings settings(schema, store);
    PowerBridge::importState(qEnvironmentVariable("MICROSET_POWER_CONF", u"/var/lib/microos/power.conf"_s), settings);

    PowerBridge bridge(bus, settings, undervolts);
    Auth auth(bus, qEnvironmentVariable("MICROOS_PROC", u"/proc"_s));
    Service service(bus, settings, auth, bridge);
    service.addLive(u"display.rotation"_s);
    SleepInhibitor sleep(bus, u"stop the rotation sensor and the stick LEDs"_s);
    SscRotation rotation(new QrtrLink, &sleep);
    SscApplier ssc(settings, rotation);
    QObject::connect(&service, &Service::subscribersChanged, &ssc, [&ssc](const QString &key, bool held) {
        if (key == "display.rotation"_L1)
            ssc.setSubscribed(held);
    });
    ssc.start();
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
    RgbApplier rgb(settings, &sleep, qEnvironmentVariable("MICROSET_RGB_DEVICE", u"/dev/ttyHS2"_s),
                   qEnvironmentVariable("MICROSET_BACKLIGHT", u"/sys/class/backlight/backlight"_s));
    rgb.start();
    sleep.take();
    return app.exec();
}
