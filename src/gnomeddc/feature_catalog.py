from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum
from typing import Dict


class FeatureKind(Enum):
    RANGE = 'range'
    CHOICE = 'choice'
    FLAG = 'flag'
    VALUE = 'value'


@dataclass(frozen=True)
class FeatureDefinition:
    code: int
    name: str
    description: str
    category: str
    kind: FeatureKind
    minimum: int | None = None
    maximum: int | None = None
    writable: bool = True
    choices: Dict[int, str] = field(default_factory=dict)


def _range_feature(code: int, name: str, description: str, category: str, minimum: int = 0, maximum: int | None = 100) -> FeatureDefinition:
    return FeatureDefinition(
        code=code,
        name=name,
        description=description,
        category=category,
        kind=FeatureKind.RANGE,
        minimum=minimum,
        maximum=maximum,
    )


def _choice_feature(code: int, name: str, description: str, category: str, choices: Dict[int, str]) -> FeatureDefinition:
    return FeatureDefinition(
        code=code,
        name=name,
        description=description,
        category=category,
        kind=FeatureKind.CHOICE,
        choices=choices,
    )


def _flag_feature(code: int, name: str, description: str, category: str) -> FeatureDefinition:
    return FeatureDefinition(
        code=code,
        name=name,
        description=description,
        category=category,
        kind=FeatureKind.FLAG,
    )


ALL_FEATURES: Dict[int, FeatureDefinition] = {
    0x10: _range_feature(0x10, 'Brightness', 'Overall backlight intensity.', 'Luminance'),
    0x12: _range_feature(0x12, 'Contrast', 'Difference between light and dark.', 'Luminance'),
    0x16: _range_feature(0x16, 'Red gain', 'Red channel gain for color temperature fine tuning.', 'Color', maximum=255),
    0x18: _range_feature(0x18, 'Green gain', 'Green channel gain for color temperature fine tuning.', 'Color', maximum=255),
    0x1a: _range_feature(0x1A, 'Blue gain', 'Blue channel gain for color temperature fine tuning.', 'Color', maximum=255),
    0x1c: _flag_feature(0x1C, 'Auto color setup', 'Trigger automatic color calibration (if supported).', 'Color'),
    0x14: _range_feature(0x14, 'Black level', 'Adjusts black level or backlight bias.', 'Picture'),
    0x60: _choice_feature(
        0x60,
        'Input source',
        'Choose the active video input.',
        'Picture',
        {
            0x01: 'VGA 1',
            0x03: 'DVI 1',
            0x04: 'DVI 2',
            0x0f: 'DisplayPort 1',
            0x10: 'DisplayPort 2',
            0x11: 'HDMI 1',
            0x12: 'HDMI 2',
            0x1b: 'USB-C',
        },
    ),
    0x62: _range_feature(0x62, 'Audio speaker volume', 'Adjust speaker or headphone output volume when available.', 'Audio', maximum=100),
    0x68: _choice_feature(
        0x68,
        'Power mode',
        'Manage low power, standby, and on states.',
        'Power',
        {
            0x01: 'On',
            0x02: 'Standby',
            0x03: 'Suspend',
            0x04: 'Off',
        },
    ),
    0xdc: _choice_feature(
        0xDC,
        'Preset mode',
        'Monitor built-in picture preset selection.',
        'Picture',
        {
            0x01: 'Standard',
            0x02: 'Movie',
            0x03: 'Game',
            0x04: 'Text',
            0x05: 'sRGB',
            0x06: 'Adobe RGB',
            0x0b: 'Custom',
        },
    ),
    0xe0: _flag_feature(0xE0, 'Power LED', 'Toggle the monitor status LED when supported.', 'Power'),
    0xe1: _range_feature(0xE1, 'Uniformity compensation', 'Backlight uniformity level.', 'Picture'),
}


def features_by_category(category: str) -> dict[int, FeatureDefinition]:
    return {code: feature for code, feature in ALL_FEATURES.items() if feature.category == category}


