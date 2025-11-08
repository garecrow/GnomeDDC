"""Main application module for GnomeDDC."""
from __future__ import annotations

import base64
import functools
import json
import logging
import os
import random
import re
import string
import textwrap
from dataclasses import dataclass, field
from typing import Any, Callable, Dict, Iterable, List, Optional, Sequence, Tuple

import gi

try:
    gi.require_version("Adw", "1")
    gi.require_version("Gtk", "4.0")
    from gi.repository import Adw, Gio, GLib, GObject, Gtk
except (ImportError, ValueError) as exc:  # pragma: no cover - import guard
    raise SystemExit(
        "GTK 4 and Libadwaita are required to run GnomeDDC."
    ) from exc

from . import __version__

SERVICE_NAME = "com.ddcutil.DdcutilService"
OBJECT_PATH = "/com/ddcutil/DdcutilObject"
INTERFACE = "com.ddcutil.DdcutilInterface"

_LOGGER = logging.getLogger("gnomeddc")


@dataclass(slots=True)
class VcpValue:
    code: int
    current: int
    maximum: int
    label: str = ""
    is_continuous: bool = True
    is_complex: bool = False
    options: Dict[int, str] = field(default_factory=dict)
    read_only: bool = False
    write_only: bool = False
    supports_reset: bool = False
    default_value: Optional[int] = None


@dataclass(slots=True)
class Monitor:
    display_number: int
    edid: str
    model: str
    mccs_version: str = ""
    capabilities: Dict[int, VcpValue] = field(default_factory=dict)
    attributes: Dict[str, Any] = field(default_factory=dict)

    def short_hash(self) -> str:
        try:
            raw = base64.b64decode(self.edid)
        except Exception:  # pragma: no cover - defensive
            return "????"
        return base64.b16encode(raw[:4]).decode("ascii")


class DdcutilError(RuntimeError):
    """Raised when ddcutil-service returns a non-zero status."""

    def __init__(self, status: int, message: str) -> None:
        super().__init__(f"DDC error {status}: {message}")
        self.status = status
        self.message = message


