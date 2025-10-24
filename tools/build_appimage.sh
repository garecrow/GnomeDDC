#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
APPDIR="$ROOT_DIR/build/AppDir"
DIST_DIR="$ROOT_DIR/dist"
APPIMAGE_NAME="GnomeDDC-x86_64.AppImage"
APPIMAGE_PATH="$DIST_DIR/$APPIMAGE_NAME"
APPIMAGETOOL_DIR="$ROOT_DIR/build/appimagetool"
APPIMAGETOOL_APPIMAGE="$APPIMAGETOOL_DIR/appimagetool-x86_64.AppImage"
APPIMAGETOOL_BIN="$APPIMAGETOOL_DIR/squashfs-root/AppRun"

rm -rf "$APPDIR"
mkdir -p "$APPDIR" "$DIST_DIR"

mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/lib/gnomeddc" "$APPDIR/usr/share/applications" \
         "$APPDIR/usr/share/icons/hicolor/scalable/apps"

rsync -a --exclude __pycache__ "$ROOT_DIR/src/gnomeddc/" "$APPDIR/usr/lib/gnomeddc/"

install -Dm644 "$ROOT_DIR/data/io.github.gnomeddc.GnomeDDC.desktop" \
    "$APPDIR/io.github.gnomeddc.GnomeDDC.desktop"
install -Dm644 "$ROOT_DIR/data/io.github.gnomeddc.GnomeDDC.desktop" \
    "$APPDIR/usr/share/applications/io.github.gnomeddc.GnomeDDC.desktop"
install -Dm644 "$ROOT_DIR/data/icons/hicolor/scalable/apps/io.github.gnomeddc.GnomeDDC.svg" \
    "$APPDIR/io.github.gnomeddc.GnomeDDC.svg"
install -Dm644 "$ROOT_DIR/data/icons/hicolor/scalable/apps/io.github.gnomeddc.GnomeDDC.svg" \
    "$APPDIR/usr/share/icons/hicolor/scalable/apps/io.github.gnomeddc.GnomeDDC.svg"

cat <<'EOF' > "$APPDIR/usr/bin/gnomeddc"
#!/bin/sh
HERE="$(dirname "$(readlink -f "$0")")"
export PYTHONPATH="$HERE/../lib/gnomeddc${PYTHONPATH:+:$PYTHONPATH}"
exec /usr/bin/env python3 -m gnomeddc.main "$@"
EOF
chmod +x "$APPDIR/usr/bin/gnomeddc"

cat <<'EOF' > "$APPDIR/AppRun"
#!/bin/sh
HERE="$(dirname "$(readlink -f "$0")")"
export PYTHONPATH="$HERE/usr/lib/gnomeddc${PYTHONPATH:+:$PYTHONPATH}"
exec /usr/bin/env python3 -m gnomeddc.main "$@"
EOF
chmod +x "$APPDIR/AppRun"
ln -sf io.github.gnomeddc.GnomeDDC.svg "$APPDIR/.DirIcon"

if [ ! -f "$APPIMAGETOOL_APPIMAGE" ]; then
    mkdir -p "$APPIMAGETOOL_DIR"
    curl -L "https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage" \
        -o "$APPIMAGETOOL_APPIMAGE"
    chmod +x "$APPIMAGETOOL_APPIMAGE"
fi

if [ ! -x "$APPIMAGETOOL_BIN" ]; then
    (cd "$APPIMAGETOOL_DIR" && "./$(basename "$APPIMAGETOOL_APPIMAGE")" --appimage-extract > /dev/null)
fi

ARCH=x86_64 "$APPIMAGETOOL_BIN" "$APPDIR" "$APPIMAGE_PATH"

printf '\nAppImage created at %s\n' "$APPIMAGE_PATH"
