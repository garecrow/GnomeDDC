"""D-Bus client bindings for ddcutil-service.

This module centralises all D-Bus interactions used by the UI.  It exposes a
`DdcutilClient` class that wraps the D-Bus proxy and provides higher level
Pythonic helpers for common service calls.  All methods are designed to be safe
when the service is unavailable – errors are captured and surfaced via
callbacks so the UI can keep running in a degraded mode.

The client uses GLib's asynchronous primitives so the UI never blocks on I/O.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, Callable, Dict, Iterable, List, Mapping, Optional, Tuple

import gi

gi.require_version("Gio", "2.0")

from gi.repository import Gio, GLib, GObject

# Constants that mirror the public D-Bus API.
SERVICE_NAME = "com.ddcutil.DdcutilService"
OBJECT_PATH = "/com/ddcutil/DdcutilObject"
INTERFACE_NAME = "com.ddcutil.DdcutilInterface"


class DdcutilError(RuntimeError):
    """Raised when a D-Bus call returns an error payload."""

    def __init__(self, status: int, message: str) -> None:
        super().__init__(message)
        self.status = status
        self.message = message


@dataclass
class MonitorDescriptor:
    """Basic information about a detected monitor."""

    display_number: int
    edid: str
    manufacturer: str = ""
    model: str = ""
    serial: str = ""
    bus: str = ""
    address: str = ""
    attributes: Mapping[str, Any] = field(default_factory=dict)

    @property
    def key(self) -> Tuple[int, str]:
        return (self.display_number, self.edid)

    @property
    def short_edid(self) -> str:
        return self.edid[:12]

    @property
    def display_label(self) -> str:
        friendly = f"{self.model or 'Display'}"
        if self.serial:
            friendly += f" · {self.serial}"
        return friendly


@dataclass
class VcpMetadata:
    """Metadata describing a single VCP feature."""

    code: int
    name: str = ""
    is_continuous: bool = False
    is_complex: bool = False
    is_read_only: bool = False
    is_write_only: bool = False
    is_rw: bool = True
    has_factory_default: bool = False
    has_user_default: bool = False
    default_value: Optional[int] = None
    maximum: Optional[int] = None
    minimum: Optional[int] = None
    values: Mapping[int, str] = field(default_factory=dict)

    @classmethod
    def from_mapping(cls, code: int, payload: Mapping[str, Any]) -> "VcpMetadata":
        return cls(
            code=code,
            name=str(payload.get("name", f"VCP 0x{code:02X}")),
            is_continuous=bool(payload.get("is_continuous")),
            is_complex=bool(payload.get("is_complex")),
            is_read_only=bool(payload.get("is_read_only")),
            is_write_only=bool(payload.get("is_write_only")),
            is_rw=bool(payload.get("is_rw", True)),
            has_factory_default=bool(payload.get("has_factory_default")),
            has_user_default=bool(payload.get("has_user_default")),
            default_value=payload.get("default_value"),
            maximum=payload.get("maximum"),
            minimum=payload.get("minimum"),
            values={int(k): str(v) for k, v in payload.get("values", {}).items()},
        )

    def can_write(self) -> bool:
        if self.is_read_only:
            return False
        if self.is_write_only:
            return True
        return self.is_rw


class DdcutilClient(GObject.GObject):
    """High level wrapper around the ddcutil-service D-Bus interface."""

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
        "service-error": (GObject.SignalFlags.RUN_LAST, None, (str,)),
    }

    def __init__(self) -> None:
        super().__init__()
        self._proxy: Optional[Gio.DBusProxy] = None
        self._raw_values: bool = False
        self._no_verify: bool = False
        self._connect_to_service()

    # ------------------------------------------------------------------
    # Public configuration flags
    # ------------------------------------------------------------------
    @property
    def raw_values(self) -> bool:
        return self._raw_values

    @raw_values.setter
    def raw_values(self, value: bool) -> None:
        self._raw_values = bool(value)

    @property
    def no_verify(self) -> bool:
        return self._no_verify

    @no_verify.setter
    def no_verify(self, value: bool) -> None:
        self._no_verify = bool(value)

    # ------------------------------------------------------------------
    # Service binding
    # ------------------------------------------------------------------
    def _connect_to_service(self) -> None:
        def _on_proxy_ready(source: Gio.DBusProxy, result: Gio.AsyncResult) -> None:
            try:
                proxy = Gio.DBusProxy.new_for_bus_finish(result)
            except GLib.Error as error:  # pragma: no cover - relies on DBus
                self.emit("service-error", error.message)
                return

            self._proxy = proxy
            self._proxy.connect("g-signal", self._on_g_signal)
            self.emit("service-initialized")

        Gio.DBusProxy.new_for_bus(
            Gio.BusType.SYSTEM,
            Gio.DBusProxyFlags.NONE,
            None,
            SERVICE_NAME,
            OBJECT_PATH,
            INTERFACE_NAME,
            None,
            _on_proxy_ready,
        )

    def _on_g_signal(
        self,
        proxy: Gio.DBusProxy,
        sender_name: str,
        signal_name: str,
        parameters: GLib.Variant,
    ) -> None:
        if signal_name == "ServiceInitialized":
            self.emit("service-initialized")
            return
        if signal_name == "ConnectedDisplaysChanged":
            edid_txt, event_type, flags = parameters.unpack()
            self.emit("connected-displays-changed", edid_txt, event_type, flags)
            return
        if signal_name == "VcpValueChanged":
            (
                display_number,
                edid_txt,
                vcp_code,
                vcp_new_value,
                source_client_name,
                source_client_context,
                flags,
            ) = parameters.unpack()
            self.emit(
                "vcp-value-changed",
                display_number,
                edid_txt,
                vcp_code,
                vcp_new_value,
                source_client_name,
                source_client_context,
                flags,
            )
            return

    # ------------------------------------------------------------------
    # Helper utilities
    # ------------------------------------------------------------------
    def _call_sync(
        self,
        method: str,
        parameters: Optional[GLib.Variant] = None,
        timeout: int = -1,
    ) -> GLib.Variant:
        if not self._proxy:
            raise RuntimeError("Service proxy is not available")
        try:
            return self._proxy.call_sync(method, parameters, Gio.DBusCallFlags.NONE, timeout, None)
        except GLib.Error as error:  # pragma: no cover - DBus runtime
            raise RuntimeError(error.message) from error

    def _prepare_flags(self, base_flags: int = 0) -> int:
        flags = base_flags
        if self._raw_values:
            flags |= 2  # RETURN_RAW_VALUES
        if self._no_verify:
            flags |= 4  # NO_VERIFY
        return flags

    # ------------------------------------------------------------------
    # Monitor enumeration
    # ------------------------------------------------------------------
    def detect(self, flags: int = 0) -> List[MonitorDescriptor]:
        response = self._call_sync("Detect", GLib.Variant("(i)", (flags,)))
        monitors_payload, error_status, error_message = response.unpack()
        self._raise_if_error(error_status, error_message)

        monitors: List[MonitorDescriptor] = []
        for item in monitors_payload:
            # Each item is expected to be a dict mapping.
            mapping = item
            monitors.append(
                MonitorDescriptor(
                    display_number=int(mapping.get("display_number", -1)),
                    edid=str(mapping.get("edid", "")),
                    manufacturer=str(mapping.get("manufacturer", "")),
                    model=str(mapping.get("model", "")),
                    serial=str(mapping.get("serial", "")),
                    bus=str(mapping.get("bus", "")),
                    address=str(mapping.get("address", "")),
                    attributes=dict(mapping),
                )
            )
        return monitors

    def list_detected(self) -> List[MonitorDescriptor]:
        response = self._call_sync("ListDetected", None)
        monitors_payload = response.unpack()[0]
        monitors: List[MonitorDescriptor] = []
        for mapping in monitors_payload:
            monitors.append(
                MonitorDescriptor(
                    display_number=int(mapping.get("display_number", -1)),
                    edid=str(mapping.get("edid", "")),
                    manufacturer=str(mapping.get("manufacturer", "")),
                    model=str(mapping.get("model", "")),
                    serial=str(mapping.get("serial", "")),
                    attributes=dict(mapping),
                )
            )
        return monitors

    # ------------------------------------------------------------------
    # Capabilities & metadata
    # ------------------------------------------------------------------
    def get_capabilities_string(self, descriptor: MonitorDescriptor) -> str:
        response = self._call_sync(
            "GetCapabilitiesString",
            GLib.Variant("(is)", (descriptor.display_number, descriptor.edid)),
        )
        result, error_status, error_message = response.unpack()
        self._raise_if_error(error_status, error_message)
        return str(result)

    def get_capabilities_metadata(self, descriptor: MonitorDescriptor) -> Mapping[int, VcpMetadata]:
        response = self._call_sync(
            "GetCapabilitiesMetadata",
            GLib.Variant("(is)", (descriptor.display_number, descriptor.edid)),
        )
        payload, error_status, error_message = response.unpack()
        self._raise_if_error(error_status, error_message)
        metadata: Dict[int, VcpMetadata] = {}
        for code_str, mapping in payload.items():
            code = int(code_str, 0) if isinstance(code_str, str) else int(code_str)
            metadata[code] = VcpMetadata.from_mapping(code, mapping)
        return metadata

    def get_vcp_metadata(self, descriptor: MonitorDescriptor, code: int) -> VcpMetadata:
        response = self._call_sync(
            "GetVcpMetadata",
            GLib.Variant("(iis)", (descriptor.display_number, code, descriptor.edid)),
        )
        payload, error_status, error_message = response.unpack()
        self._raise_if_error(error_status, error_message)
        return VcpMetadata.from_mapping(code, payload)

    # ------------------------------------------------------------------
    # VCP interactions
    # ------------------------------------------------------------------
    def get_multiple_vcp(
        self, descriptor: MonitorDescriptor, codes: Iterable[int], flags: int = 0
    ) -> Mapping[int, Tuple[int, int]]:
        merged_flags = self._prepare_flags(flags)
        response = self._call_sync(
            "GetMultipleVcp",
            GLib.Variant(
                "(ia(s)i)",
                (
                    descriptor.display_number,
                    [(descriptor.edid, code) for code in codes],
                    merged_flags,
                ),
            ),
        )
        payload, error_status, error_message = response.unpack()
        self._raise_if_error(error_status, error_message)
        return {int(code): (int(value[0]), int(value[1])) for code, value in payload.items()}

    def get_vcp(self, descriptor: MonitorDescriptor, code: int, flags: int = 0) -> Tuple[int, int]:
        merged_flags = self._prepare_flags(flags)
        response = self._call_sync(
            "GetVcp",
            GLib.Variant(
                "(iiis)",
                (descriptor.display_number, code, merged_flags, descriptor.edid),
            ),
        )
        (current_value, maximum_value), error_status, error_message = response.unpack()
        self._raise_if_error(error_status, error_message)
        return int(current_value), int(maximum_value)

    def set_vcp(self, descriptor: MonitorDescriptor, code: int, value: int, flags: int = 0) -> None:
        merged_flags = self._prepare_flags(flags)
        response = self._call_sync(
            "SetVcp",
            GLib.Variant(
                "(iiiis)",
                (
                    descriptor.display_number,
                    code,
                    value,
                    merged_flags,
                    descriptor.edid,
                ),
            ),
        )
        error_status, error_message = response.unpack()
        self._raise_if_error(error_status, error_message)

    def set_vcp_with_context(
        self,
        descriptor: MonitorDescriptor,
        code: int,
        value: int,
        client_context: str,
        flags: int = 0,
    ) -> None:
        merged_flags = self._prepare_flags(flags)
        response = self._call_sync(
            "SetVcpWithContext",
            GLib.Variant(
                "(iiiiss)",
                (
                    descriptor.display_number,
                    code,
                    value,
                    merged_flags,
                    client_context,
                    descriptor.edid,
                ),
            ),
        )
        error_status, error_message = response.unpack()
        self._raise_if_error(error_status, error_message)

    # ------------------------------------------------------------------
    # Service properties
    # ------------------------------------------------------------------
    def get_properties(self) -> Mapping[str, Any]:
        response = self._call_sync("org.freedesktop.DBus.Properties.GetAll", GLib.Variant("(s)", (INTERFACE_NAME,)))
        return response.unpack()[0]

    # ------------------------------------------------------------------
    # Utilities
    # ------------------------------------------------------------------
    def _raise_if_error(self, status: int, message: str) -> None:
        if status != 0:
            raise DdcutilError(status, message or "Unknown ddcutil error")


__all__ = [
    "DdcutilClient",
    "DdcutilError",
    "MonitorDescriptor",
    "VcpMetadata",
]
