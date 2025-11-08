"""Monitor and VCP feature state management for the GTK UI."""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Dict, Iterable, List, Mapping, Optional

from gi.repository import GLib, GObject

from .client import DdcutilClient, MonitorDescriptor, VcpMetadata


@dataclass
class VcpValue:
    code: int
    current: int
    maximum: int
    metadata: Optional[VcpMetadata] = None

    def as_percentage(self) -> float:
        if self.maximum:
            return self.current / self.maximum
        return 0.0


@dataclass
class MonitorState:
    descriptor: MonitorDescriptor
    capabilities: Mapping[int, VcpMetadata] = field(default_factory=dict)
    values: Dict[int, VcpValue] = field(default_factory=dict)


class MonitorStore(GObject.GObject):
    """Keeps monitor and VCP state in sync with the service."""

    __gsignals__ = {
        "monitors-changed": (GObject.SignalFlags.RUN_LAST, None, ()),
        "vcp-updated": (GObject.SignalFlags.RUN_LAST, None, (int, int)),
        "error": (GObject.SignalFlags.RUN_LAST, None, (str,)),
    }

    def __init__(self, client: DdcutilClient) -> None:
        super().__init__()
        self._client = client
        self._monitors: Dict[str, MonitorState] = {}
        self._client.connect("service-initialized", self._on_service_initialized)
        self._client.connect("connected-displays-changed", self._on_displays_changed)
        self._client.connect("vcp-value-changed", self._on_vcp_signal)

    # ------------------------------------------------------------------
    # Accessors
    # ------------------------------------------------------------------
    def monitors(self) -> List[MonitorState]:
        return list(self._monitors.values())

    def get(self, edid: str) -> Optional[MonitorState]:
        return self._monitors.get(edid)

    # ------------------------------------------------------------------
    # Service interaction
    # ------------------------------------------------------------------
    def refresh(self) -> None:
        try:
            monitors = self._client.detect(flags=0)
        except Exception as error:  # pragma: no cover - DBus runtime
            self.emit("error", str(error))
            return

        updated: Dict[str, MonitorState] = {}
        for monitor in monitors:
            state = self._monitors.get(monitor.edid)
            if state is None:
                state = MonitorState(descriptor=monitor)
                self._load_capabilities(state)
            else:
                state.descriptor = monitor
            updated[monitor.edid] = state

        removed = set(self._monitors) - set(updated)
        self._monitors = updated
        if removed:
            # Drop stale entries without emitting per-monitor updates; the UI
            # will rebuild itself when "monitors-changed" fires.
            pass
        self.emit("monitors-changed")

    def refresh_fast(self) -> None:
        try:
            monitors = self._client.list_detected()
        except Exception:  # pragma: no cover - DBus runtime
            return
        for monitor in monitors:
            state = self._monitors.get(monitor.edid)
            if state:
                state.descriptor = monitor
        self.emit("monitors-changed")

    def _load_capabilities(self, state: MonitorState) -> None:
        try:
            caps = self._client.get_capabilities_metadata(state.descriptor)
        except Exception as error:  # pragma: no cover - DBus runtime
            self.emit("error", str(error))
            caps = {}
        state.capabilities = caps
        if caps:
            self.refresh_vcp_values(state, caps.keys())

    def refresh_vcp_values(self, state: MonitorState, codes: Iterable[int]) -> None:
        try:
            values = self._client.get_multiple_vcp(state.descriptor, list(codes))
        except Exception as error:  # pragma: no cover - DBus runtime
            self.emit("error", str(error))
            return
        for code, (current, maximum) in values.items():
            metadata = state.capabilities.get(code)
            state.values[code] = VcpValue(code=code, current=current, maximum=maximum, metadata=metadata)
        for code in values:
            self.emit("vcp-updated", state.descriptor.display_number, code)

    # ------------------------------------------------------------------
    # Signal handling
    # ------------------------------------------------------------------
    def _on_service_initialized(self, *_args: object) -> None:
        self.refresh()

    def _on_displays_changed(self, _client: DdcutilClient, _edid: str, event_type: int, _flags: int) -> None:
        if event_type == 0:  # general refresh
            self.refresh_fast()
        else:
            self.refresh()

    def _on_vcp_signal(
        self,
        _client: DdcutilClient,
        display_number: int,
        edid: str,
        code: int,
        value: int,
        _source_name: str,
        _source_context: str,
        _flags: int,
    ) -> None:
        state = self._monitors.get(edid)
        if not state:
            return
        metadata = state.capabilities.get(code)
        current = state.values.get(code)
        maximum = current.maximum if current else metadata.maximum if metadata and metadata.maximum else value
        state.values[code] = VcpValue(code=code, current=value, maximum=maximum, metadata=metadata)
        self.emit("vcp-updated", display_number, code)

    # ------------------------------------------------------------------
    # Mutations
    # ------------------------------------------------------------------
    def set_vcp_value(
        self, state: MonitorState, code: int, value: int, *, context: str = "GnomeDDC"
    ) -> None:
        try:
            self._client.set_vcp_with_context(state.descriptor, code, value, client_context=context)
        except Exception as error:  # pragma: no cover - DBus runtime
            self.emit("error", str(error))
            return
        current = state.values.get(code)
        metadata = state.capabilities.get(code)
        maximum = current.maximum if current else metadata.maximum if metadata and metadata.maximum else value
        state.values[code] = VcpValue(code=code, current=value, maximum=maximum, metadata=metadata)
        self.emit("vcp-updated", state.descriptor.display_number, code)


__all__ = ["MonitorStore", "MonitorState", "VcpValue"]