class DdcutilServiceClient(GObject.GObject):
    """Small wrapper around the D-Bus interface exposed by ddcutil-service."""

    __gsignals__ = {
        "service-initialized": (GObject.SignalFlags.RUN_LAST, None, ()),
        "connected-displays-changed": (
            GObject.SignalFlags.RUN_LAST,
            None,
            (str, int, int),
        ),
        "vcp-value-changed": (
            GObject.SignalFlags.RUN_LAST,
            None,
            (int, str, int, int, str, str, int),
        ),
    }

    def __init__(self, *, bus_type: Gio.BusType = Gio.BusType.SESSION) -> None:
        super().__init__()
        self._bus_type = bus_type
        self._proxy: Optional[Gio.DBusProxy] = None
        self._raw_values = False
        self._no_verify = False
        self._client_name = "GnomeDDC"
        self._client_context = self._generate_context()
        self._mock = os.environ.get("GNOMEDDC_FORCE_MOCK") == "1"
        if self._mock:
            self._setup_mock()
        else:
            self._connect_proxy()

    # -- Public toggles -------------------------------------------------
    @property
    def raw_values(self) -> bool:
        return self._raw_values

    @raw_values.setter
    def raw_values(self, value: bool) -> None:
        self._raw_values = value

    @property
    def no_verify(self) -> bool:
        return self._no_verify

    @no_verify.setter
    def no_verify(self, value: bool) -> None:
        self._no_verify = value

    # -- Proxy management -----------------------------------------------
    def _connect_proxy(self) -> None:
        try:
            self._proxy = Gio.DBusProxy.new_for_bus_sync(
                self._bus_type,
                Gio.DBusProxyFlags.NONE,
                None,
                SERVICE_NAME,
                OBJECT_PATH,
                INTERFACE,
                None,
            )
            self._subscribe_to_signals()
        except GLib.Error as err:  # pragma: no cover - runtime only
            _LOGGER.warning("Falling back to mock backend: %s", err.message)
            self._mock = True
            self._setup_mock()

    def _generate_context(self) -> str:
        rng = random.SystemRandom()
        return "gnomeddc-" + "".join(rng.choice(string.ascii_lowercase) for _ in range(6))

    def _subscribe_to_signals(self) -> None:
        if not self._proxy:
            return
        bus = self._proxy.get_connection()
        if not bus:
            return

        def _on_signal(
            connection: Gio.DBusConnection,
            sender_name: str,
            object_path: str,
            interface_name: str,
            signal_name: str,
            parameters: GLib.Variant,
        ) -> None:
            if signal_name == "ServiceInitialized":
                self.emit("service-initialized")
            elif signal_name == "ConnectedDisplaysChanged":
                edid_txt, event_type, flags = parameters.unpack()
                self.emit("connected-displays-changed", edid_txt, event_type, flags)
            elif signal_name == "VcpValueChanged":
                args = parameters.unpack()
                self.emit("vcp-value-changed", *args)

        bus.signal_subscribe(
            None,
            INTERFACE,
            None,
            OBJECT_PATH,
            None,
            Gio.DBusSignalFlags.NONE,
            _on_signal,
        )

    # -- Mock backend ---------------------------------------------------
    def _setup_mock(self) -> None:
        self._proxy = None
        self._mock_state: Dict[str, Monitor] = {}
        for index, name in enumerate(["MockView", "ColorPro", "UltraWide"], start=1):
            edid = base64.b64encode(f"MOCK{name}{index:02d}".encode("utf-8")).decode("ascii")
            monitor = Monitor(
                display_number=index,
                edid=edid,
                model=f"{name} {index}",
                mccs_version="2.2",
            )
            monitor.capabilities = {
                0x10: VcpValue(
                    code=0x10,
                    current=40,
                    maximum=100,
                    label="Brightness",
                    is_continuous=True,
                    supports_reset=True,
                    default_value=50,
                ),
                0x12: VcpValue(
                    code=0x12,
                    current=50,
                    maximum=100,
                    label="Contrast",
                    is_continuous=True,
                    supports_reset=True,
                    default_value=50,
                ),
                0x14: VcpValue(
                    code=0x14,
                    current=20,
                    maximum=100,
                    label="Select Color Preset",
                    is_continuous=False,
                    is_complex=True,
                    options={
                        0x01: "sRGB",
                        0x03: "Warm",
                        0x04: "Cool",
                    },
                ),
                0x60: VcpValue(
                    code=0x60,
                    current=0x0f,
                    maximum=0xff,
                    label="Input Source",
                    is_continuous=False,
                    options={
                        0x0f: "DisplayPort",
                        0x11: "HDMI 1",
                        0x12: "HDMI 2",
                    },
                ),
            }
            monitor.attributes = {
                "bus": "i2c-9",
                "address": "0x37",
                "display_number": index,
                "manufacturer": "GnomeDisplay",
                "serial": f"{name[:3].upper()}-{index:03d}",
            }
            self._mock_state[monitor.edid] = monitor

    # -- Utilities ------------------------------------------------------
    def _call(self, method: str, parameters: Sequence[Any] = ()) -> Any:
        if self._mock:
            return self._mock_call(method, parameters)
        if not self._proxy:
            raise RuntimeError("D-Bus proxy is not available")
        flags = self._build_flags()
        params_variant = self._wrap_parameters(method, parameters, flags)
        try:
            result = self._proxy.call_sync(method, params_variant, Gio.DBusCallFlags.NONE, -1, None)
            unpacked = result.unpack()
        except GLib.Error as err:  # pragma: no cover - runtime only
            _LOGGER.error("D-Bus call %s failed: %s", method, err.message)
            if not self._mock:
                self._mock = True
                self._setup_mock()
                return self._mock_call(method, parameters)
            raise RuntimeError(err.message) from err
        status = unpacked[0]
        message = unpacked[1]
        if status:
            raise DdcutilError(status, message)
        return unpacked

    def _build_flags(self) -> int:
        flags = 0
        if self._raw_values:
            flags |= 2
        if self._no_verify:
            flags |= 4
        return flags

    def _wrap_parameters(self, method: str, params: Sequence[Any], flags: int) -> GLib.Variant:
        signature_map = {
            "Detect": "(i)",
            "ListDetected": "()",
            "GetCapabilitiesString": "(is)",
            "GetCapabilitiesMetadata": "(is)",
            "GetVcp": "(isi)",
            "GetMultipleVcp": "(iaus)",
            "SetVcp": "(isis)",
            "SetVcpWithContext": "(isisi)",
            "GetVcpMetadata": "(isi)",
            "GetSleepMultiplier": "(is)",
            "SetSleepMultiplier": "(isii)",
            "Restart": "(ssi)",
        }
        signature = signature_map.get(method)
        if signature is None:
            raise ValueError(f"Unknown method signature for {method}")
        if method in {"Detect", "ListDetected"}:
            return GLib.Variant(signature, (*params,))
        if method == "Restart":
            text_options, syslog_level = params
            return GLib.Variant(signature, (text_options, syslog_level, flags))
        if method == "SetVcpWithContext":
            display_number, edid_txt, vcp_code, value = params
            context = self._client_context
            return GLib.Variant(signature, (display_number, edid_txt, vcp_code, context, value))
        if method == "SetVcp":
            display_number, edid_txt, vcp_code, value = params
            return GLib.Variant(signature, (display_number, edid_txt, vcp_code, flags, value))
        return GLib.Variant(signature, (*params,))

    # -- Mock calls -----------------------------------------------------
    def _mock_call(self, method: str, params: Sequence[Any]) -> Tuple[int, str, Any]:
        if method == "Detect":
            flags = params[0] if params else 0
            return self._mock_detect(flags)
        if method == "ListDetected":
            return (0, "OK", [self._monitor_to_detect_tuple(m) for m in self._mock_state.values()])
        if method == "GetCapabilitiesString":
            display_number, edid_txt = params
            monitor = self._mock_state.get(edid_txt)
            capabilities = "".join(
                f"(vcp {value.code:02X})" for value in monitor.capabilities.values()
            )
            return (0, "OK", capabilities, "", {})
        if method == "GetCapabilitiesMetadata":
            display_number, edid_txt = params
            monitor = self._mock_state.get(edid_txt)
            metadata = {
                "features": [
                    {
                        "code": f"0x{value.code:02X}",
                        "name": value.label,
                        "is_continuous": value.is_continuous,
                        "is_complex": value.is_complex,
                        "values": {f"0x{code:02X}": label for code, label in value.options.items()},
                        "maximum": value.maximum,
                        "current": value.current,
                        "default": value.default_value,
                        "has_default": value.default_value is not None,
                    }
                    for value in monitor.capabilities.values()
                ]
            }
            return (0, "OK", json.dumps(metadata))
        if method == "GetVcpMetadata":
            display_number, edid_txt, vcp_code = params
            monitor = self._mock_state.get(edid_txt)
            value = monitor.capabilities.get(vcp_code)
            metadata = {
                "code": f"0x{vcp_code:02X}",
                "name": value.label,
                "is_continuous": value.is_continuous,
                "is_complex": value.is_complex,
                "values": {f"0x{k:02X}": v for k, v in value.options.items()},
            }
            return (0, "OK", json.dumps(metadata))
        if method == "GetVcp":
            display_number, edid_txt, vcp_code = params
            monitor = self._mock_state[edid_txt]
            value = monitor.capabilities[vcp_code]
            return (0, "OK", value.current, value.maximum, value.default_value or 0, 0)
        if method == "GetMultipleVcp":
            display_number, codes, edid_txt = params
            monitor = self._mock_state[edid_txt]
            results = []
            for code in codes:
                v = monitor.capabilities.get(code)
                if not v:
                    continue
                results.append((code, v.current, v.maximum, v.default_value or 0, 0))
            return (0, "OK", results)
        if method in {"SetVcp", "SetVcpWithContext"}:
            display_number, edid_txt, vcp_code, value = params[:4]
            monitor = self._mock_state[edid_txt]
            monitor.capabilities[vcp_code].current = int(value)
            return (0, "OK")
        if method == "GetSleepMultiplier":
            display_number, edid_txt = params
            return (0, "OK", 1)
        if method == "SetSleepMultiplier":
            return (0, "OK")
        if method == "Restart":
            return (0, "OK", 0, "Restart mocked")
        raise NotImplementedError(method)

    def _mock_detect(self, flags: int) -> Tuple[int, str, List[Tuple[Any, ...]]]:
        result = [self._monitor_to_detect_tuple(m) for m in self._mock_state.values()]
        return (0, "OK", result)

    @staticmethod
    def _monitor_to_detect_tuple(monitor: Monitor) -> Tuple[Any, ...]:
        return (
            monitor.display_number,
            monitor.edid,
            monitor.model,
            monitor.mccs_version,
            json.dumps(monitor.attributes),
        )

    # -- Public API -----------------------------------------------------
    def detect(self, flags: int = 0) -> List[Monitor]:
        status, message, result = self._call("Detect", (flags,))
        return self._parse_detect(result)

    def list_detected(self) -> List[Monitor]:
        status, message, result = self._call("ListDetected")
        return self._parse_detect(result)

    def get_capabilities(self, monitor: Monitor) -> Monitor:
        status, message, caps_string, metadata_json, extra = self._call(
            "GetCapabilitiesString",
            (monitor.display_number, monitor.edid),
        )
        metadata = {}
        if metadata_json:
            try:
                metadata = json.loads(metadata_json)
            except json.JSONDecodeError:
                metadata = {}
        monitor.mccs_version = extra.get("mccs_version", monitor.mccs_version)
        features = self._build_vcp_values(metadata)
        if not features:
            features = self._parse_capabilities_string(caps_string)
        monitor.capabilities.update(features)
        return monitor

    def get_vcp_metadata(self, monitor: Monitor, code: int) -> Dict[str, Any]:
        status, message, metadata_json = self._call(
            "GetVcpMetadata",
            (monitor.display_number, monitor.edid, code),
        )
        return json.loads(metadata_json) if metadata_json else {}

    def get_multiple_vcp(self, monitor: Monitor, codes: Iterable[int]) -> Dict[int, Tuple[int, int, int]]:
        codes_list = list(codes)
        status, message, results = self._call(
            "GetMultipleVcp",
            (monitor.display_number, codes_list, monitor.edid),
        )
        values: Dict[int, Tuple[int, int, int]] = {}
        for code, current, maximum, default, flags in results:
            values[code] = (current, maximum, default)
        return values

    def _parse_capabilities_string(self, caps_string: str) -> Dict[int, VcpValue]:
        values: Dict[int, VcpValue] = {}
        pattern = re.compile(r"vcp\s*\(([^)]*)\)")
        for block in pattern.findall(caps_string or ""):
            for token in block.split():
                token = token.strip()
                if not token:
                    continue
                try:
                    code = int(token, 16)
                except ValueError:
                    continue
                values[code] = VcpValue(
                    code=code,
                    current=0,
                    maximum=100,
                    label=f"VCP 0x{code:02X}",
                )
        return values

    def set_vcp(self, monitor: Monitor, code: int, value: int) -> None:
        self._call(
            "SetVcpWithContext",
            (monitor.display_number, monitor.edid, code, value),
        )

    def get_sleep_multiplier(self, monitor: Monitor) -> int:
        status, message, multiplier = self._call(
            "GetSleepMultiplier",
            (monitor.display_number, monitor.edid),
        )
        return multiplier

    def set_sleep_multiplier(self, monitor: Monitor, multiplier: int) -> None:
        self._call(
            "SetSleepMultiplier",
            (monitor.display_number, monitor.edid, multiplier, self._build_flags()),
        )

    def restart_service(self, text_options: str, syslog_level: int) -> Tuple[int, str]:
        status, message, error_status, error_message = self._call(
            "Restart",
            (text_options, syslog_level),
        )
        return error_status, error_message

    # -- Parsing helpers ------------------------------------------------
    def _parse_detect(self, result: Sequence[Tuple[Any, ...]]) -> List[Monitor]:
        monitors: List[Monitor] = []
        for entry in result:
            display_number, edid_txt, model_name, mccs_version, attributes_json = entry
            attributes = json.loads(attributes_json) if attributes_json else {}
            monitors.append(
                Monitor(
                    display_number=display_number,
                    edid=edid_txt,
                    model=model_name,
                    mccs_version=mccs_version,
                    attributes=attributes,
                )
            )
        return monitors

    def _build_vcp_values(self, metadata: Dict[str, Any]) -> Dict[int, VcpValue]:
        values: Dict[int, VcpValue] = {}
        for entry in metadata.get("features", []):
            code = int(entry.get("code", "0"), 0) if isinstance(entry.get("code"), str) else entry.get("code")
            if code is None:
                continue
            values[code] = VcpValue(
                code=code,
                current=entry.get("current", 0),
                maximum=entry.get("maximum", 100),
                label=entry.get("name", f"VCP 0x{code:02X}"),
                is_continuous=entry.get("is_continuous", True),
                is_complex=entry.get("is_complex", False),
                options={int(k): v for k, v in entry.get("values", {}).items()},
                read_only=entry.get("is_read_only", False),
                write_only=entry.get("is_write_only", False),
                supports_reset=entry.get("has_default", False),
                default_value=entry.get("default"),
            )
        return values


