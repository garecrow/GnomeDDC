"""Utility helpers for running blocking tasks off the GTK main loop."""

from __future__ import annotations

import threading
from typing import Callable, Generic, Optional, TypeVar

from gi.repository import GLib

T = TypeVar("T")


class AsyncRunner(Generic[T]):
    """Run blocking callables in a background thread and return results via callbacks."""

    def run(self, func: Callable[[], T], callback: Callable[[Optional[T], Optional[BaseException]], None]) -> None:
        def target() -> None:
            try:
                result = func()
            except BaseException as exc:  # pragma: no cover - defensive
                GLib.idle_add(callback, None, exc)
            else:
                GLib.idle_add(callback, result, None)

        thread = threading.Thread(target=target, daemon=True)
        thread.start()


__all__ = ["AsyncRunner"]

