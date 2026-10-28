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
  page (`SettingsView`) lists the schema groups and edits their keys over `net.seilent.MicroSet`: A on a list or
  number row opens a value sheet, on an on/off row flips it. The `games` group holds `fex.preset`, `wine.sync` and
  `proton.profiles`, then under a `games` label one row per installed Steam game and non Steam shortcut, sorted by
  name, with the game's preset from `Effective`. Games come from `steamapps/libraryfolders.vdf`, the
  `appmanifest_<appid>.acf` files of each library and `userdata/<id>/config/shortcuts.vdf` (keyed by the 32 bit
  shortcut id) under `MICROSET_STEAM_DIR` (`~/.local/share/Steam`), read when the page opens, again only once
  `libraryfolders.vdf`, a library's `steamapps` dir, one of its manifests, a `toolmanifest.vdf` in one of its
  `steamapps/common` dirs, `userdata` or a `shortcuts.vdf` changed (inode, size or mtime): regular files of at most
  1 MiB, names of at most 128 characters, at most 2000 manifests read. Apps without a plain `installdir` or with a
  `toolmanifest.vdf` in their install dir, the Steamworks, EAC and BattlEye runtimes and Lossless Scaling are left out.
  A game with stored values and no manifest shows as `not installed`; the runtimes and Lossless Scaling never do. Game
  names in the rows and in the header of the game's page are plain text.
- Schema files in `/usr/share/microset/schema.d/`. `10-microset.json` holds the device and system keys, every other
  file only keys under its owner name (`microdap.*`, `microemu.*`). A key can carry `ui` hints for the page (`order`,
  `widget` `none`, `hue` or `edit` (left/right edit instead of a sheet), `step` or a `steps` list (a value off the list
  steps to the next entry in the step's direction), `values` labels, `unit`, `scale` to show the value divided by it,
  `apply` `confirm` to apply on A, `bounded` to stop the sheet at the ends, and a `lock` that makes it read only while
  another key has a given value; a list or range over 64 values is an edit row) and a file
  can name and order its groups under `groups`; the first file that describes a group wins.
- A range key can carry a `floor` inside its range: the lowest level an applier restores and a page offers, while `Set`
  still takes the whole range. `"store": "coalesce"` keeps a key's new value in memory and writes the state file 1 s
  after the last change (`MICROSET_COALESCE_MS` in tests), on stop and before sleep, and 10 times that after a failed
  write; `Get`, `Stored` and `Changed` see it at once, and `Import` treats it as stored.

`Get`, `List` and `Changed` give the effective value; `Stored` gives only the keys stored under a prefix, with their
stored values. `Import` stores every key that has no stored value yet in one write and returns the keys it took and the
refused ones with a reason; stored keys are left as they are. `Set`, `Import`, `Override` and `ClearOverride` need uid
0 or `wheel`, and are refused from a game.

Per game keys are `app.<appid>.<key>` (appid of 1 to 10 digits, no leading zero) from the `app.*` templates in
`10-microset.json` (`"per_app": true`): `profile` (`on` or `off`), `fex.preset`, `fex.tso`, `fex.half_barrier`,
`fex.vector_tso`, `fex.memcpy_tso`, `fex.multiblock`, `fex.max_inst`, `fex.x87_reduced`, `fex.smc`,
`fex.volatile_metadata`, `fex.mono_hacks` and `wine.sync`, all `auto` by default. `Schema("app.")` lists the
templates, `Schema("app.<appid>")` the keys of one game, `List` and `Stored` the stored ones. A `Set` back to the
default removes the key, at most 512 games hold values (`Failed`, "too many games"), and they take no `Override` or
`Subscribe`. The global keys are `fex.preset` (`default` or `compatibility`), `wine.sync` (`fsync` or `ntsync`) and
`proton.profiles` (`on` or `off`); the FEX presets are under `presets` in `10-microset.json`.

Proton profiles come from `/usr/share/microos/proton/profiles.json` (`MICROSET_PROTON_PROFILES`):
`{"version": 1, "profiles": {"<appid>": {"tool": "<Proton version prefix>", "set": {"<per game key>": "<value>"}}}}`.
A bad entry is dropped with one log line, and `microsetd --check-profiles <file>` exits 1 when it drops anything. A
value counts as set when it differs from its default. Per option the first of these wins: the game's value, the game's
`fex.preset`, the global `fex.preset` (for the options it sets) or `wine.sync`, the profile while `proton.profiles` and
the game's `profile` are `on`, then Proton's own. `Effective(appid)` returns the result as JSON with the source of each
value (`set`, `preset`, `global`, `profile`, `default`), `Profiles()` the loaded profiles.

`microsetd` writes the result to `/run/microset/proton/` (dir 0755, files 0644): `default.json` and `<appid>.json` for
every game with a value or a profile, each by rename and at most 4096 bytes, before it takes its bus name and on every
change. Format version 2: `features.<feature>.env` (with `fallback` for the names a profile set), `below_local` (names
from a profile or a default rule) and `profile` (`names`, `yield`, `tool`), read by the MicroOS Proton hook.

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
5) is set once per boot on the default output. `audio.bt_codec_headphones` (the device streaming to headphones) is
`auto`, `aptx`, `aptx_hd`, `aac`, `ldac` or `sbc`. `audio.bt_codec` (a phone streaming to the device, off the page) is
`auto`, `aptx`, `aac` or `sbc`. A store that holds a non `auto` `audio.bt_codec` valid for `audio.bt_codec_headphones`
(`ldac` included) and no `audio.bt_codec_headphones` gets it moved to `audio.bt_codec_headphones` once at start, in one
write with `meta.moved_bt_codec`; any other stored `audio.bt_codec` outside its values is written back as `auto` at
start. An `Import` with `audio.bt_codec` and no `audio.bt_codec_headphones` takes the value as
`audio.bt_codec_headphones`.

Power settings are applied by MicroOS `microos-powerd`; `microsetd` pushes the stored values to it and records the
changes Steam makes there.

`power.gpu_undervolt` is `off` or a preset from `/usr/lib/microos/gpu-undervolt.conf` whose frequencies match the GPU.
A deeper preset needs A held 1 s: `Set` returns `HoldPending`, a refusal comes as `HoldRefused`. It applies from the
next boot. A GPU hang, a GMU fault or timeout runs the GPU stock for the rest of that boot, and
`power.gpu_tuning_trip` holds the reason until the next boot. powerd turns the preset off after two boots in a row with
one, or after a boot that ended without a clean shutdown, and `power.gpu_tuning_fallback` holds the reason.

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
