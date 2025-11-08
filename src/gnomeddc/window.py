from __future__ import annotations

from typing import Dict

from gi.repository import Adw, GLib, Gtk

from .monitor_manager import FeatureState, MonitorInfo, MonitorManager
from .widgets import FeatureRow


@Gtk.Template(resource_path='/io/github/gnomeddc/ui/monitor-row.ui')
class MonitorRow(Adw.ActionRow):
    __gtype_name__ = 'MonitorRow'

    def __init__(self, monitor: MonitorInfo) -> None:
        super().__init__()
        self.monitor = monitor
        self.init_template()
        self.refresh()

    def refresh(self) -> None:
        self.set_title(self.monitor.display_name)
        subtitle = ' · '.join(filter(None, [self.monitor.model, self.monitor.vendor]))
        if not subtitle:
            subtitle = self.monitor.serial or ''
        self.set_subtitle(subtitle)


@Gtk.Template(resource_path='/io/github/gnomeddc/ui/feature-page.ui')
class MonitorFeaturePage(Adw.Bin):
    __gtype_name__ = 'MonitorFeaturePage'

    preferences_page: Adw.PreferencesPage = Gtk.Template.Child()
    model_row: Adw.ActionRow = Gtk.Template.Child()
    serial_row: Adw.ActionRow = Gtk.Template.Child()
    capabilities_row: Adw.ActionRow = Gtk.Template.Child()
    luminance_group: Adw.PreferencesGroup = Gtk.Template.Child()
    picture_group: Adw.PreferencesGroup = Gtk.Template.Child()
    color_group: Adw.PreferencesGroup = Gtk.Template.Child()
    advanced_group: Adw.PreferencesGroup = Gtk.Template.Child()
    feature_table_row: Adw.ExpanderRow = Gtk.Template.Child()
    feature_list: Gtk.ListBox = Gtk.Template.Child()

    def __init__(self) -> None:
        super().__init__()
        self.init_template()
        self._manager: MonitorManager | None = None
        self._monitor: MonitorInfo | None = None
        self._feature_rows: Dict[int, list[FeatureRow]] = {}

    def bind(self, manager: MonitorManager, monitor: MonitorInfo) -> None:
        self._manager = manager
        self._monitor = monitor
        self.model_row.set_subtitle(monitor.model or '—')
        self.serial_row.set_subtitle(monitor.serial or '—')
        self.capabilities_row.set_subtitle(monitor.capabilities or '—')
        self._rebuild_feature_groups(monitor)

    def update_feature(self, feature: FeatureState) -> None:
        rows = self._feature_rows.get(feature.code, [])
        for row in rows:
            row.update(feature.value, feature.maximum)

    # Internal helpers -------------------------------------------------

    def _rebuild_feature_groups(self, monitor: MonitorInfo) -> None:
        for group in (self.luminance_group, self.picture_group, self.color_group, self.advanced_group):
            for child in list(group.get_children()):
                group.remove(child)
        for child in list(self.feature_list.get_children()):
            self.feature_list.remove(child)
        self._feature_rows.clear()

        category_groups = {
            'Luminance': self.luminance_group,
            'Picture': self.picture_group,
            'Color': self.color_group,
        }

        ordered_features = sorted(monitor.features.values(), key=lambda feature: (feature.category, feature.code))
        for feature in ordered_features:
            handler = self._make_on_change(feature)
            row = FeatureRow(feature, handler)
            self._feature_rows.setdefault(feature.code, []).append(row)
            target_group = category_groups.get(feature.category)
            if target_group is not None:
                target_group.add(row)
            else:
                self.advanced_group.add(row)
            explorer_row = FeatureRow(feature, handler)
            self._feature_rows.setdefault(feature.code, []).append(explorer_row)
            self.feature_list.append(explorer_row)
        self.feature_table_row.set_visible(bool(ordered_features))

    def _make_on_change(self, feature: FeatureState):
        def handler(value: int) -> bool:
            if self._manager is None or self._monitor is None:
                return False
            return self._manager.set_feature_value(self._monitor, feature, value)

        return handler