class MonitorStore(GObject.GObject):
    """State container for monitors and VCP values."""

    __gsignals__ = {
        "monitors-changed": (GObject.SignalFlags.RUN_LAST, None, ()),
        "monitor-updated": (GObject.SignalFlags.RUN_LAST, None, (str,)),
    }

    def __init__(self, service: DdcutilServiceClient) -> None:
        super().__init__()
        self.service = service
        self.monitors: Dict[str, Monitor] = {}
        self._order: List[str] = []

    def refresh(self, use_list: bool = False) -> None:
        try:
            if use_list:
                monitors = self.service.list_detected()
            else:
                monitors = self.service.detect()
        except Exception as err:  # pragma: no cover - runtime only
            _LOGGER.error("Monitor refresh failed: %s", err)
            return
        self.monitors = {monitor.edid: monitor for monitor in monitors}
        self._order = [monitor.edid for monitor in monitors]
        self.emit("monitors-changed")

    def get_monitor(self, edid: str) -> Optional[Monitor]:
        return self.monitors.get(edid)

    def ensure_capabilities(self, edid: str) -> Optional[Monitor]:
        monitor = self.monitors.get(edid)
        if monitor is None:
            return None
        if not monitor.capabilities:
            self.service.get_capabilities(monitor)
        return monitor

    @property
    def order(self) -> List[str]:
        return list(self._order)


