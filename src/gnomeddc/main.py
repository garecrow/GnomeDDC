"""Entry point for the gnomeddc application."""

from __future__ import annotations

import argparse
import logging
import sys

from .app import GnomeDDCApplication


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="GnomeDDC - Libadwaita UI for ddcutil")
    parser.add_argument(
        "--verbose",
        "-v",
        action="count",
        default=0,
        help="Increase logging verbosity (can be specified multiple times)",
    )
    return parser.parse_args(argv)


def configure_logging(level: int) -> None:
    log_level = logging.WARNING
    if level == 1:
        log_level = logging.INFO
    elif level >= 2:
        log_level = logging.DEBUG
    logging.basicConfig(level=log_level)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    configure_logging(args.verbose)
    app = GnomeDDCApplication()
    return app.run(sys.argv)


if __name__ == "__main__":  # pragma: no cover - CLI entry point
    sys.exit(main())
