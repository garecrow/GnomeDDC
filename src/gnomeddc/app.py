"""Application window and UI logic for GnomeDDC."""

from __future__ import annotations

import logging
from typing import Dict, List, Optional, Tuple

import gi

try:  # pragma: no cover - environment check
    gi.require_version("Gtk", "4.0")
    gi.require_version("Adw", "1")
except ValueError as exc:  # pragma: no cover - environment check
    raise SystemExit("Gtk 4 and Libadwaita are required to run GnomeDDC") from exc

from gi.repository import Adw, Gio, GLib, Gtk

from . import __version__
from .ddcutil import (
    DDCError,
    DisplayInfo,
    FeatureValue,
    InputSource,
    list_displays,
    list_input_sources,
    query_feature,
    set_feature,
)
from .worker import AsyncRunner

LOG = logging.getLogger(__name__)

SUPPORTED_FEATURES: Dict[str, Dict[str, str]] = {
    "0x10": {
        "title": "Brightness",
        "subtitle": "Adjust overall luminance",
        "icon": "display-brightness-symbolic",
    },
    "0x12": {
        "title": "Contrast",
        "subtitle": "Adjust the difference between dark and light areas",
        "icon": "contrast-symbolic",
    },
}


class MonitorRow(Adw.ActionRow):
    """Row representing a monitor in the sidebar."""

    def __init__(self, display: DisplayInfo):
        super().__init__(title=display.model or "Unknown display", subtitle=display.manufacturer or "")
        self.display = display
        self.set_icon_name("video-display-symbolic")


