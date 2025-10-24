#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
DIST_DIR="$ROOT_DIR/dist"
SPEC_FILE="$ROOT_DIR/packaging/gnomeddc.spec"

if [[ ! -f $SPEC_FILE ]]; then
    echo "Spec file not found: $SPEC_FILE" >&2
    exit 1
fi

mkdir -p "$DIST_DIR"

VERSION=$(python -c 'import pathlib, tomllib; data = tomllib.loads(pathlib.Path("pyproject.toml").read_text()); print(data["project"]["version"])' 2>/dev/null || \
    python -c 'import pathlib, toml; data = toml.loads(pathlib.Path("pyproject.toml").read_text()); print(data["project"]["version"])')

SDIST="gnomeddc-${VERSION}.tar.gz"

python -m build --sdist --outdir "$DIST_DIR"

rpmbuild -bs "$SPEC_FILE" \
    --define "_sourcedir $DIST_DIR" \
    --define "_srcrpmdir $DIST_DIR"

echo "Source tarball: $DIST_DIR/$SDIST"
echo "SRPM: $(ls "$DIST_DIR"/gnomeddc-${VERSION}-*.src.rpm)"