class Throttle:
    """Simple GLib timeout-based throttle for slider writes."""

    def __init__(self, delay_ms: int, callback: Callable[[Any], None]) -> None:
        self.delay_ms = delay_ms
        self.callback = callback
        self._source_id: Optional[int] = None
        self._last_value: Any = None

    def update(self, value: Any) -> None:
        self._last_value = value
        if self._source_id is not None:
            GLib.source_remove(self._source_id)
        self._source_id = GLib.timeout_add(self.delay_ms, self._fire)

    def _fire(self) -> bool:
        if self._last_value is not None:
            self.callback(self._last_value)
        self._source_id = None
        return GLib.SOURCE_REMOVE


class MonitorRow(Adw.ActionRow):
    """List row representing a monitor."""

    def __init__(self, monitor: Monitor) -> None:
        super().__init__()
        self.set_title(monitor.model)
        self.set_subtitle(f"EDID {monitor.short_hash()}")
        badge = Gtk.Label(label=monitor.mccs_version or "MCCS ?")
        badge.add_css_class("badge")
        self.add_suffix(badge)


class VcpSliderRow(Adw.ActionRow):
    """Row containing a slider for continuous VCP values."""

    def __init__(
        self,
        monitor: Monitor,
        value: VcpValue,
        on_changed: Callable[[int], None],
        on_reset: Optional[Callable[[], None]] = None,
    ) -> None:
        super().__init__()
        self._monitor = monitor
        self._value = value
        self._throttle = Throttle(150, lambda val: on_changed(int(val)))
        self.set_title(value.label)
        self._scale = Gtk.Scale.new_with_range(Gtk.Orientation.HORIZONTAL, 0, value.maximum, 1)
        self._scale.set_value(value.current)
        self._scale.set_hexpand(True)
        self._scale.connect("value-changed", self._on_value_changed)
        self.add_suffix(self._scale)
        self._value_label = Gtk.Label(label=str(value.current))
        self._value_label.add_css_class("dim-label")
        self.add_suffix(self._value_label)
        if on_reset and value.supports_reset and value.default_value is not None:
            reset_button = Gtk.Button.new_with_label("Reset")
            reset_button.add_css_class("flat")
            reset_button.connect("clicked", lambda _btn: on_reset())
            self.add_suffix(reset_button)
        if value.read_only:
            self.set_sensitive(False)

    def _on_value_changed(self, scale: Gtk.Scale) -> None:
        value = int(scale.get_value())
        self._value_label.set_text(str(value))
        self._throttle.update(value)

    def update(self, current: int, maximum: int) -> None:
        self._scale.set_range(0, maximum)
        self._scale.set_value(current)
        self._value_label.set_text(str(current))


