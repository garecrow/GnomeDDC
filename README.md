# GnomeDDC

GnomeDDC is a native Libadwaita interface for [ddcutil](https://www.ddcutil.com/) that
lets you control external monitor settings such as brightness, contrast, and input
sources directly from GNOME. The application is written with GTK 4 / Libadwaita and
is designed to run seamlessly on Wayland systems with an X11 fallback, making it an
excellent fit for Fedora 41–43.

## Features

- Discover all DDC/CI capable displays using `ddcutil`
- Adjust brightness and contrast with smooth, Adwaita-styled sliders
- Switch between available input sources when supported by the monitor
- Responsive, asynchronous UI that avoids blocking the GNOME Shell
- Built with modern Libadwaita widgets and follows GNOME’s Human Interface Guidelines

## Requirements

- Fedora 41 or newer (other Wayland-based distributions should also work)
- Python 3.11+
- `ddcutil` command line tool installed and accessible to the user
- GTK 4, Libadwaita, and PyGObject libraries

## Running from source

```bash
pip install --user .
gnomeddc
```

While running the app you may increase verbosity by passing `-v` (info) or `-vv` (debug).

## Developing

The `gnomeddc` package is organised as a modern Python project that uses GTK 4 and
Libadwaita. All long-running `ddcutil` calls are executed in worker threads so the UI
remains responsive. The project aims to provide a GNOME-first experience compared to
Qt-based alternatives.

