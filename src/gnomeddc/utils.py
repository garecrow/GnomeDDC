from __future__ import annotations

from typing import Mapping

from . import feature_catalog


def format_feature_value(definition: feature_catalog.FeatureDefinition, value: int, choices: Mapping[int, str] | None = None) -> str:
    choice_map = dict(definition.choices)
    if choices:
        choice_map.update(choices)
    if definition.kind is feature_catalog.FeatureKind.CHOICE and choice_map:
        return choice_map.get(value, f'{value}')
    if definition.kind is feature_catalog.FeatureKind.FLAG:
        return 'On' if value else 'Off'
    return f'{value}'