class VcpEnumRow(Adw.ActionRow):
    """Row containing a dropdown for enumerated VCP values."""

    def __init__(self, monitor: Monitor, value: VcpValue, on_changed: Callable[[int], None]) -> None:
        super().__init__()
        self.set_title(value.label)
        self._value = value
        self._model = Gio.ListStore(item_type=Gtk.StringObject)
        for code, label in value.options.items():
            self._model.append(Gtk.StringObject.new(f"0x{code:02X} · {label}"))
        self._dropdown = Gtk.DropDown.new(
            model=self._model,
            expression=Gtk.PropertyExpression.new(Gtk.StringObject, None, "string"),
        )
        codes = list(value.options.keys())
        try:
            index = codes.index(value.current)
        except ValueError:
            index = 0
        self._dropdown.set_selected(index)
        self._dropdown.connect(
            "notify::selected",
            lambda dropdown, _pspec: on_changed(codes[dropdown.get_selected()]),
        )
        self.add_suffix(self._dropdown)
        if value.read_only:
            self.set_sensitive(False)

    def update(self, current: int) -> None:
        codes = list(self._value.options.keys())
        if current in codes:
            self._dropdown.set_selected(codes.index(current))


class EdidView(Gtk.TextView):
    def __init__(self) -> None:
        super().__init__()
        self.set_monospace(True)
        self.set_editable(False)
        self.get_style_context().add_class("card")

    def set_edid(self, edid: str) -> None:
        decoded = base64.b64decode(edid)
        summary = textwrap.fill(decoded.hex(), width=32)
        buffer = self.get_buffer()
        buffer.set_text(f"Base64:\n{edid}\n\nHex:\n{summary}")


