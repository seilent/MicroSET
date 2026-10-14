# MicroSET

Settings for MicroOS, MicroDAP and MicroEMU. `microsetd` stores every setting and applies the device ones; the apps
read, change and watch them over D-Bus.

- `microsetd`: system service on `net.seilent.MicroSet`, object `/net/seilent/MicroSet`, interface
  `net.seilent.MicroSet1` (`data/net.seilent.MicroSet1.xml`). State in `/var/lib/microset/settings.json`.
- `libmicroset`: Qt client `MicroSet::Client`, CMake package `MicroSet`.
- `microset`: CLI with `get`, `set`, `list`, `schema` and `watch`.
- Schema files in `/usr/share/microset/schema.d/`. `10-microset.json` holds the device and system keys, every other
  file only keys under its owner name (`microdap.*`, `microemu.*`).

Power settings are applied by MicroOS `microos-powerd`; `microsetd` pushes the stored values to it and records the
changes Steam makes there.

## Build

Qt 6.11 (Core, DBus), CMake 3.22, a C++20 compiler.

```
cmake -B build -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
ctest --test-dir build --output-on-failure
```

The tests start a private `dbus-daemon` and run callers through `setpriv` as `nobody`, so they need `dbus-daemon`,
`setpriv` and root.

## License

GPL-2.0-or-later, see [LICENSE](LICENSE).
