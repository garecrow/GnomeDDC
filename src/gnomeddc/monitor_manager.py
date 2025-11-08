from __future__ import annotations

from concurrent.futures import Future, ThreadPoolExecutor
from dataclasses import dataclass, field
from typing import Dict, Iterable, Optional

from gi.repository import Gio, GLib, GObject

from . import feature_catalog

BUS_NAMES: tuple[str, ...] = (
    'io.github.ddcutil.Service',
    'io.github.ddcutil1.Service',
    'io.github.ddcutil1',
    'io.github.ddcutil',
    'org.ddcutilservice.DdcutilService',
)

DEVICE_INTERFACE_HINTS: tuple[str, ...] = (
    'io.github.ddcutil.Device',
    'io.github.ddcutil.Device1',
    'io.github.ddcutil1.Device',
    'org.ddcutilservice.Device',
)

GET_METHOD_CANDIDATES: tuple[str, ...] = (
    'GetVcpFeature',
    'GetVcpValue',
    'GetFeature',
    'Get',
)

SET_METHOD_CANDIDATES: tuple[str, ...] = (
    'SetVcpFeature',
    'SetVcpValue',
    'SetFeature',
    'Set',
)

LIST_METHOD_CANDIDATES: tuple[str, ...] = (
    'ListVcpFeatures',
    'ListFeatures',
    'EnumerateFeatures',
)

_executor = ThreadPoolExecutor(max_workers=1, thread_name_prefix='gnomeddc')


@dataclass
class FeatureState:
    code: int
    name: str
    description: str
    category: str
    kind: feature_catalog.FeatureKind
    value: int
    maximum: Optional[int]
    writable: bool
    definition: feature_catalog.FeatureDefinition = field(repr=False)
    choices: Dict[int, str] = field(default_factory=dict)

    @property
    def min_value(self) -> int:
        if self.definition.minimum is not None:
            return self.definition.minimum
        return 0

    @property
    def max_value(self) -> int:
        if self.maximum is not None:
            return self.maximum
        if self.definition.maximum is not None:
            return self.definition.maximum
        return max(self.value, 100)


@dataclass
class MonitorInfo:
    object_path: str
    interface_name: str
    name: str
    model: str
    vendor: str
    serial: str
    capabilities: str
    features: Dict[int, FeatureState] = field(default_factory=dict)

    @property
    def display_name(self) -> str:
        base = self.name or self.model or self.vendor
        return base or 'Display'