class MonitorView(Gtk.Box):
    """Configuration page for a single monitor."""

    __gtype_name__ = "GnomeDDCMonitorView"

    def __init__(self, display: DisplayInfo, runner: AsyncRunner[object]):
        super().__init__(orientation=Gtk.Orientation.VERTICAL, spacing=12)
        self.display = display
        self.runner = runner
        self.set_margin_top(24)
        self.set_margin_bottom(24)
        self.set_margin_start(24)
        self.set_margin_end(24)

        self._feature_rows: Dict[str, SliderRow] = {}

        banner = Adw.Banner()
        banner.set_title(display.model or "Unknown display")
        subtitle_parts: List[str] = []
        if display.manufacturer:
            subtitle_parts.append(display.manufacturer)
        if display.serial:
            subtitle_parts.append(f"SN {display.serial}")
        banner.set_subtitle(" · ".join(subtitle_parts))
        banner.set_button_label("Refresh")
        banner.connect("button-clicked", self._on_refresh_clicked)
        self.append(banner)

        self.content_page = Adw.PreferencesPage()

        picture_group = Adw.PreferencesGroup(title="Picture")
        for code, meta in SUPPORTED_FEATURES.items():
            row = SliderRow(meta["title"], meta["subtitle"], meta["icon"], code, self.on_slider_changed)
            picture_group.add(row)
            self._feature_rows[code] = row
        self.content_page.add(picture_group)

        self.input_row = InputSourceRow(on_change=self.on_input_changed)
        input_group = Adw.PreferencesGroup(title="Input")
        input_group.add(self.input_row)
        self.content_page.add(input_group)

        clamp = Adw.Clamp()
        clamp.set_child(self.content_page)
        self.append(clamp)

        self._load_all()

    # -------------------- Data loading --------------------

    def _on_refresh_clicked(self, *_args) -> None:
        self._load_all()

    def _load_all(self) -> None:
        for row in self._feature_rows.values():
            row.set_loading(True)
        self.input_row.set_loading(True)

        self.runner.run(self._fetch_features, self._on_features_loaded)
        self.runner.run(self._fetch_inputs, self._on_inputs_loaded)

    def _fetch_features(self) -> Dict[str, FeatureValue]:
        data: Dict[str, FeatureValue] = {}
        for code in SUPPORTED_FEATURES.keys():
            try:
                data[code] = query_feature(self.display.bus_number, code)
            except DDCError as exc:
                LOG.warning("Unable to query %s on bus %s: %s", code, self.display.bus_number, exc)
        return data

    def _fetch_inputs(self) -> Tuple[List[InputSource], Optional[int]]:
        sources: List[InputSource] = []
        try:
            sources = list_input_sources(self.display.bus_number)
        except DDCError as exc:
            LOG.debug("No input sources for %s: %s", self.display.bus_number, exc)
        current_value: Optional[int] = None
        try:
            feature = query_feature(self.display.bus_number, "0x60")
            current_value = feature.current
        except DDCError as exc:
            LOG.debug("Unable to read current input for %s: %s", self.display.bus_number, exc)
        return sources, current_value

    def _on_features_loaded(self, result: Optional[Dict[str, FeatureValue]], error: Optional[BaseException]) -> None:
        if error:
            LOG.error("Failed to load features: %s", error)
            for row in self._feature_rows.values():
                row.set_error(str(error))
                row.set_loading(False)
            return
        assert result is not None
        for code, row in self._feature_rows.items():
            value = result.get(code)
            if value:
                row.update_value(value.current, value.maximum)
            else:
                row.set_error("Feature unsupported")
        for row in self._feature_rows.values():
            row.set_loading(False)

    def _on_inputs_loaded(self, result: Optional[Tuple[List[InputSource], Optional[int]]], error: Optional[BaseException]) -> None:
        if error:
            LOG.error("Failed to load inputs: %s", error)
            self.input_row.set_error(str(error))
            self.input_row.set_loading(False)
            return
        assert result is not None
        sources, active_value = result
        self.input_row.update_sources(sources, active_value)
        self.input_row.set_loading(False)

    # -------------------- Event handlers --------------------

    def on_slider_changed(self, code: str, value: int) -> None:
        def apply_change() -> None:
            set_feature(self.display.bus_number, code, value)

        self.runner.run(apply_change, self._on_set_feature_finished)

    def _on_set_feature_finished(self, _result: Optional[object], error: Optional[BaseException]) -> None:
        if error:
            LOG.error("Failed to set feature: %s", error)

    def on_input_changed(self, value: int) -> None:
        def apply_change() -> None:
            set_feature(self.display.bus_number, "0x60", value)

        self.runner.run(apply_change, self._on_set_feature_finished)


class SliderRow(Adw.ActionRow):
    """Action row with a slider for VCP values."""

    def __init__(self, title: str, subtitle: str, icon: str, code: str, callback):
        super().__init__(title=title, subtitle=subtitle)
        self.code = code
        self._callback = callback
        self._default_subtitle = subtitle
        self._updating = False

        self._scale = Gtk.Scale.new_with_range(Gtk.Orientation.HORIZONTAL, 0, 100, 1)
        self._scale.set_hexpand(True)
        self._scale.set_valign(Gtk.Align.CENTER)
        self._scale.connect("value-changed", self._on_value_changed)
        self.add_suffix(self._scale)
        self.set_activatable_widget(self._scale)
        if icon:
            self.set_icon_name(icon)

    def set_loading(self, loading: bool) -> None:
        self._scale.set_sensitive(not loading)

    def set_error(self, message: str) -> None:
        self.set_subtitle(message)
        self._scale.set_sensitive(False)

    def update_value(self, current: int, maximum: int) -> None:
        self.set_subtitle(self._default_subtitle)
        self._scale.set_sensitive(True)
        self._updating = True
        self._scale.set_range(0, max(1, maximum or 100))
        self._scale.set_value(current)
        self._updating = False

    def _on_value_changed(self, scale: Gtk.Scale) -> None:
        if self._updating:
            return
        value = int(scale.get_value())
        self._callback(self.code, value)