class CapabilityView(Gtk.Box):
    def __init__(self) -> None:
        super().__init__(orientation=Gtk.Orientation.VERTICAL, spacing=12)
        self.set_margin_top(12)
        self.set_margin_bottom(12)
        self.set_margin_start(12)
        self.set_margin_end(12)
        self.rows: Dict[int, Adw.ActionRow] = {}

    def bind(self, monitor: Monitor, on_slider: Callable[[int, int], None], on_enum: Callable[[int, int], None]) -> None:
        for child in list(self.get_children()):
            self.remove(child)
        self.rows.clear()
        for code, value in sorted(monitor.capabilities.items()):
            if value.is_continuous:
                row = VcpSliderRow(
                    monitor,
                    value,
                    on_changed=functools.partial(on_slider, code),
                    on_reset=lambda c=code, v=value: on_slider(c, v.default_value or 0),
                )
            else:
                row = VcpEnumRow(
                    monitor,
                    value,
                    on_changed=functools.partial(on_enum, code),
                )
            row.add_css_class("card")
            self.append(row)
            self.rows[code] = row

    def update_value(self, code: int, current: int, maximum: int) -> None:
        row = self.rows.get(code)
        if isinstance(row, VcpSliderRow):
            row.update(current, maximum)
        elif isinstance(row, VcpEnumRow):
            row.update(current)