@Gtk.Template(resource_path='/io/github/gnomeddc/ui/main-window.ui')
class GnomeDdcWindow(Adw.ApplicationWindow):
    __gtype_name__ = 'GnomeDdcWindow'

    monitor_list: Gtk.ListBox = Gtk.Template.Child()
    content_stack: Adw.ViewStack = Gtk.Template.Child()
    loading_page: Adw.StatusPage = Gtk.Template.Child()
    empty_page: Adw.StatusPage = Gtk.Template.Child()
    refresh_button: Gtk.Button = Gtk.Template.Child()
    search_entry: Gtk.SearchEntry = Gtk.Template.Child()
    toast_overlay: Adw.ToastOverlay = Gtk.Template.Child()

    def __init__(self, **kwargs) -> None:
        super().__init__(**kwargs)
        self.init_template()
        self._manager = MonitorManager()
        self._manager.connect('refresh-started', self._on_refresh_started)
        self._manager.connect('refresh-failed', self._on_refresh_failed)
        self._manager.connect('monitors-changed', self._on_monitors_changed)
        self._manager.connect('monitor-updated', self._on_monitor_updated)
        self.refresh_button.connect('clicked', lambda *_: self.reload_monitors())
        self.monitor_list.connect('row-selected', self._on_monitor_selected)
        self.search_entry.connect('search-changed', self._on_search_changed)
        self._row_lookup: Dict[Gtk.ListBoxRow, MonitorInfo] = {}
        self._page_lookup: Dict[str, MonitorFeaturePage] = {}
        self.reload_monitors()

    # Public API -------------------------------------------------------

    def reload_monitors(self) -> None:
        self._manager.refresh()

    # Event handlers ---------------------------------------------------

    def _on_refresh_started(self, *_args) -> None:
        self._show_status_page(self.loading_page)

    def _on_refresh_failed(self, _manager: MonitorManager, message: str) -> None:
        self._show_status_page(self.empty_page)
        toast = Adw.Toast.new(message)
        self.toast_overlay.add_toast(toast)

    def _on_monitors_changed(self, manager: MonitorManager) -> None:
        monitors = manager.monitors()
        selected = self.monitor_list.get_selected_row()
        selected_path = None
        if selected and selected in self._row_lookup:
            selected_path = self._row_lookup[selected].object_path
        self._row_lookup.clear()
        for child in list(self.monitor_list.get_children()):
            self.monitor_list.remove(child)

        for monitor in monitors:
            row = MonitorRow(monitor)
            self.monitor_list.append(row)
            self._row_lookup[row] = monitor
        self.monitor_list.show()

        missing_paths = set(self._page_lookup.keys()) - {monitor.object_path for monitor in monitors}
        for object_path in missing_paths:
            page = self._page_lookup.pop(object_path)
            page_widget = page
            stack_page = self.content_stack.get_page(page_widget)
            if stack_page is not None:
                self.content_stack.remove(page_widget)

        if not monitors:
            self._show_status_page(self.empty_page)
            return

        if selected_path:
            for row, monitor in self._row_lookup.items():
                if monitor.object_path == selected_path:
                    self.monitor_list.select_row(row)
                    break
            else:
                self.monitor_list.select_row(self.monitor_list.get_row_at_index(0))
        else:
            self.monitor_list.select_row(self.monitor_list.get_row_at_index(0))

    def _on_monitor_updated(self, _manager: MonitorManager, object_path: str) -> None:
        page = self._page_lookup.get(object_path)
        monitor = next((info for info in self._manager.monitors() if info.object_path == object_path), None)
        if not page or monitor is None:
            return
        for feature in monitor.features.values():
            page.update_feature(feature)

    def _on_monitor_selected(self, listbox: Gtk.ListBox, row: Gtk.ListBoxRow | None) -> None:
        if row is None:
            self._show_status_page(self.empty_page)
            return
        monitor = self._row_lookup.get(row)
        if monitor is None:
            self._show_status_page(self.empty_page)
            return
        page = self._page_lookup.get(monitor.object_path)
        if page is None:
            page = MonitorFeaturePage()
            page.bind(self._manager, monitor)
            self._page_lookup[monitor.object_path] = page
            self.content_stack.add_titled(page, monitor.object_path, monitor.display_name)
        else:
            page.bind(self._manager, monitor)
        self.content_stack.set_visible_child(page)

    def _on_search_changed(self, entry: Gtk.SearchEntry) -> None:
        query = entry.get_text().strip().lower()
        for row, monitor in self._row_lookup.items():
            match = not query or query in monitor.display_name.lower() or query in (monitor.model or '').lower()
            row.set_visible(match)
        visible_rows = [row for row in self._row_lookup if row.get_visible()]
        if visible_rows and self.monitor_list.get_selected_row() not in visible_rows:
            self.monitor_list.select_row(visible_rows[0])
        elif not visible_rows:
            self.content_stack.set_visible_child(self.empty_page)

    # Internal helpers -------------------------------------------------

    def _show_status_page(self, page: Adw.StatusPage) -> None:
        self.content_stack.set_visible_child(page)