class MonitorManager(GObject.GObject):
    __gsignals__ = {
        'refresh-started': (GObject.SignalFlags.RUN_FIRST, None, ()),
        'refresh-failed': (GObject.SignalFlags.RUN_FIRST, None, (str,)),
        'monitors-changed': (GObject.SignalFlags.RUN_FIRST, None, ()),
        'monitor-updated': (GObject.SignalFlags.RUN_FIRST, None, (str,)),
    }

    def __init__(self) -> None:
        super().__init__()
        self._monitors: list[MonitorInfo] = []
        self._service_name: str | None = None
        self._bus: Gio.DBusConnection | None = None
        self._refresh_future: Future[list[MonitorInfo]] | None = None

    # Public API -----------------------------------------------------

    def refresh(self) -> None:
        if self._refresh_future and not self._refresh_future.done():
            return
        self.emit('refresh-started')
        self._refresh_future = _executor.submit(self._load_monitors_thread)
        self._refresh_future.add_done_callback(self._on_refresh_finished)

    def monitors(self) -> list[MonitorInfo]:
        return list(self._monitors)

    def set_feature_value(self, monitor: MonitorInfo, feature: FeatureState, value: int) -> bool:
        if self._bus is None or self._service_name is None:
            return False
        try:
            self._call_set(self._bus, self._service_name, monitor.object_path, monitor.interface_name, feature.code, value)
        except GLib.Error as err:
            GLib.idle_add(self.emit, 'refresh-failed', err.message or str(err))
            return False
        feature.value = value
        GLib.idle_add(self.emit, 'monitor-updated', monitor.object_path)
        return True

    # Internal --------------------------------------------------------

    def _on_refresh_finished(self, future: Future[list[MonitorInfo]]) -> None:
        try:
            monitors = future.result()
        except Exception as err:  # noqa: BLE001
            GLib.idle_add(self.emit, 'refresh-failed', str(err))
            return

        def update() -> bool:
            self._monitors = monitors
            self.emit('monitors-changed')
            return False

        GLib.idle_add(update)

    def _load_monitors_thread(self) -> list[MonitorInfo]:
        bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
        service_name = self._discover_service(bus)
        self._bus = bus
        self._service_name = service_name
        if not service_name:
            return []

        try:
            objects_variant = bus.call_sync(
                service_name,
                '/',
                'org.freedesktop.DBus.ObjectManager',
                'GetManagedObjects',
                None,
                None,
                Gio.DBusCallFlags.NO_AUTO_START,
                5000,
                None,
            )
        except GLib.Error as err:
            raise RuntimeError(err.message or 'Failed to query ddcutil-service') from err

        objects = objects_variant.unpack()
        if isinstance(objects, tuple) and len(objects) == 1 and isinstance(objects[0], dict):
            objects = objects[0]

        monitors: list[MonitorInfo] = []
        for object_path, interfaces in objects.items():
            if not isinstance(interfaces, dict):
                continue
            interface_name, props = self._find_device_interface(interfaces)
            if not interface_name:
                continue
            monitor = MonitorInfo(
                object_path=object_path,
                interface_name=interface_name,
                name=self._get_str_prop(props, ('DisplayName', 'Name', 'ProductName')),
                model=self._get_str_prop(props, ('Model', 'ModelName', 'DisplayProductName')),
                vendor=self._get_str_prop(props, ('Vendor', 'Manufacturer', 'ManufacturerName')),
                serial=self._get_str_prop(props, ('Serial', 'SerialNumber')),
                capabilities=self._get_str_prop(props, ('Capabilities', 'CapabilityString')),
            )
            monitor.features = self._collect_features(bus, service_name, monitor)
            monitors.append(monitor)
        return monitors

    def _collect_features(self, bus: Gio.DBusConnection, service_name: str, monitor: MonitorInfo) -> Dict[int, FeatureState]:
        available_codes = self._discover_feature_codes(bus, service_name, monitor)
        feature_states: Dict[int, FeatureState] = {}
        for code, definition in feature_catalog.ALL_FEATURES.items():
            if available_codes and code not in available_codes:
                continue
            try:
                value, maximum = self._call_get(bus, service_name, monitor.object_path, monitor.interface_name, code)
            except GLib.Error:
                continue
            feature_states[code] = FeatureState(
                code=code,
                name=definition.name,
                description=definition.description,
                category=definition.category,
                kind=definition.kind,
                value=value,
                maximum=maximum,
                writable=definition.writable,
                definition=definition,
                choices=dict(definition.choices),
            )
        return feature_states

    def _discover_feature_codes(self, bus: Gio.DBusConnection, service_name: str, monitor: MonitorInfo) -> set[int]:
        codes: set[int] = set()
        for method in LIST_METHOD_CANDIDATES:
            try:
                result = bus.call_sync(
                    service_name,
                    monitor.object_path,
                    monitor.interface_name,
                    method,
                    None,
                    None,
                    Gio.DBusCallFlags.NO_AUTO_START,
                    3000,
                    None,
                )
            except GLib.Error:
                continue
            data = result.unpack()
            codes.update(self._normalize_feature_list(data))
            if codes:
                break
        return codes

    def _normalize_feature_list(self, data) -> Iterable[int]:
        if data is None:
            return []
        if isinstance(data, tuple) and len(data) == 1:
            data = data[0]
        if isinstance(data, dict):
            return [int(key) for key in data.keys()]
        if isinstance(data, list):
            return [int(item) for item in data]
        return []

    def _call_get(
        self,
        bus: Gio.DBusConnection,
        service_name: str,
        object_path: str,
        interface_name: str,
        code: int,
    ) -> tuple[int, Optional[int]]:
        for method in GET_METHOD_CANDIDATES:
            try:
                result = bus.call_sync(
                    service_name,
                    object_path,
                    interface_name,
                    method,
                    GLib.Variant('(q)', (code,)),
                    None,
                    Gio.DBusCallFlags.NO_AUTO_START,
                    3000,
                    None,
                )
            except GLib.Error:
                continue
            unpacked = result.unpack()
            current, maximum = self._extract_feature_values(unpacked)
            return current, maximum
        raise GLib.Error(message=f'Unable to read feature {code:02x}', domain=0, code=0)

    def _call_set(
        self,
        bus: Gio.DBusConnection,
        service_name: str,
        object_path: str,
        interface_name: str,
        code: int,
        value: int,
    ) -> None:
        for method in SET_METHOD_CANDIDATES:
            try:
                bus.call_sync(
                    service_name,
                    object_path,
                    interface_name,
                    method,
                    GLib.Variant('(q u)', (code, value)),
                    None,
                    Gio.DBusCallFlags.NO_AUTO_START,
                    3000,
                    None,
                )
                return
            except GLib.Error:
                continue
        raise GLib.Error(message=f'Unable to write feature {code:02x}', domain=0, code=0)

    def _extract_feature_values(self, unpacked) -> tuple[int, Optional[int]]:
        if isinstance(unpacked, tuple):
            if len(unpacked) == 1:
                return self._extract_feature_values(unpacked[0])
            if len(unpacked) >= 2:
                current = int(unpacked[0])
                maximum = int(unpacked[1]) if unpacked[1] is not None else None
                return current, maximum
        if isinstance(unpacked, dict):
            current = int(unpacked.get('value') or unpacked.get('CurrentValue') or unpacked.get('current', 0))
            maximum_raw = unpacked.get('maximum') or unpacked.get('MaxValue') or unpacked.get('max')
            maximum = int(maximum_raw) if maximum_raw is not None else None
            return current, maximum
        if isinstance(unpacked, int):
            return unpacked, None
        raise GLib.Error(message='Unexpected reply structure', domain=0, code=0)

    def _discover_service(self, bus: Gio.DBusConnection) -> str | None:
        for name in BUS_NAMES:
            try:
                bus.call_sync(
                    'org.freedesktop.DBus',
                    '/org/freedesktop/DBus',
                    'org.freedesktop.DBus',
                    'GetNameOwner',
                    GLib.Variant('(s)', (name,)),
                    None,
                    Gio.DBusCallFlags.NONE,
                    1000,
                    None,
                )
                return name
            except GLib.Error:
                continue
        return None

    def _find_device_interface(self, interfaces: dict) -> tuple[str | None, dict]:
        for name, props in interfaces.items():
            if not isinstance(name, str):
                continue
            if any(hint in name for hint in DEVICE_INTERFACE_HINTS):
                return name, props
        for name, props in interfaces.items():
            if isinstance(name, str) and 'Device' in name:
                return name, props
        return None, {}

    def _get_str_prop(self, props: dict, keys: Iterable[str]) -> str:
        for key in keys:
            if key in props:
                value = props[key]
                if isinstance(value, GLib.Variant):
                    try:
                        unpacked = value.unpack()
                    except TypeError:
                        unpacked = value
                else:
                    unpacked = value
                if unpacked:
                    return str(unpacked)
        return ''




