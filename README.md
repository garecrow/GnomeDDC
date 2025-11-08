# GnomeDDC

GnomeDDC is a Libadwaita desktop client for [`ddcutil-service`](https://github.com/rockowitz/ddcutil). The
application provides a GNOME-style interface for detecting monitors, exploring their MCCS
capabilities, and adjusting VCP (Virtual Control Panel) values in real time. It is designed to be
packaged as both a Fedora RPM and an AppImage, following the GNOME Human Interface Guidelines and the
Libadwaita 1.x documentation.

## Features

* Automatic detection of connected DDC/CI monitors via `Detect()`.
* Passive refresh using `ListDetected()` when supported.
* Monitor sidebar with model name and EDID hash summary.
* Dynamic control panels that are generated from reported VCP capabilities.
* Advanced toggles for raw value reads and write verification behaviour.
* Service property controls (polling, logging, signal emission, etc.).
* Live updates from `VcpValueChanged`, `ConnectedDisplaysChanged`, and `ServiceInitialized` signals.
* Per-monitor capability summary, EDID view, and attributes table.
* Monitor profiles that can be applied across displays sharing the same EDID.

The implementation draws heavily from the official Libadwaita widgets—`AdwApplication`,
`AdwNavigationSplitView`, `AdwClamp`, `AdwPreferencesPage`, and friends—to deliver a native GNOME 45+
experience.

## Development

Create a virtual environment, install the project in editable mode, and run the application.

```bash
python -m venv .venv
source .venv/bin/activate
pip install --upgrade pip
pip install -e .
G_MESSAGES_DEBUG=all gnomeddc
```

When running inside a development container without access to `ddcutil-service`, the application will
fall back to a mock backend that simulates a trio of monitors. This enables UI iteration without
hardware access.

## Packaging targets

* Fedora RPM: integrate with `%pyproject_*` macros and install the desktop file, icons, and appdata
  provided in the `data/` directory.
* AppImage: bundle the Python runtime with GTK 4, Libadwaita, and the project wheel using tools like
  `linuxdeploy` or `appimage-builder`. The `gnomeddc.desktop` entry point and icon assets are ready for
  inclusion.

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE).
