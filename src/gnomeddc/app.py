"""Application entry-point for GnomeDDC."""

from __future__ import annotations

import sys
from typing import Optional

import gi

for namespace, version in (("Adw", "1"), ("Gtk", "4")):
    try:
        gi.require_version(namespace, version)
    except ValueError:
        # Namespace already initialised with a compatible version.
        pass

from gi.repository import Adw, Gio, GLib

from .client import DdcutilClient
from .monitor_store import MonitorStore
from .ui.main_window import MainWindow


class GnomeDdcApplication(Adw.Application):
    """Libadwaita application that orchestrates the monitor UI."""

    def __init__(self) -> None:
        super().__init__(application_id="dev.gnomeddc.UI", flags=Gio.ApplicationFlags.HANDLES_COMMAND_LINE)
        self.add_main_option(
            "version",
            ord("v"),
            GLib.OptionFlags.NONE,
            GLib.OptionArg.NONE,
            "Print application version and exit",
            None,
        )
        self._client: Optional[DdcutilClient] = None
        self._store: Optional[MonitorStore] = None
        self.connect("activate", self._on_activate)
        self.connect("command-line", self._on_command_line)

        Adw.StyleManager.get_default().set_color_scheme(Adw.ColorScheme.FORCE_DARK)

    # ------------------------------------------------------------------
    # Application lifecycle
    # ------------------------------------------------------------------
    def _ensure_client(self) -> None:
        if self._client is None:
            self._client = DdcutilClient()
            self._store = MonitorStore(self._client)

    def _on_activate(self, app: Adw.Application) -> None:
        self._ensure_client()
        assert self._client and self._store
        window = self.props.active_window
        if not window:
            window = MainWindow(self, self._client, self._store)
        window.present()

    def _on_command_line(self, _app: Adw.Application, command_line: Gio.ApplicationCommandLine) -> int:
        options = command_line.get_options_dict()
        if options.contains("version"):
            print("GnomeDDC UI 1.0")
            return 0
        self.activate()
        return 0


def main() -> int:
    argv = sys.argv[:]
    if "--version" in argv[1:] or "-v" in argv[1:]:
        print("GnomeDDC UI 1.0")
        return 0

    app = GnomeDdcApplication()
    return app.run(argv)


__all__ = ["GnomeDdcApplication", "main"]