class InputSourceRow(Adw.ActionRow):
    """Row with a drop-down for selecting an input source."""

    def __init__(self, on_change):
        super().__init__(title="Input source", subtitle="Switch the active video input")
        self._on_change = on_change
        self._sources: List[InputSource] = []
        self._updating = False

        self._dropdown = Gtk.DropDown.new_from_strings([])
        self._dropdown.set_hexpand(True)
        self._dropdown.connect("notify::selected", self._on_selected)
        self.add_suffix(self._dropdown)
        self.set_activatable_widget(self._dropdown)

    def set_loading(self, loading: bool) -> None:
        self._dropdown.set_sensitive(not loading)

    def set_error(self, message: str) -> None:
        self.set_subtitle(message)
        self._dropdown.set_sensitive(False)

    def update_sources(self, sources: List[InputSource], active_value: Optional[int]) -> None:
        self._sources = sources
        if not sources:
            self.set_subtitle("No controllable inputs found")
            self._dropdown.set_model(Gtk.StringList.new([]))
            self._dropdown.set_sensitive(False)
            return

        labels = [source.label for source in sources]
        model = Gtk.StringList.new(labels)
        self._dropdown.set_model(model)
        self._dropdown.set_sensitive(True)
        self.set_subtitle("Switch the active video input")

        index = 0
        if active_value is not None:
            for idx, source in enumerate(sources):
                if source.value == active_value:
                    index = idx
                    break
        self._updating = True
        self._dropdown.set_selected(index)
        self._updating = False

    def _on_selected(self, dropdown: Gtk.DropDown, _pspec) -> None:
        if self._updating:
            return
        index = dropdown.get_selected()
        if 0 <= index < len(self._sources):
            self._on_change(self._sources[index].value)


