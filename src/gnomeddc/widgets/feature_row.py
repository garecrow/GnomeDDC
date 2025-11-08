from __future__ import annotations

from typing import Callable, Dict

from gi.repository import Adw, Gtk

from .. import feature_catalog, utils
from ..monitor_manager import FeatureState


class FeatureRow(Adw.ActionRow):
    __gtype_name__ = 'FeatureRow'

    def __init__(self, feature: FeatureState, on_change: Callable[[int], bool]) -> None:
        super().__init__()
        self._feature = feature
        self._on_change = on_change
        self.set_title(feature.name)
        self.set_subtitle(feature.description)
        self.set_activatable(False)
        self._choices: Dict[int, str] = dict(feature.choices)

        if feature.kind is feature_catalog.FeatureKind.RANGE:
            self._add_range_control()
        elif feature.kind is feature_catalog.FeatureKind.CHOICE:
            self._add_choice_control()
        elif feature.kind is feature_catalog.FeatureKind.FLAG:
            self._add_flag_control()
        else:
            self._add_value_label()

    # Builders ---------------------------------------------------------

    def _add_range_control(self) -> None:
        adjustment = Gtk.Adjustment(
            lower=float(self._feature.min_value),
            upper=float(self._feature.max_value),
            step_increment=1.0,
            page_increment=5.0,
            value=float(self._feature.value),
        )
        scale = Gtk.Scale(orientation=Gtk.Orientation.HORIZONTAL, adjustment=adjustment)
        scale.set_hexpand(True)
        scale.set_draw_value(False)
        scale.add_css_class('feature-slider-row')
        scale.set_sensitive(self._feature.writable)
        scale.connect('value-changed', self._on_scale_value_changed)
        self.add_suffix(scale)
        self._value_label = Gtk.Label(label=self._format_value(self._feature.value))
        self._value_label.add_css_class('dim-label')
        self.add_suffix(self._value_label)
        self._scale = scale

    def _add_choice_control(self) -> None:
        string_list = Gtk.StringList.new([label for _, label in sorted(self._choices.items())])
        self._choice_lookup: Dict[int, int] = {}
        for idx, (value, _label) in enumerate(sorted(self._choices.items())):
            self._choice_lookup[value] = idx
        dropdown = Gtk.DropDown(model=string_list)
        dropdown.set_sensitive(self._feature.writable)
        dropdown.connect('notify::selected', self._on_dropdown_selected)
        if self._feature.value in self._choice_lookup:
            dropdown.set_selected(self._choice_lookup[self._feature.value])
        self.add_suffix(dropdown)
        self._dropdown = dropdown

    def _add_flag_control(self) -> None:
        switch = Gtk.Switch()
        switch.set_valign(Gtk.Align.CENTER)
        switch.set_active(bool(self._feature.value))
        switch.set_sensitive(self._feature.writable)
        switch.connect('state-set', self._on_switch_state_set)
        self.add_suffix(switch)
        self._switch = switch

    def _add_value_label(self) -> None:
        label = Gtk.Label(label=self._format_value(self._feature.value))
        label.add_css_class('dim-label')
        self.add_suffix(label)
        self._value_label = label

    # Event handlers ---------------------------------------------------

    def _on_scale_value_changed(self, scale: Gtk.Scale) -> None:
        value = int(round(scale.get_value()))
        if value == self._feature.value:
            return
        if self._feature.writable and self._on_change(value):
            self._feature.value = value
            if hasattr(self, '_value_label'):
                self._value_label.set_text(self._format_value(value))
        else:
            scale.set_value(self._feature.value)

    def _on_dropdown_selected(self, dropdown: Gtk.DropDown, _param) -> None:
        if not self._feature.writable:
            return
        index = dropdown.get_selected()
        if index < 0:
            return
        for value, position in self._choice_lookup.items():
            if position == index and value != self._feature.value:
                if self._on_change(value):
                    self._feature.value = value
                else:
                    dropdown.set_selected(self._choice_lookup.get(self._feature.value, -1))
                break

    def _on_switch_state_set(self, switch: Gtk.Switch, state: bool) -> bool:
        value = 1 if state else 0
        if not self._feature.writable:
            return True
        if self._on_change(value):
            self._feature.value = value
            return False
        return True

    # Public API -------------------------------------------------------

    def update(self, value: int, maximum: int | None = None) -> None:
        self._feature.value = value
        if maximum is not None:
            self._feature.maximum = maximum
        if hasattr(self, '_scale'):
            adjustment = self._scale.get_adjustment()
            if maximum is not None:
                adjustment.set_upper(float(maximum))
            self._scale.set_value(float(value))
        if hasattr(self, '_dropdown'):
            if value in self._choice_lookup:
                self._dropdown.set_selected(self._choice_lookup[value])
        if hasattr(self, '_switch'):
            self._switch.set_active(bool(value))
        if hasattr(self, '_value_label'):
            self._value_label.set_text(self._format_value(value))

    # Helpers ----------------------------------------------------------

    def _format_value(self, value: int) -> str:
        return utils.format_feature_value(self._feature.definition, value, self._choices)



