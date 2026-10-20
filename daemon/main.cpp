#include "alsaapplier.h"
#include "alsalibcontrol.h"
#include "auth.h"
#include "backlightapplier.h"
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

#include <QCoreApplication>
#include <QDBusConnection>
#include <QSocketNotifier>

#include <signal.h>
#include <sys/signalfd.h>
#include <unistd.h>

using namespace Qt::StringLiterals;

int main(int argc, char **argv)
{
    sigset_t stop;
    sigemptyset(&stop);
    sigaddset(&stop, SIGTERM);
    sigaddset(&stop, SIGINT);
    if (sigprocmask(SIG_BLOCK, &stop, nullptr) != 0) {
        qCritical("cannot block SIGTERM and SIGINT");
        return 1;
    }
    const int stopFd = signalfd(-1, &stop, SFD_CLOEXEC | SFD_NONBLOCK);
    if (stopFd < 0) {
        qCritical("cannot open a signalfd");
        return 1;
    }
    QCoreApplication app(argc, argv);
    QSocketNotifier stopNotifier(stopFd, QSocketNotifier::Read);
    QObject::connect(&stopNotifier, &QSocketNotifier::activated, &app, [stopFd] {
        signalfd_siginfo info;
        while (::read(stopFd, &info, sizeof info) == sizeof info)
            ;
        QCoreApplication::quit();
    });

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
    schema.addApplier(u"backlight"_s);
    schema.addProvider(u"sessions"_s, [&sessions] { return sessions.defaults(); });
    schema.load(qEnvironmentVariable("MICROSET_SCHEMA_DIR", QStringLiteral(MICROSET_SCHEMA_DIR)));

    Store store(qEnvironmentVariable("STATE_DIRECTORY", u"/var/lib/microset"_s));
    store.load();
    Settings settings(schema, store);
    bool delayOk = false;
    const int delay = qEnvironmentVariableIntValue("MICROSET_COALESCE_MS", &delayOk);
    if (delayOk && delay > 0)
        settings.setCoalesceDelay(delay);
    PowerBridge::importState(qEnvironmentVariable("MICROSET_POWER_CONF", u"/var/lib/microos/power.conf"_s), settings);
    settings.moveBtCodec();

    PowerBridge bridge(bus, settings);
    Auth auth(bus, qEnvironmentVariable("MICROOS_PROC", u"/proc"_s));
    const QString backlight = qEnvironmentVariable("MICROSET_BACKLIGHT", u"/sys/class/backlight/backlight"_s);
    BacklightApplier light(settings, backlight, qEnvironmentVariable("RUNTIME_DIRECTORY", u"/run/microset"_s));
    light.start();
    Service service(bus, settings, auth, bridge, light);
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
    RgbApplier rgb(settings, &sleep, qEnvironmentVariable("MICROSET_RGB_DEVICE", u"/dev/ttyHS2"_s), backlight);
    rgb.start();
    QObject::connect(&sleep, &SleepInhibitor::prepareForSleep, &settings, [&settings](bool start) {
        if (start)
            settings.flush();
    });
    sleep.take();
    const int code = app.exec();
    settings.flush();
    return code;
}
