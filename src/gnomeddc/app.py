"""Application entry-point for GnomeDDC."""

from __future__ import annotations

from typing import Optional

from gi.repository import Adw, Gio, GLib

from .client import DdcutilClient
from .monitor_store import MonitorStore
from .ui.main_window import MainWindow


class GnomeDdcApplication(Adw.Application):
    """Libadwaita application that orchestrates the monitor UI."""

    def __init__(self) -> None:
        super().__init__(application_id="dev.gnomeddc.UI", flags=Gio.ApplicationFlags.HANDLES_COMMAND_LINE)
        self._client: Optional[DdcutilClient] = None
        self._store: Optional[MonitorStore] = None
        self.connect("activate", self._on_activate)
        self.connect("command-line", self._on_command_line)

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
    app = GnomeDdcApplication()
    return app.run()


__all__ = ["GnomeDdcApplication", "main"]
