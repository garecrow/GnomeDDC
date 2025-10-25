# GnomeDDC

GnomeDDC is a native GTK 4 and Libadwaita application for Fedora that presents a
simple control panel for DDC/CI capable monitors using the `ddcutil` command line
tool. The project is written in C and built with CMake to align with Fedora's
packaging guidelines.

## Features

- Discover DDC/CI monitors through `ddcutil detect --brief`
- Display per-monitor metadata such as I²C bus and serial number
- Adjust brightness using a Libadwaita-styled slider
- Refresh the display list with a single toolbar action

## Build requirements

### Fedora

The following packages are required to build and run GnomeDDC on Fedora:

- `cmake`
- `gcc`
- `pkgconf-pkg-config`
- `gtk4-devel`
- `libadwaita-devel`
- `ddcutil` (runtime dependency)

### Debian / Ubuntu

The project also builds cleanly on Debian-based distributions with the
following packages installed:

- `build-essential`
- `cmake`
- `pkg-config`
- `libgtk-4-dev`
- `libadwaita-1-dev`
- `libddcutil-dev`

## Building and running

```bash
cmake -S . -B build
cmake --build build
make -C build
./build/gnomeddc
```

On systems without an available display server (such as continuous
integration runners) you can still verify the binary launches by running:

```bash
./build/gnomeddc --help
```

GnomeDDC communicates with `ddcutil`, so make sure your user is allowed to access
`/dev/i2c-*` devices. On Fedora this typically means adding your user to the
`i2c` group and re-logging.

## Project structure

- `CMakeLists.txt` — project configuration
- `src/main.c` — Libadwaita application and interface logic
- `src/ddcutil_client.c` — helpers that wrap `ddcutil` CLI calls
- `src/monitor_item.c` — boxed GObject used by the list model

Contributions are welcome! Please follow GNOME's C formatting guidelines and keep
new UI elements consistent with the rest of the application.