class GnomeDDCWindow(Adw.ApplicationWindow):
    """Primary application window."""

    def __init__(self, app: Adw.Application):
        super().__init__(application=app)
        self.set_default_size(940, 580)
        self.set_title("GnomeDDC")

        self.runner: AsyncRunner[object] = AsyncRunner()
        self._monitor_views: Dict[str, MonitorView] = {}

        toolbar_view = Adw.ToolbarView()
        self.set_content(toolbar_view)

        header = Adw.HeaderBar()
        title = Adw.WindowTitle(title="GnomeDDC", subtitle=f"Version {__version__}")
        header.set_title_widget(title)

        self._refresh_button: Gtk.Button = Gtk.Button()
        self._refresh_button.set_child(Gtk.Image.new_from_icon_name("view-refresh-symbolic"))
        self._refresh_button.set_tooltip_text("Rescan for connected monitors")
        self._refresh_button.connect("clicked", self._on_refresh_button_clicked)
        header.pack_end(self._refresh_button)

        toolbar_view.add_top_bar(header)

        self.split_view = Adw.NavigationSplitView()
        toolbar_view.set_content(self.split_view)

        self.monitor_list = Gtk.ListBox(css_classes=["navigation-sidebar"])
        self.monitor_list.set_selection_mode(Gtk.SelectionMode.SINGLE)
        self.monitor_list.connect("row-selected", self._on_monitor_selected)
        sidebar_page = Adw.NavigationPage.new(self.monitor_list, "Displays")
        sidebar_page.set_icon_name("video-display-symbolic")
        self.split_view.set_sidebar(sidebar_page)

        self.content_stack = Gtk.Stack()
        self.content_stack.set_transition_type(Gtk.StackTransitionType.CROSSFADE)

        self._status_page = self._create_status_page("Searching for monitors…")
        self.content_stack.add_named(self._status_page, "status")

        self._placeholder_page = self._create_status_page(
            "Select a monitor to begin", icon="video-display-symbolic"
        )
        self.content_stack.add_named(self._placeholder_page, "placeholder")

        self._error_page: Adw.StatusPage | None = None
        self._empty_page: Adw.StatusPage | None = None

        self.split_view.set_content(Adw.NavigationPage.new(self.content_stack, "Controls"))

        self._set_refresh_sensitive(True)
        self._load_monitors()

    def _create_status_page(
        self,
        title: str,
        description: str = "",
        icon: str = "dialog-information-symbolic",
    ) -> Adw.StatusPage:
        page = Adw.StatusPage(title=title, description=description)
        page.set_icon_name(icon)
        return page

    def _load_monitors(self) -> None:
        self._set_refresh_sensitive(False)
        self.content_stack.set_visible_child_name("status")

        def finish(result: Optional[List[DisplayInfo]], error: Optional[BaseException]) -> None:
            self._set_refresh_sensitive(True)
            if error:
                LOG.error("Failed to list monitors: %s", error)
                page = self._ensure_status_page(
                    "error",
                    "Unable to detect monitors",
                    str(error),
                    "dialog-error-symbolic",
                )
                self.content_stack.set_visible_child_name("error")
                return
            assert result is not None
            self._populate_monitor_list(result)
            if result:
                self.content_stack.set_visible_child_name("placeholder")
            else:
                empty_page = self._ensure_status_page(
                    "empty",
                    "No monitors found",
                    "Ensure your display supports DDC/CI and that ddcutil has permission to access it.",
                    "dialog-warning-symbolic",
                )
                self.content_stack.set_visible_child_name("empty")

        self.runner.run(list_displays, finish)

    def _ensure_status_page(
        self, name: str, title: str, description: str, icon: str
    ) -> Adw.StatusPage:
        if name == "error":
            if self._error_page is None:
                self._error_page = self._create_status_page(title, description, icon)
                self.content_stack.add_named(self._error_page, name)
            else:
                self._update_status_page(self._error_page, title, description, icon)
            return self._error_page
        if name == "empty":
            if self._empty_page is None:
                self._empty_page = self._create_status_page(title, description, icon)
                self.content_stack.add_named(self._empty_page, name)
            else:
                self._update_status_page(self._empty_page, title, description, icon)
            return self._empty_page
        page = self._create_status_page(title, description, icon)
        self.content_stack.add_named(page, name)
        return page

    def _update_status_page(
        self, page: Adw.StatusPage, title: str, description: str, icon: str
    ) -> None:
        page.set_title(title)
        page.set_description(description)
        page.set_icon_name(icon)

    def _populate_monitor_list(self, displays: List[DisplayInfo]) -> None:
        for child in list(self.monitor_list.get_children()):
            self.monitor_list.remove(child)
        self._monitor_views.clear()

        for display in displays:
            row = MonitorRow(display)
            self.monitor_list.append(row)

    def _set_refresh_sensitive(self, enabled: bool) -> None:
        self._refresh_button.set_sensitive(enabled)

    def _on_refresh_button_clicked(self, _button: Gtk.Button) -> None:
        self._load_monitors()

    def _on_monitor_selected(self, _box: Gtk.ListBox, row: Optional[MonitorRow]) -> None:
        if row is None:
            self.content_stack.set_visible_child_name("placeholder")
            return
        display = row.display
        if display.bus_number not in self._monitor_views:
            view = MonitorView(display, self.runner)
            self._monitor_views[display.bus_number] = view
            self.content_stack.add_named(view, display.bus_number)
        self.content_stack.set_visible_child_name(display.bus_number)


class GnomeDDCApplication(Adw.Application):
    """Main application class."""

    def __init__(self) -> None:
        super().__init__(
            application_id="io.github.gnomeddc.GnomeDDC",
            flags=Gio.ApplicationFlags.FLAGS_NONE,
        )
        Adw.init()
        GLib.set_application_name("GnomeDDC")
        self.connect("activate", self.on_activate)

    def on_activate(self, app: Adw.Application) -> None:
        window = self.props.active_window
        if window is None:
            window = GnomeDDCWindow(self)
        window.present()


__all__ = ["GnomeDDCApplication"]

