"""Libadwaita application window for the native GnomeDDC UI."""

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
        "subtitle": "Control overall luminance",
        "icon": "display-brightness-symbolic",
    },
    "0x12": {
        "title": "Contrast",
        "subtitle": "Increase the separation between dark and light areas",
        "icon": "contrast-symbolic",
    },
}


class MonitorRow(Adw.ActionRow):
    """Sidebar row representing a discovered monitor."""

    def __init__(self, display: DisplayInfo):
        super().__init__(title=display.model or "Unknown display", subtitle=display.manufacturer or "")
        self.display = display
        self.set_icon_name("video-display-symbolic")
        self.set_activatable(True)


class InfoRow(Adw.ActionRow):
    """Simple information row used in the details section."""

    def __init__(self, title: str, value: str | None):
        super().__init__(title=title, subtitle=value or "—")
        self.set_activatable(False)


class SliderRow(Adw.ActionRow):
    """Action row that combines a slider with a live value indicator."""

    def __init__(self, title: str, subtitle: str, icon: str, code: str, callback):
        super().__init__(title=title, subtitle=subtitle)
        self.code = code
        self._callback = callback
        self._default_subtitle = subtitle
        self._updating = False

        if icon:
            self.set_icon_name(icon)

        self._scale = Gtk.Scale.new_with_range(Gtk.Orientation.HORIZONTAL, 0, 100, 1)
        self._scale.set_hexpand(True)
        self._scale.set_valign(Gtk.Align.CENTER)
        self._scale.set_draw_value(False)
        self._scale.connect("value-changed", self._on_value_changed)
        self.add_suffix(self._scale)
        self.set_activatable_widget(self._scale)

        self._value_label = Gtk.Label(label="0")
        self._value_label.set_width_chars(4)
        self._value_label.set_xalign(1.0)
        self._value_label.add_css_class("numeric")
        self.add_suffix(self._value_label)

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
        self._value_label.set_text(str(current))
        self._updating = False

    def _on_value_changed(self, scale: Gtk.Scale) -> None:
        value = int(scale.get_value())
        self._value_label.set_text(str(value))
        if self._updating:
            return
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
        self._dropdown.set_valign(Gtk.Align.CENTER)
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
        self._dropdown.set_model(Gtk.StringList.new(labels))
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


