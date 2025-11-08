"""Main window for the GnomeDDC application."""

from __future__ import annotations

import base64
from typing import Dict

import gi

for namespace, version in ("Adw", "1"), ("Gtk", "4"):
    try:
        gi.require_version(namespace, version)
    except ValueError:
        # The namespace may have been initialised elsewhere; ignore the error
        # as long as the required version is already loaded.
        pass

from gi.repository import Adw, Gio, GLib, Gtk

from ..client import DdcutilClient
from ..monitor_store import MonitorState, MonitorStore
from .capability_widgets import build_control


class MonitorSidebarRow(Adw.ActionRow):
    def __init__(self, state: MonitorState) -> None:
        super().__init__()
        self.state = state
        self.set_title(state.descriptor.display_label)
        self.set_subtitle(f"EDID {state.descriptor.short_edid}")


class MonitorPage(Adw.NavigationPage):
    def __init__(self, store: MonitorStore, state: MonitorState) -> None:
        super().__init__(title=state.descriptor.display_label)
        self.store = store
        self.state = state
        self._controls: Dict[int, Gtk.Widget] = {}

        self._store_handler = self.store.connect("vcp-updated", self._on_vcp_updated)
        self._monitor_handler = self.store.connect("monitors-changed", self._on_monitors_changed)

        scroll = Gtk.ScrolledWindow()
        scroll.set_policy(Gtk.PolicyType.NEVER, Gtk.PolicyType.AUTOMATIC)
        scroll.set_hexpand(True)
        scroll.set_vexpand(True)
        self.set_child(scroll)

        self._content = Gtk.Box(
            orientation=Gtk.Orientation.VERTICAL,
            spacing=12,
            margin_top=12,
            margin_bottom=12,
            margin_start=12,
            margin_end=12,
        )
        scroll.set_child(self._content)

        self._rebuild_controls()

    def do_dispose(self) -> None:  # type: ignore[override]
        if self._store_handler:
            self.store.disconnect(self._store_handler)
            self._store_handler = 0
        if self._monitor_handler:
            self.store.disconnect(self._monitor_handler)
            self._monitor_handler = 0
        return super().do_dispose()

    def _rebuild_controls(self) -> None:
        child = self._content.get_first_child()
        while child:
            next_child = child.get_next_sibling()
            self._content.remove(child)
            child = next_child
        self._controls.clear()

        self._content.append(self._build_overview_card())

        caps = sorted(self.state.capabilities.values(), key=lambda c: c.code)
        for metadata in caps:
            value = self.state.values.get(metadata.code)
            if not value:
                continue
            control = build_control(self.store, self.state, value)
            control.add_css_class("card")
            self._controls[metadata.code] = control
            self._content.append(control)

    def _build_overview_card(self) -> Gtk.Widget:
        card = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6, margin_top=6, margin_bottom=6, margin_start=6, margin_end=6)
        card.add_css_class("card")

        title = Gtk.Label(xalign=0)
        escaped_title = GLib.markup_escape_text(self.state.descriptor.display_label)
        title.set_markup(f"<b>{escaped_title}</b>")
        card.append(title)

        grid = Gtk.Grid(column_spacing=12, row_spacing=6)
        card.append(grid)

        def add_row(row: int, label: str, value: str) -> None:
            key_label = Gtk.Label(label=f"{label}:", xalign=1)
            key_label.add_css_class("dim-label")
            grid.attach(key_label, 0, row, 1, 1)
            value_label = Gtk.Label(label=value, xalign=0)
            value_label.set_selectable(True)
            grid.attach(value_label, 1, row, 1, 1)

        descriptor = self.state.descriptor
        add_row(0, "Manufacturer", descriptor.manufacturer or "—")
        add_row(1, "Model", descriptor.model or "—")
        add_row(2, "Display", str(descriptor.display_number))
        if descriptor.bus:
            add_row(3, "Bus", descriptor.bus)
        if descriptor.address:
            add_row(4, "Address", descriptor.address)

        edid_frame = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
        edid_label = Gtk.Label(label="EDID (base64)", xalign=0)
        edid_frame.append(edid_label)
        edid_buffer = Gtk.TextBuffer()
        edid_buffer.set_text(self.state.descriptor.edid)
        edid_view = Gtk.TextView.new_with_buffer(edid_buffer)
        edid_view.set_editable(False)
        edid_view.set_monospace(True)
        edid_view.set_wrap_mode(Gtk.WrapMode.CHAR)
        edid_view.set_size_request(-1, 120)
        edid_frame.append(edid_view)

        summary = Gtk.Label(label=self._format_edid_summary(), xalign=0)
        summary.add_css_class("dim-label")
        edid_frame.append(summary)

        card.append(edid_frame)
        return card

    def _format_edid_summary(self) -> str:
        try:
            raw = base64.b64decode(self.state.descriptor.edid)
        except Exception:
            return "Invalid EDID"
        if len(raw) < 10:
            return "Short EDID"
        manufacturer_id = raw[8:10].hex().upper()
        product_code = raw[10:12].hex().upper()
        serial = raw[12:16].hex().upper()
        return f"Manufacturer ID: {manufacturer_id} · Product: {product_code} · Serial: {serial}"

    def _on_vcp_updated(self, _store: MonitorStore, display_number: int, code: int) -> None:
        if self.state.descriptor.display_number != display_number:
            return
        control = self._controls.get(code)
        if control is None:
            self._rebuild_controls()
            return
        value = self.state.values.get(code)
        if not value:
            return
        if hasattr(control, "update_value"):
            control.update_value(value.current, value.maximum)

    def _on_monitors_changed(self, store: MonitorStore) -> None:
        state = store.get(self.state.descriptor.edid)
        if state:
            self.state = state
            self._rebuild_controls()


