# MicroSET

Settings for MicroOS, MicroDAP and MicroEMU. `microsetd` stores every setting and applies the device ones; the apps
read, change and watch them over D-Bus.

- `microsetd`: system service on `net.seilent.MicroSet`, object `/net/seilent/MicroSet`, interface
  `net.seilent.MicroSet1` (`data/net.seilent.MicroSet1.xml`). State in `/var/lib/microset/settings.json`.
- `libmicroset`: Qt client `MicroSet::Client`, CMake package `MicroSet`.
- `microset`: CLI with `get`, `set`, `list`, `stored`, `schema` and `watch`. `as` values are JSON arrays
  (`microset set audio.eq.usb.filters '["preamp -3","peak 1000 -2 1.41"]'`).
- `microset-recovery`: Qt Quick screen of the MicroOS recovery session. Shows the failing session and the last 40
  lines of its journal; tries that session again, switches to Steam, boots the previous image (hold A) or powers off.
  Pad input comes from InputPlumber's D-Bus target while the MicroOS intercept arbiter (`net.seilent.MicroOs.Intercept`)
  holds it for the app; arrow keys, Return and Escape work too. `--screenshot <png> --state <name>` renders a fixture
  (960x640 by default). Look and components from MicroDAP, font Selawik (OFL 1.1, `MICROSET_FONT_DIR`).
- Schema files in `/usr/share/microset/schema.d/`. `10-microset.json` holds the device and system keys, every other
  file only keys under its owner name (`microdap.*`, `microemu.*`).

`Get`, `List` and `Changed` give the effective value; `Stored` gives only the keys stored under a prefix, with their
stored values. `Import` stores every key that has no stored value yet in one write and returns the keys it took and the
refused ones with a reason; stored keys are left as they are. `Set`, `Import`, `Override` and `ClearOverride` need uid
0 or `wheel`, and are refused from a game.

Keys marked `"override": true` (`rgb.*`) take an `Override`: one per caller bus name per key, the newest wins, dropped
on `ClearOverride` or when the caller leaves the bus. Overrides live in memory; `libmicroset` sends them again when
`microsetd` restarts. Under an override `Set` still stores its value, and it shows once the override is gone.

`as` keys can set `max_items` and a `validator`. `eq_filters` takes `preamp <dB>` (-24 to 0) first, then up to 10
`<type> <Hz> <dB> <Q>` with type `peak`, `lowshelf`, `highshelf`, `bandpass`, `lowpass`, `highpass` or `allpass`, 20 to
20000 Hz, -24 to 12 dB, Q 0.1 to 10. `audio.eq.<slot>.on`, `.name` and `.filters` exist for `headphones`, `speaker`,
`usb` and `bt`; `audio.bt_codec` is `auto`, `aptx`, `aac`, `ldac` or `sbc`.

Power settings are applied by MicroOS `microos-powerd`; `microsetd` pushes the stored values to it and records the
changes Steam makes there.

Headphone gain, headphone amp mode and speaker gain go straight to the ALSA controls of card `PocketMICRO2`.
`microsetd` writes them at start, on every change and 5 s after the card appears, and writes them again when a readback
differs.

`display.rotation` is live. While a caller holds it with `Subscribe` and `display.auto_rotate` is on, `microsetd`
streams the SSC `device_orient` sensor over QRTR and follows it; otherwise it equals `display.orientation`. The stream
stops before suspend and starts again after.

`rgb.enabled`, `rgb.color` (`0xRRGGBB`) and `rgb.brightness` (0 to 255) go to the stick LED MCU on `/dev/ttyHS2`. The
LEDs are off during suspend.

## Build

Qt 6.11 (Core, DBus; Gui, Qml, Quick for `microset-recovery`, off with `-DMICROSET_RECOVERY=OFF`), alsa-lib, CMake
3.22, a C++20 compiler. `-DMICROSET_TEST_FONT_DIR=<dir>` with the Selawik faces adds the screenshot tests.

```
cmake -B build -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
ctest --test-dir build --output-on-failure
```

The tests start a private `dbus-daemon` and run callers through `setpriv` as `nobody`, so they need `dbus-daemon`,
`setpriv` and root.

## License

GPL-2.0-or-later, see [LICENSE](LICENSE).