class MonitorDetailPage(Gtk.ScrolledWindow):
    """Scrollable page with controls for a single monitor."""

    def __init__(self, display: DisplayInfo, runner: AsyncRunner[object]):
        super().__init__()
        self.display = display
        self.runner = runner
        self._feature_rows: Dict[str, SliderRow] = {}
        self._pending_callbacks = 0

        self.set_policy(Gtk.PolicyType.NEVER, Gtk.PolicyType.AUTOMATIC)

        clamp = Adw.Clamp()
        clamp.set_maximum_size(720)
        clamp.set_tightening_threshold(520)
        clamp.set_margin_top(24)
        clamp.set_margin_bottom(24)
        clamp.set_margin_start(24)
        clamp.set_margin_end(24)

        self._content = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=24)
        clamp.set_child(self._content)
        self.set_child(clamp)

        self._content.append(self._build_header())
        self._content.append(self._build_feature_groups())
        self._content.append(self._build_details_group())

        self.refresh()

    # -------------------- UI construction helpers --------------------

    def _build_header(self) -> Gtk.Widget:
        header = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=18)
        header.add_css_class("card")
        header.add_css_class("background")
        header.set_margin_bottom(6)

        icon = Gtk.Image.new_from_icon_name("video-display-symbolic")
        icon.set_pixel_size(96)
        icon.add_css_class("accent")
        header.append(icon)

        info_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=4)
        info_box.set_hexpand(True)
        title = Gtk.Label(label=self.display.model or "Unknown display")
        title.set_xalign(0.0)
        title.add_css_class("title-1")
        info_box.append(title)

        subtitle_parts: List[str] = []
        if self.display.manufacturer:
            subtitle_parts.append(self.display.manufacturer)
        if self.display.serial:
            subtitle_parts.append(f"SN {self.display.serial}")
        subtitle = Gtk.Label(label=" · ".join(subtitle_parts) or "Unidentified manufacturer")
        subtitle.set_xalign(0.0)
        subtitle.set_wrap(True)
        info_box.append(subtitle)

        details = Gtk.Label(label=f"Bus {self.display.bus_number}")
        details.set_xalign(0.0)
        details.add_css_class("dim-label")
        info_box.append(details)

        header.append(info_box)

        self._refresh_button = Gtk.Button()
        self._refresh_button.add_css_class("pill")
        self._refresh_content = Adw.ButtonContent()
        self._refresh_content.set_icon_name("view-refresh-symbolic")
        self._refresh_content.set_label("Refresh data")
        self._refresh_button.set_child(self._refresh_content)
        self._refresh_button.set_valign(Gtk.Align.START)
        self._refresh_button.connect("clicked", lambda _btn: self.refresh())
        header.append(self._refresh_button)

        return header

    def _build_feature_groups(self) -> Gtk.Widget:
        picture_page = Adw.PreferencesGroup(title="Picture", description="Adjust how the display looks")
        for code, meta in SUPPORTED_FEATURES.items():
            row = SliderRow(meta["title"], meta["subtitle"], meta["icon"], code, self._on_slider_changed)
            picture_page.add(row)
            self._feature_rows[code] = row
        return picture_page

    def _build_details_group(self) -> Gtk.Widget:
        details = Adw.PreferencesGroup(title="Details")
        details.add(InfoRow("Manufacturer", self.display.manufacturer))
        details.add(InfoRow("Model", self.display.model))
        details.add(InfoRow("Serial number", self.display.serial))
        details.add(InfoRow("Bus", self.display.bus_number))

        self.input_row = InputSourceRow(on_change=self._on_input_changed)
        input_group = Adw.PreferencesGroup(title="Input")
        input_group.add(self.input_row)

        container = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=18)
        container.append(input_group)
        container.append(details)
        return container

    # -------------------- Data loading --------------------

    def refresh(self) -> None:
        for row in self._feature_rows.values():
            row.set_loading(True)
        self.input_row.set_loading(True)
        self._set_refresh_loading(True)

        self._pending_callbacks = 2
        self.runner.run(self._fetch_features, self._on_features_loaded)
        self.runner.run(self._fetch_inputs, self._on_inputs_loaded)

    def _set_refresh_loading(self, loading: bool) -> None:
        self._refresh_button.set_sensitive(not loading)
        self._refresh_content.set_label("Refreshing…" if loading else "Refresh data")

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

    def _on_features_loaded(
        self,
        result: Optional[Dict[str, FeatureValue]],
        error: Optional[BaseException],
    ) -> None:
        if error:
            LOG.error("Failed to load features: %s", error)
            for row in self._feature_rows.values():
                row.set_error(str(error))
                row.set_loading(False)
            self._finalize_refresh()
            return

        assert result is not None
        for code, row in self._feature_rows.items():
            value = result.get(code)
            if value:
                row.update_value(value.current, value.maximum)
            else:
                row.set_error("Feature unsupported")
            row.set_loading(False)
        self._finalize_refresh()

    def _on_inputs_loaded(
        self,
        result: Optional[Tuple[List[InputSource], Optional[int]]],
        error: Optional[BaseException],
    ) -> None:
        if error:
            LOG.error("Failed to load inputs: %s", error)
            self.input_row.set_error(str(error))
            self.input_row.set_loading(False)
            self._finalize_refresh()
            return

        assert result is not None
        sources, active_value = result
        self.input_row.update_sources(sources, active_value)
        self.input_row.set_loading(False)
        self._finalize_refresh()

    def _finalize_refresh(self) -> None:
        self._pending_callbacks -= 1
        if self._pending_callbacks <= 0:
            self._set_refresh_loading(False)

    # -------------------- Event handlers --------------------

    def _on_slider_changed(self, code: str, value: int) -> None:
        def apply_change() -> None:
            set_feature(self.display.bus_number, code, value)

        self.runner.run(apply_change, self._on_set_feature_finished)

    def _on_input_changed(self, value: int) -> None:
        def apply_change() -> None:
            set_feature(self.display.bus_number, "0x60", value)

        self.runner.run(apply_change, self._on_set_feature_finished)

    def _on_set_feature_finished(self, _result: Optional[object], error: Optional[BaseException]) -> None:
        if error:
            LOG.error("Failed to set feature: %s", error)


