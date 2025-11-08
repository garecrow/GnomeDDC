from __future__ import annotations

import logging
from importlib import resources as importlib_resources

from gi.repository import Gio

__all__ = [
    'application',
    'main',
]

_logger = logging.getLogger(__name__)


def _register_resources() -> None:
    try:
        resource_path = importlib_resources.files(__name__).joinpath('io.github.gnomeddc.gresource')
    except (FileNotFoundError, AttributeError):
        _logger.debug('Resource bundle missing from package; UI will load from disk if available.')
        return

    try:
        resource = Gio.Resource.load(str(resource_path))
    except (FileNotFoundError, Gio.Error) as err:  # type: ignore[attr-defined]
        _logger.warning('Failed to load GNOME resources: %s', err)
    else:
        Gio.resources_register(resource)


_register_resources()
