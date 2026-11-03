#!/usr/bin/env bash
# Builds the .app and packages it as a DMG with the user-facing app name.
set -euo pipefail
cd "$(dirname "$0")/.."

VERSION="${1:-0.2.0}"
ARCH="$(uname -m)"
DEST="dist"
APP="FlatCAM C++"
OUT="$DEST/FlatCAM-CPP-${VERSION}-${ARCH}.dmg"
STAGE="$(mktemp -d "${TMPDIR:-/tmp}/flatcam_dmg.XXXXXX")"
trap 'rm -rf "$STAGE"' EXIT

./tools/package_macos.sh

cp -R "$DEST/flatcam-gui.app" "$STAGE/$APP.app"
ln -sfn /Applications "$STAGE/Applications"
hdiutil create -volname "$APP" -srcfolder "$STAGE" -ov -format UDZO "$OUT" >/dev/null

echo "DMG: $OUT"
shasum -a 256 "$OUT"