class GnomeDDCWindow(Adw.ApplicationWindow):
    """Primary application window matching the GNOME Settings look."""

    def __init__(self, app: Adw.Application):
        super().__init__(application=app)
        self.set_default_size(960, 640)
        self.set_title("GnomeDDC")

        self.runner: AsyncRunner[object] = AsyncRunner()
        self._monitor_pages: Dict[str, MonitorDetailPage] = {}

        toolbar_view = Adw.ToolbarView()
        self.set_content(toolbar_view)

        header = Adw.HeaderBar()
        self._title_widget = Adw.WindowTitle(title="Displays", subtitle=f"GnomeDDC {__version__}")
        header.set_title_widget(self._title_widget)
        toolbar_view.add_top_bar(header)

        self._detect_button = Gtk.Button()
        self._detect_button.add_css_class("pill")
        detect_content = Adw.ButtonContent()
        detect_content.set_icon_name("system-search-symbolic")
        detect_content.set_label("Detect displays")
        self._detect_button.set_child(detect_content)
        self._detect_button.connect("clicked", self._on_detect_clicked)
        header.pack_end(self._detect_button)

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
        self.split_view.set_content(Adw.NavigationPage.new(self.content_stack, "Controls"))

        self._status_pages: Dict[str, Adw.StatusPage] = {}
        self._show_status(
            "loading",
            "Searching for displays…",
            "Looking for monitors that expose DDC/CI controls.",
            icon="system-search-symbolic",
        )

        self._load_monitors()

    # -------------------- Sidebar and status helpers --------------------

    def _create_status_page(
        self,
        title: str,
        description: str = "",
        icon: str = "dialog-information-symbolic",
    ) -> Adw.StatusPage:
        page = Adw.StatusPage(title=title, description=description)
        page.set_icon_name(icon)
        return page

    def _show_status(self, key: str, title: str, description: str, icon: str = "dialog-information-symbolic") -> None:
        page = self._status_pages.get(key)
        if page is None:
            page = self._create_status_page(title, description, icon)
            self._status_pages[key] = page
            self.content_stack.add_named(page, key)
        else:
            page.set_title(title)
            page.set_description(description)
            page.set_icon_name(icon)
        self.content_stack.set_visible_child_name(key)

    def _populate_monitor_list(self, displays: List[DisplayInfo]) -> None:
        for child in list(self.monitor_list.get_children()):
            self.monitor_list.remove(child)
        self._monitor_pages.clear()

        for display in displays:
            row = MonitorRow(display)
            self.monitor_list.append(row)

        self.monitor_list.show_all()

    def _update_header_subtitle(self, displays: List[DisplayInfo]) -> None:
        count = len(displays)
        subtitle = "No displays detected" if count == 0 else ("1 display detected" if count == 1 else f"{count} displays detected")
        self._title_widget.set_subtitle(subtitle)

    # -------------------- Monitor loading --------------------

    def _on_detect_clicked(self, _button: Gtk.Button) -> None:
        self._load_monitors()

    def _load_monitors(self) -> None:
        self._detect_button.set_sensitive(False)
        self._show_status(
            "loading",
            "Searching for displays…",
            "Looking for monitors that expose DDC/CI controls.",
            icon="system-search-symbolic",
        )

        def finish(result: Optional[List[DisplayInfo]], error: Optional[BaseException]) -> None:
            self._detect_button.set_sensitive(True)
            if error:
                LOG.error("Failed to list monitors: %s", error)
                self._show_status(
                    "error",
                    "Unable to detect monitors",
                    str(error),
                    icon="dialog-error-symbolic",
                )
                self._update_header_subtitle([])
                return

            assert result is not None
            self._populate_monitor_list(result)
            self._update_header_subtitle(result)

            if not result:
                self._show_status(
                    "empty",
                    "No monitors found",
                    "Ensure your display supports DDC/CI and that ddcutil has permission to access it.",
                    icon="dialog-warning-symbolic",
                )
                return

            self._show_status(
                "placeholder",
                "Select a display",
                "Choose a monitor from the sidebar to adjust its settings.",
                icon="video-display-symbolic",
            )
            if self.monitor_list.get_row_at_index(0):
                self.monitor_list.select_row(self.monitor_list.get_row_at_index(0))

        self.runner.run(list_displays, finish)

    # -------------------- Selection handling --------------------

    def _on_monitor_selected(self, _box: Gtk.ListBox, row: Optional[MonitorRow]) -> None:
        if row is None:
            self._show_status(
                "placeholder",
                "Select a display",
                "Choose a monitor from the sidebar to adjust its settings.",
                icon="video-display-symbolic",
            )
            self._title_widget.set_title("Displays")
            return

        display = row.display
        self._title_widget.set_title(display.model or "Display controls")

        if display.bus_number not in self._monitor_pages:
            page = MonitorDetailPage(display, self.runner)
            self._monitor_pages[display.bus_number] = page
            self.content_stack.add_named(page, display.bus_number)
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