class MonitorDetail(Adw.Bin):
    """Detail pane for a monitor."""

    def __init__(self, service: DdcutilServiceClient, store: MonitorStore) -> None:
        super().__init__()
        self.service = service
        self.store = store
        self.current_edid: Optional[str] = None

        self._header = Adw.HeaderBar()
        self._header.set_show_end_title_buttons(True)
        self._rescan_button = Gtk.Button.new_with_label("Rescan")
        self._rescan_button.connect("clicked", self._on_rescan)
        self._header.pack_end(self._rescan_button)

        self._view_stack = Adw.ViewStack()
        self._view_switcher = Adw.ViewSwitcher(title="Monitor Pages")
        self._view_switcher.set_stack(self._view_stack)

        controls_page = Adw.Clamp()
        self._capabilities = CapabilityView()
        controls_page.set_child(self._capabilities)
        self._view_stack.add_titled(controls_page, "controls", "Controls")

        self._edid_view = EdidView()
        self._view_stack.add_titled(self._edid_view, "edid", "EDID")

        self._attributes_grid = Gtk.Grid(column_spacing=12, row_spacing=6, margin_top=12, margin_bottom=12, margin_start=12, margin_end=12)
        self._view_stack.add_titled(self._attributes_grid, "attributes", "Attributes")

        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=12)
        box.append(self._header)
        box.append(self._view_switcher)
        box.append(self._view_stack)
        self.set_child(box)

    def show_monitor(self, edid: str) -> None:
        monitor = self.store.ensure_capabilities(edid)
        if monitor is None:
            return
        self.current_edid = edid
        try:
            values = self.service.get_multiple_vcp(monitor, monitor.capabilities.keys())
        except Exception as err:  # pragma: no cover - runtime only
            _LOGGER.warning("Failed to fetch VCP values: %s", err)
            values = {}
        for code, (current, maximum, default) in values.items():
            feature = monitor.capabilities.get(code)
            if feature:
                feature.current = current
                feature.maximum = maximum
                if default:
                    feature.default_value = default
        self._edid_view.set_edid(monitor.edid)
        self._capabilities.bind(
            monitor,
            on_slider=self._on_slider_changed,
            on_enum=self._on_enum_changed,
        )
        self._populate_attributes(monitor)

    def _populate_attributes(self, monitor: Monitor) -> None:
        for child in list(self._attributes_grid.get_children()):
            self._attributes_grid.remove(child)
        for index, (key, value) in enumerate(sorted(monitor.attributes.items())):
            label = Gtk.Label(label=key.capitalize(), xalign=0)
            value_label = Gtk.Label(label=str(value), xalign=0)
            value_label.add_css_class("dim-label")
            self._attributes_grid.attach(label, 0, index, 1, 1)
            self._attributes_grid.attach(value_label, 1, index, 1, 1)

    def _on_rescan(self, button: Gtk.Button) -> None:
        self.store.refresh()

    def _on_slider_changed(self, code: int, value: int) -> None:
        if self.current_edid is None:
            return
        monitor = self.store.get_monitor(self.current_edid)
        if not monitor:
            return
        self.service.set_vcp(monitor, code, value)

    def _on_enum_changed(self, code: int, value: int) -> None:
        if self.current_edid is None:
            return
        monitor = self.store.get_monitor(self.current_edid)
        if not monitor:
            return
        self.service.set_vcp(monitor, code, value)

    def update_vcp(self, edid: str, code: int, current: int, maximum: int) -> None:
        if self.current_edid != edid:
            return
        self._capabilities.update_value(code, current, maximum)


class ServiceOptionsView(Adw.PreferencesPage):
    def __init__(self, service: DdcutilServiceClient) -> None:
        super().__init__()
        self.service = service
        group = Adw.PreferencesGroup(title="Developer Options")
        self.append(group)

        self._raw_switch = Gtk.Switch()
        self._raw_switch.set_active(False)
        self._raw_switch.connect("notify::active", self._on_raw_changed)
        row = Adw.ActionRow(title="Return raw values", subtitle="Use flag 2 when reading VCP values")
        row.add_suffix(self._raw_switch)
        group.add(row)

        self._verify_switch = Gtk.Switch(active=False)
        self._verify_switch.connect("notify::active", self._on_verify_changed)
        row = Adw.ActionRow(title="Skip verify on write", subtitle="Use flag 4 when setting values")
        row.add_suffix(self._verify_switch)
        group.add(row)

    def _on_raw_changed(self, switch: Gtk.Switch, _pspec: GObject.ParamSpec) -> None:
        self.service.raw_values = switch.get_active()

    def _on_verify_changed(self, switch: Gtk.Switch, _pspec: GObject.ParamSpec) -> None:
        self.service.no_verify = switch.get_active()


