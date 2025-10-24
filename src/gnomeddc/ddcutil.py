"""Helpers for interacting with the ``ddcutil`` command line tool."""

from __future__ import annotations

import json
import logging
import re
import shutil
import subprocess
from dataclasses import dataclass
from typing import Dict, Iterable, List, Optional

LOG = logging.getLogger(__name__)


class DDCError(RuntimeError):
    """Raised when executing ``ddcutil`` fails."""


@dataclass(slots=True)
class DisplayInfo:
    """Metadata about a monitor discovered via ``ddcutil``."""

    display_id: int
    bus_number: str
    model: str
    manufacturer: Optional[str]
    serial: Optional[str]
    capabilities: Optional[Dict[str, object]] = None

    @property
    def label(self) -> str:
        parts = [self.model or "Unknown Display"]
        if self.manufacturer:
            parts.append(self.manufacturer)
        return " · ".join(parts)


@dataclass(slots=True)
class FeatureValue:
    """Represents a single VCP feature."""

    code: str
    description: str
    current: int
    maximum: int
    raw: Dict[str, object]

    @property
    def fraction(self) -> float:
        if self.maximum == 0:
            return 0.0
        return self.current / self.maximum


@dataclass(slots=True)
class InputSource:
    """Represents an entry from the Input Source feature."""

    value: int
    label: str


_DEF_TIMEOUT = 5


def _ensure_binary() -> None:
    if shutil.which("ddcutil") is None:
        raise DDCError(
            "ddcutil executable not found. Install the 'ddcutil' package to control monitors."
        )


def _run_ddcutil(args: Iterable[str], timeout: int = _DEF_TIMEOUT) -> str:
    _ensure_binary()
    cmd = ["ddcutil", *args]
    LOG.debug("Running command: %s", " ".join(cmd))
    proc = subprocess.run(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=timeout,
        check=False,
    )
    if proc.returncode != 0:
        raise DDCError(proc.stderr.strip() or proc.stdout.strip() or "ddcutil failed")
    return proc.stdout


def list_displays() -> List[DisplayInfo]:
    """Return a list of displays detected by ``ddcutil``.

    Uses the JSON output if available and falls back to the terse format otherwise.
    """

    try:
        output = _run_ddcutil(["detect", "--json"])
        return _parse_detect_json(output)
    except (DDCError, json.JSONDecodeError) as exc:
        LOG.debug("Falling back to text detection: %s", exc)
        output = _run_ddcutil(["detect", "--brief", "--terse"])
        return _parse_detect_text(output)


def query_feature(bus: str, feature_code: str) -> FeatureValue:
    """Retrieve information about a VCP feature from the selected monitor."""

    code = feature_code.lower()
    try:
        output = _run_ddcutil(["--bus", bus, "getvcp", code, "--json"])
        return _parse_feature_json(output)
    except (DDCError, json.JSONDecodeError) as exc:
        LOG.debug("Falling back to text getvcp parsing for %s: %s", code, exc)
        output = _run_ddcutil(["--bus", bus, "getvcp", code])
        return _parse_feature_text(code, output)


def set_feature(bus: str, feature_code: str, value: int) -> None:
    """Set the current value for a VCP feature."""

    code = feature_code.lower()
    _run_ddcutil(["--bus", bus, "setvcp", code, str(int(value))])


def list_input_sources(bus: str) -> List[InputSource]:
    """Return the available values for the input source feature (0x60)."""

    try:
        output = _run_ddcutil(["--bus", bus, "getvcp", "0x60", "--json"])
        return _parse_input_sources_json(output)
    except (DDCError, json.JSONDecodeError):
        output = _run_ddcutil(["--bus", bus, "getvcp", "0x60"])
        return _parse_input_sources_text(output)


# -------------------- Parsing helpers --------------------


def _parse_detect_json(output: str) -> List[DisplayInfo]:
    data = json.loads(output)
    displays = []
    for entry in data.get("displays", []):
        info = entry.get("display", {})
        display_id = info.get("display_number") or len(displays) + 1
        bus = str(info.get("i2c_bus") or info.get("bus") or "0")
        displays.append(
            DisplayInfo(
                display_id=display_id,
                bus_number=bus,
                model=info.get("model") or info.get("product_name") or "Unknown",
                manufacturer=info.get("mfg") or info.get("manufacturer"),
                serial=info.get("serial_number") or info.get("sn"),
                capabilities=entry.get("capabilities"),
            )
        )
    return displays


def _parse_detect_text(output: str) -> List[DisplayInfo]:
    displays: List[DisplayInfo] = []
    current: Dict[str, str] = {}
    for line in output.splitlines():
        if line.startswith("Display "):
            if current:
                displays.append(_display_from_text(current, len(displays) + 1))
            current = {"display": line.split()[1].rstrip(":")}
        else:
            if ":" in line:
                key, value = [part.strip() for part in line.split(":", 1)]
                current[key.lower()] = value
    if current:
        displays.append(_display_from_text(current, len(displays) + 1))
    return displays


def _display_from_text(data: Dict[str, str], default_id: int) -> DisplayInfo:
    bus = data.get("i2c bus") or data.get("bus") or "0"
    model = data.get("model") or data.get("mn") or "Unknown"
    manufacturer = data.get("manufacturer") or data.get("mfg")
    serial = data.get("serial number") or data.get("sn")
    return DisplayInfo(default_id, bus, model, manufacturer, serial, capabilities=None)


def _parse_feature_json(output: str) -> FeatureValue:
    data = json.loads(output)
    vcp = data.get("vcp", {})
    values = vcp.get("values", {})
    return FeatureValue(
        code=vcp.get("code", ""),
        description=vcp.get("description", "Unknown feature"),
        current=int(values.get("current", 0)),
        maximum=int(values.get("maximum", 100) or 100),
        raw=vcp,
    )


def _parse_feature_text(code: str, output: str) -> FeatureValue:
    match = re.search(r"current value = (\d+), max value = (\d+)", output)
    if not match:
        raise DDCError(f"Unable to parse getvcp output for {code}: {output}")
    current, maximum = map(int, match.groups())
    desc_match = re.search(r"VCP code (?:0x)?[0-9a-fA-F]+ \(([^\)]+)\)", output)
    description = desc_match.group(1) if desc_match else "Feature"
    return FeatureValue(code=code, description=description, current=current, maximum=maximum, raw={})


def _parse_input_sources_json(output: str) -> List[InputSource]:
    data = json.loads(output)
    vcp = data.get("vcp", {})
    values = vcp.get("values", {})
    sources = []
    for entry in values.get("sl_values", []) or values.get("values", []):
        try:
            sources.append(InputSource(value=int(entry["value"]), label=str(entry["label"])) )
        except (KeyError, ValueError, TypeError):
            continue
    if not sources and isinstance(values, dict):
        current = values.get("current")
        if isinstance(current, dict) and "value" in current:
            sources.append(InputSource(value=int(current["value"]), label=str(current.get("label", current["value"]))))
    return sources


def _parse_input_sources_text(output: str) -> List[InputSource]:
    sources = []
    for line in output.splitlines():
        match = re.search(r"Input Source:.*sl=(\d+),.*name=(.+)", line)
        if match:
            value = int(match.group(1))
            label = match.group(2).strip()
            sources.append(InputSource(value=value, label=label))
    return sources

