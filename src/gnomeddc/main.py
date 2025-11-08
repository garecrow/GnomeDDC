from __future__ import annotations

import sys

from .application import GnomeDdcApplication


def main(argv: list[str] | None = None) -> int:
    app = GnomeDdcApplication()
    return app.run(argv or sys.argv)


if __name__ == '__main__':
    raise SystemExit(main())
