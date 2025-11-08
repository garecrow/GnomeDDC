"""Widget builders for VCP features."""

from __future__ import annotations

from typing import Callable, Optional

from gi.repository import Adw, Gio, GLib, Gtk, GObject

from ..monitor_store import MonitorState, MonitorStore, VcpValue


class ContinuousControl(Gtk.Box):
    """Slider control for continuous VCP features."""

    def __init__(
        self,
        store: MonitorStore,
        monitor: MonitorState,
        value: VcpValue,
        *,
        on_reset: Optional[Callable[[MonitorState, int], None]] = None,
    ) -> None:
        super().__init__(orientation=Gtk.Orientation.VERTICAL, spacing=6)
        self.store = store
        self.monitor = monitor
        self.value = value
        self.on_reset = on_reset

        metadata = value.metadata
        title = metadata.name if metadata else f"VCP 0x{value.code:02X}"

        header = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=6)
        label = Gtk.Label(label=title, xalign=0)
        header.append(label)
        self._value_label = Gtk.Label(label=self._format_value(value.current), xalign=1)
        header.append(self._value_label)
        self.append(header)

        adjustment = Gtk.Adjustment(
            value=value.current,
            lower=metadata.minimum if metadata and metadata.minimum is not None else 0,
            upper=value.maximum or 100,
            step_increment=1,
            page_increment=5,
        )
        self._slider = Gtk.Scale(orientation=Gtk.Orientation.HORIZONTAL, adjustment=adjustment)
        self._slider.set_hexpand(True)
        self._slider.set_draw_value(False)
        self._slider.connect("value-changed", self._on_slider_changed)
        self._slider.connect("change-value", self._on_change_value)
        self.append(self._slider)

        buttons = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=6)
        self._entry = Gtk.SpinButton(adjustment=adjustment, climb_rate=1, digits=0)
        self._entry.connect("value-changed", self._on_entry_changed)
        buttons.append(self._entry)

        reset_button = Gtk.Button.new_with_label("Reset")
        reset_button.connect("clicked", self._on_reset_clicked)
        reset_button.set_sensitive(bool(metadata and (metadata.has_user_default or metadata.has_factory_default)))
        buttons.append(reset_button)

        self.append(buttons)

        self._debounce_id: int = 0
        self._pending_value: Optional[int] = None

    def _format_value(self, value: int) -> str:
        metadata = self.value.metadata
        if metadata and metadata.maximum:
            return f"{value}/{metadata.maximum}"
        return str(value)

    def update_value(self, current: int, maximum: Optional[int] = None) -> None:
        if maximum is not None:
            self.value.maximum = maximum
        self.value.current = current
        self._value_label.set_text(self._format_value(current))
        if int(self._slider.get_value()) != current:
            self._slider.set_value(current)
        if self._entry.get_value_as_int() != current:
            self._entry.set_value(current)

    def _on_slider_changed(self, slider: Gtk.Scale) -> None:
        self._entry.set_value(slider.get_value())
        self._schedule_commit(int(slider.get_value()))

    def _on_entry_changed(self, entry: Gtk.SpinButton) -> None:
        self._slider.set_value(entry.get_value())
        self._schedule_commit(int(entry.get_value()))

    def _on_change_value(self, _scale: Gtk.Scale, _scroll: Gtk.ScrollType, value: float) -> bool:
        self._schedule_commit(int(value))
        return False

    def _on_reset_clicked(self, _button: Gtk.Button) -> None:
        metadata = self.value.metadata
        if metadata and metadata.default_value is not None:
            value = metadata.default_value
        else:
            value = self.value.maximum // 2 if self.value.maximum else self.value.current
        if self.on_reset:
            self.on_reset(self.monitor, self.value.code)
        else:
            self.store.set_vcp_value(self.monitor, self.value.code, value)

    def _commit_value(self, value: int) -> None:
        if value == self.value.current:
            return
        self.store.set_vcp_value(self.monitor, self.value.code, value)
        self.value.current = value
        self._value_label.set_text(self._format_value(value))

    def _schedule_commit(self, value: int) -> None:
        self._pending_value = value
        if self._debounce_id:
            return

        def _on_timeout() -> bool:
            if self._pending_value is not None:
                self._commit_value(self._pending_value)
            self._debounce_id = 0
            self._pending_value = None
            return False

        self._debounce_id = GLib.timeout_add(150, _on_timeout)


class EnumeratedControl(Gtk.Box):
    """Dropdown control for enumerated VCP features."""

    def __init__(self, store: MonitorStore, monitor: MonitorState, value: VcpValue) -> None:
        super().__init__(orientation=Gtk.Orientation.VERTICAL, spacing=6)
        self.store = store
        self.monitor = monitor
        self.value = value

        metadata = value.metadata
        title = metadata.name if metadata else f"VCP 0x{value.code:02X}"

        header = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=6)
        label = Gtk.Label(label=title, xalign=0)
        header.append(label)
        self.append(header)

        model = Gtk.StringList()
        self._value_to_index = {}
        active_index = 0
        if metadata and metadata.values:
            for index, (raw_value, description) in enumerate(sorted(metadata.values.items())):
                model.append(f"{description} ({raw_value})")
                self._value_to_index[index] = raw_value
                if raw_value == value.current:
                    active_index = index
        else:
            model.append(str(value.current))
        self._dropdown = Gtk.DropDown(model=model)
        self._dropdown.set_selected(active_index)
        self._dropdown.connect("notify::selected", self._on_selected)
        self.append(self._dropdown)
        self._active_index = active_index

    def _on_selected(self, dropdown: Gtk.DropDown, _pspec: GObject.ParamSpec) -> None:  # type: ignore[name-defined]
        index = dropdown.get_selected()
        value = self._value_to_index.get(index, self.value.current)
        if value != self.value.current:
            self.store.set_vcp_value(self.monitor, self.value.code, value)
            self.value.current = value
            self._active_index = index

    def update_value(self, current: int, _maximum: Optional[int] = None) -> None:
        self.value.current = current
        for index, raw_value in self._value_to_index.items():
            if raw_value == current and index != self._active_index:
                self._dropdown.set_selected(index)
                self._active_index = index
                break


def build_control(store: MonitorStore, monitor: MonitorState, value: VcpValue) -> Gtk.Widget:
    metadata = value.metadata
    if metadata and metadata.is_continuous:
        return ContinuousControl(store, monitor, value)
    return EnumeratedControl(store, monitor, value)