class GnomeDdcApplication(Adw.Application):
    """The main Libadwaita application."""

    def __init__(self) -> None:
        super().__init__(application_id="org.gnomeddc.App", flags=Gio.ApplicationFlags.FLAGS_NONE)
        self.service = DdcutilServiceClient()
        self.store = MonitorStore(self.service)
        self.window: Optional[Adw.ApplicationWindow] = None

    def do_startup(self) -> None:  # pragma: no cover - GTK hook
        Adw.Application.do_startup(self)
        action = Gio.SimpleAction.new("about", None)
        action.connect("activate", self._on_about)
        self.add_action(action)

    def do_activate(self) -> None:  # pragma: no cover - GTK hook
        if not self.window:
            self.window = self._build_window()
        self.window.present()
        self.store.refresh()

    def _build_window(self) -> Adw.ApplicationWindow:
        window = Adw.ApplicationWindow(application=self)
        window.set_default_size(1100, 720)
        window.set_title(f"GnomeDDC {__version__}")

        split_view = Adw.NavigationSplitView()
        sidebar = Adw.NavigationPage()
        sidebar.set_title("Displays")
        sidebar_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=12, margin_top=12, margin_bottom=12, margin_start=12, margin_end=12)

        self._monitor_list = Gtk.ListBox()
        self._monitor_list.add_css_class("boxed-list")
        self._monitor_list.set_selection_mode(Gtk.SelectionMode.SINGLE)
        self._monitor_list.connect("row-selected", self._on_monitor_selected)
        sidebar_box.append(self._monitor_list)

        service_options = ServiceOptionsView(self.service)
        sidebar_box.append(service_options)

        sidebar.set_child(sidebar_box)
        split_view.set_sidebar(sidebar)

        self._monitor_detail = MonitorDetail(self.service, self.store)
        split_view.set_content(self._monitor_detail)

        window.set_content(split_view)

        self.store.connect("monitors-changed", self._on_monitors_changed)
        self.service.connect("vcp-value-changed", self._on_vcp_changed)
        self.service.connect("connected-displays-changed", self._on_connected_changed)
        self.service.connect("service-initialized", lambda *_args: self.store.refresh())

        return window

    def _on_monitors_changed(self, store: MonitorStore) -> None:
        for child in list(self._monitor_list.get_children()):
            self._monitor_list.remove(child)
        for edid in store.order:
            monitor = store.monitors[edid]
            row_widget = Gtk.ListBoxRow()
            row_widget.set_selectable(True)
            row_widget.set_child(MonitorRow(monitor))
            self._monitor_list.append(row_widget)
        if store.order:
            first = store.order[0]
            self._monitor_list.select_row(self._monitor_list.get_row_at_index(0))
            self._monitor_detail.show_monitor(first)

    def _on_monitor_selected(self, listbox: Gtk.ListBox, row: Optional[Gtk.ListBoxRow]) -> None:
        if row is None:
            return
        index = row.get_index()
        edids = self.store.order
        if 0 <= index < len(edids):
            self._monitor_detail.show_monitor(edids[index])

    def _on_vcp_changed(
        self,
        service: DdcutilServiceClient,
        display_number: int,
        edid_txt: str,
        vcp_code: int,
        vcp_new_value: int,
        source_client_name: str,
        source_client_context: str,
        flags: int,
    ) -> None:
        monitor = self.store.get_monitor(edid_txt)
        if not monitor:
            return
        value = monitor.capabilities.get(vcp_code)
        if value:
            value.current = vcp_new_value
            self._monitor_detail.update_vcp(edid_txt, vcp_code, vcp_new_value, value.maximum)
        toast = Adw.Toast.new(
            f"{value.label if value else f'0x{vcp_code:02X}'} updated by {source_client_name or 'unknown'}"
        )
        if self.window:
            self.window.add_toast(toast)

    def _on_connected_changed(self, *args: Any) -> None:
        self.store.refresh(use_list=True)

    def _on_about(self, action: Gio.SimpleAction, parameter: Optional[GLib.Variant]) -> None:
        if not self.window:
            return
        about = Adw.AboutWindow(
            application_name="GnomeDDC",
            application_icon="org.gnomeddc.App",
            developer_name="GnomeDDC Team",
            version=__version__,
            issue_url="https://github.com/example/gnomeddc",
            copyright="© 2024 GnomeDDC",
        )
        about.present()


def main() -> int:
    """Entry point."""
    logging.basicConfig(level=logging.INFO)
    app = GnomeDdcApplication()
    return app.run()


__all__ = ["main", "GnomeDdcApplication"]
