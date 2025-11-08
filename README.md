# GnomeDDC UI

A Libadwaita GTK4 desktop frontend for [ddcutil-service](https://github.com/rockowitz/ddcutil/tree/master/service),
implementing an adaptive monitor control panel driven entirely by the service's
D-Bus capabilities.

## Features

* Automatic discovery of connected displays via `Detect` with a manual rescan
  action.
* Live sidebar of monitors keyed by EDID, updated through
  `ConnectedDisplaysChanged` and `ListDetected`.
* Dynamic per-monitor page that renders sliders or enumerated selectors based on
  `GetCapabilitiesMetadata` and `GetMultipleVcp` results.
* Real-time application of control updates with cross-client context tagging via
  `SetVcpWithContext`.
* Signal handling for `VcpValueChanged` so UI components update instantly when
  values change from any client.
* Advanced toggles to request raw values and skip write verification using the
  service flag bits.
* Toast overlay for surfacing errors returned by libddcutil.

## Running

The application requires GNOME platform dependencies and access to the system
D-Bus.  Install Python dependencies and run the console entry point:

```bash
pip install --user -e .
gnomeddc
```

When the ddcutil-service is unavailable the application remains responsive and
surfaces errors via toasts so the user can diagnose connectivity or permission
issues.
