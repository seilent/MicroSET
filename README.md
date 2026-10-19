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
- `microset-settings --overlay`: the settings page as a gamescope overlay in the steam, microdap and microemu sessions
  (`microset-overlay@<session>.service`). RC toggles it (`hotkeys.d/<session>.conf.d/60-microset.conf` rows for MicroOS
  hotkeyd), B on the first page closes it. D-Bus `net.seilent.MicroSet.Overlay` on the session bus, object
  `/net/seilent/MicroSet/Overlay`, interface `net.seilent.MicroSet.Overlay1` with `Toggle`, `Show`, `Hide` and the
  `Shown` property; refused from a game. While shown it holds the pad through the MicroOS intercept arbiter, and it
  gives way to another overlay that takes the arbiter until it is on top again. Shows "Hold A to format" while
  `microos-storage format` waits for the hold.
- `MicroSet` QML module: MicroDAP's tokens and components, shared by `microset-recovery` and `microset-settings`. The
  page (`SettingsView`) lists the schema groups and edits their keys over `net.seilent.MicroSet`.
- Schema files in `/usr/share/microset/schema.d/`. `10-microset.json` holds the device and system keys, every other
  file only keys under its owner name (`microdap.*`, `microemu.*`). A key can carry `ui` hints for the page (`order`,
  `widget` `none` or `hue`, `step` or a `steps` list (a value off the list steps to the next entry in the step's
  direction), `values` labels, `unit`, `scale` to show the value divided by it,
  `apply` `confirm` to apply on A, and a `lock` that makes it read only while another key has a given value) and a file
  can name and order its groups under `groups`; the first file that describes a group wins.
- A range key can carry a `floor` inside its range: the lowest level an applier restores and a page offers, while `Set`
  still takes the whole range. `"store": "coalesce"` keeps a key's new value in memory and writes the state file 1 s
  after the last change (`MICROSET_COALESCE_MS` in tests), on stop and before sleep, and 10 times that after a failed
  write; `Get`, `Stored` and `Changed` see it at once, and `Import` treats it as stored.

`Get`, `List` and `Changed` give the effective value; `Stored` gives only the keys stored under a prefix, with their
stored values. `Import` stores every key that has no stored value yet in one write and returns the keys it took and the
refused ones with a reason; stored keys are left as they are. `Set`, `Import`, `Override` and `ClearOverride` need uid
0 or `wheel`, and are refused from a game.

Keys marked `"override": true` (`rgb.enabled`, `rgb.hue`, `rgb.saturation`, `display.brightness`) take an
`Override`: one per caller bus name per key, the newest wins, dropped on `ClearOverride` or when the caller leaves the bus. Overrides live in memory;
`libmicroset` sends them again when `microsetd` restarts. Under an override `Set` still stores its value, and it shows
once the override is gone.

`as` keys can set `max_items` and a `validator`. `eq_filters` takes `preamp <dB>` (-24 to 0) first, then up to 10
`<type> <Hz> <dB> <Q>` with type `peak`, `lowshelf`, `highshelf`, `bandpass`, `lowpass`, `highpass`, `allpass` or
`matchedpeak`, 20 to 20000 Hz, -24 to 12 dB, Q 0.1 to 10. `audio.eq.<slot>.on`, `.name` and `.filters` and
`audio.balance.<slot>` (-20 to 20 half dB, negative toward L) exist for `headphones`, `speaker`, `usb` and `bt`;
`audio.crossfeed.headphones` and `audio.crossfeed.usb` are `off`, `default`, `chuMoy` or `janMeier`.
`audio.max_volume` (50 to 100 % in steps of 5) caps every sink volume: at the default 100 a level above 100 % folds to
100 %, and bit perfect output needs 100. `audio.power_on_volume` (0 keeps the last level, else 10 to 100 % in steps of
5) is set once per boot on the default output. `audio.bt_codec` is `auto`, `aptx`, `aac`, `ldac` or `sbc`.

Power settings are applied by MicroOS `microos-powerd`; `microsetd` pushes the stored values to it and records the
changes Steam makes there.

Headphone gain, headphone amp mode and speaker gain go straight to the ALSA controls of card `PocketMICRO2`.
`microsetd` writes them at start, on every change and 5 s after the card appears, and writes them again when a readback
differs.

`display.rotation` is live. While a caller holds it with `Subscribe` and `display.auto_rotate` is on, `microsetd`
streams the SSC `device_orient` sensor over QRTR and follows it; otherwise it equals `display.orientation`. The stream
stops before suspend and starts again after.

`display.brightness` is basis points of the panel's `max_brightness` (1 to 10000, shown as %); `microsetd` writes it
to `brightness` in `MICROSET_BACKLIGHT` (`/sys/class/backlight/backlight`). A `Set` with no override held writes the
node first and is stored only if the write works. A level another process writes is recorded, a 0 is written over
with the current level. At boot a stored level under the `floor` (10 %) is written and stored as the floor; a restart
(marker in `RUNTIME_DIRECTORY`) writes the stored level as is. With nothing stored the current level is recorded.

`rgb.enabled`, `rgb.hue`, `rgb.saturation` and `rgb.brightness` (% of the screen brightness) go to the stick LED MCU
on `/dev/ttyHS2`. The LEDs follow the screen brightness and are off during suspend.

## Build

Qt 6.11 (Core, DBus; Gui, Qml, Quick and xcb for the QML module, `microset-recovery` and `microset-settings`, off
with `-DMICROSET_UI=OFF -DMICROSET_RECOVERY=OFF`), alsa-lib, CMake 3.22, a C++20 compiler.
`-DMICROSET_TEST_FONT_DIR=<dir>` with the Selawik faces adds the screenshot tests; the recovery shots must match
`recovery/tests/shots.sha256`.

```
cmake -B build -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
ctest --test-dir build --output-on-failure
```

The tests start a private `dbus-daemon` and run callers through `setpriv` as `nobody`, so they need `dbus-daemon`,
`setpriv` and root.

## License

GPL-2.0-or-later, see [LICENSE](LICENSE).