class MainWindow(Adw.ApplicationWindow):
    """The primary window hosting monitor navigation and controls."""

    def __init__(self, app: Adw.Application, client: DdcutilClient, store: MonitorStore) -> None:
        super().__init__(application=app, title="GnomeDDC")
        self.set_default_size(1100, 740)

        self._client = client
        self._store = store
        self._pages: Dict[str, MonitorPage] = {}

        self._store.connect("monitors-changed", self._on_monitors_changed)
        self._store.connect("error", self._on_error)

        self._toast_overlay = Adw.ToastOverlay()
        self.set_content(self._toast_overlay)

        split_view = Adw.NavigationSplitView()
        self._toast_overlay.set_child(split_view)

        self._sidebar = Gtk.ListBox()
        self._sidebar.add_css_class("navigation-sidebar")
        self._sidebar.connect("row-selected", self._on_row_selected)
        sidebar_clamp = Adw.Clamp()
        sidebar_clamp.set_child(self._sidebar)
        sidebar_page = Adw.NavigationPage(title="Monitors")
        sidebar_page.set_child(sidebar_clamp)
        split_view.set_sidebar(sidebar_page)

        self._stack = Adw.NavigationView()
        split_view.set_content(self._stack)

        header_bar = Adw.HeaderBar()
        self.set_titlebar(header_bar)

        self._rescan_button = Gtk.Button.new_with_label("Rescan")
        self._rescan_button.connect("clicked", self._on_rescan_clicked)
        header_bar.pack_start(self._rescan_button)

        self._raw_toggle = Gtk.ToggleButton(label="Raw values")
        self._raw_toggle.connect("toggled", self._on_raw_toggled)
        header_bar.pack_end(self._raw_toggle)

        self._verify_toggle = Gtk.ToggleButton(label="Skip verify")
        self._verify_toggle.connect("toggled", self._on_verify_toggled)
        header_bar.pack_end(self._verify_toggle)

        self._on_monitors_changed(store)

    # ------------------------------------------------------------------
    # UI helpers
    # ------------------------------------------------------------------
    def _build_sidebar(self) -> None:
        row = self._sidebar.get_first_child()
        while row:
            next_row = row.get_next_sibling()
            self._sidebar.remove(row)
            row = next_row
        for state in self._store.monitors():
            row = MonitorSidebarRow(state)
            row.set_activatable(True)
            self._sidebar.append(row)
            if state.descriptor.edid not in self._pages:
                self._pages[state.descriptor.edid] = MonitorPage(self._store, state)
        if self._sidebar.get_first_child():
            self._sidebar.select_row(self._sidebar.get_row_at_index(0))

    def _show_page(self, state: MonitorState) -> None:
        page = self._pages.get(state.descriptor.edid)
        if not page:
            page = MonitorPage(self._store, state)
            self._pages[state.descriptor.edid] = page
        if page.get_parent() is None:
            self._stack.push(page)
        else:
            self._stack.present(page)

    # ------------------------------------------------------------------
    # Event handlers
    # ------------------------------------------------------------------
    def _on_monitors_changed(self, _store: MonitorStore) -> None:
        self._pages.clear()
        pages = self._stack.get_pages()
        to_remove = [pages.get_object(index) for index in range(pages.get_n_items())]
        for page in to_remove:
            if page:
                self._stack.remove(page)
        self._build_sidebar()

    def _on_row_selected(self, _listbox: Gtk.ListBox, row: Gtk.ListBoxRow) -> None:
        if not row:
            return
        adw_row = row.get_child()
        if isinstance(adw_row, MonitorSidebarRow):
            self._show_page(adw_row.state)

    def _on_rescan_clicked(self, _button: Gtk.Button) -> None:
        self._store.refresh()

    def _on_raw_toggled(self, button: Gtk.ToggleButton) -> None:
        self._client.raw_values = button.get_active()
        self._store.refresh()

    def _on_verify_toggled(self, button: Gtk.ToggleButton) -> None:
        self._client.no_verify = button.get_active()

    def _on_error(self, _store: MonitorStore, message: str) -> None:
        toast = Adw.Toast.new(message)
        toast.set_timeout(5)
        self._toast_overlay.add_toast(toast)

