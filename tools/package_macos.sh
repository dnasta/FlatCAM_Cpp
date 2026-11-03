#!/usr/bin/env bash
# Builds a self-contained macOS .app with macdeployqt and ad-hoc signing.
set -euo pipefail
cd "$(dirname "$0")/.."

QT_PATH="${QT_PATH:-$HOME/Qt/6.12.0/macos}"
BUILD="${BUILD:-build-gui}"
DEST="${DEST:-dist}"
MACDEPLOYQT="$QT_PATH/bin/macdeployqt"

if [ ! -x "$MACDEPLOYQT" ]; then
    echo "macdeployqt not found at $MACDEPLOYQT (set QT_PATH)"
    exit 1
fi

cmake -S . -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$QT_PATH" >/dev/null
cmake --build "$BUILD" -j"$(sysctl -n hw.ncpu)"
rm -rf "$DEST"
cmake --install "$BUILD" --prefix "$DEST" >/dev/null

"$MACDEPLOYQT" "$DEST/flatcam-gui.app" -always-overwrite

# macdeployqt only copies the cocoa platform plugin. Add the headless ones so
# the bundled app can also run with QT_QPA_PLATFORM=offscreen (tests, scripts).
PLUGINS="$DEST/flatcam-gui.app/Contents/PlugIns/platforms"
mkdir -p "$PLUGINS"
for plugin in libqoffscreen.dylib libqminimal.dylib libqminimalegl.dylib; do
    if [ -f "$QT_PATH/plugins/platforms/$plugin" ]; then
        cp "$QT_PATH/plugins/platforms/$plugin" "$PLUGINS/"
    fi
done
"$MACDEPLOYQT" "$DEST/flatcam-gui.app" -always-overwrite

# Keep only the SQLite driver: the PostgreSQL/ODBC/Mimer plugins may link
# against libraries that are not present on the user machine.
rm -f "$DEST/flatcam-gui.app/Contents/PlugIns/sqldrivers/libqsqlpsql.dylib" \
      "$DEST/flatcam-gui.app/Contents/PlugIns/sqldrivers/libqsqlodbc.dylib" \
      "$DEST/flatcam-gui.app/Contents/PlugIns/sqldrivers/libqsqlmimer.dylib"

# A bundle without Resources cannot be sealed by codesign.
mkdir -p "$DEST/flatcam-gui.app/Contents/Resources"
cp assets/appicon.icns "$DEST/flatcam-gui.app/Contents/Resources/appicon.icns"
cp LICENSE "$DEST/flatcam-gui.app/Contents/Resources/LICENSE"
cp THIRD_PARTY_NOTICES.md "$DEST/flatcam-gui.app/Contents/Resources/THIRD_PARTY_NOTICES.md"
/usr/libexec/PlistBuddy -c "Set :CFBundleIconFile appicon" \
    "$DEST/flatcam-gui.app/Contents/Info.plist" 2>/dev/null || \
    /usr/libexec/PlistBuddy -c "Add :CFBundleIconFile string appicon" \
    "$DEST/flatcam-gui.app/Contents/Info.plist"
codesign --force --deep --sign - --timestamp=none "$DEST/flatcam-gui.app" || \
    echo "ad-hoc codesign failed"
codesign --verify --deep --strict "$DEST/flatcam-gui.app" && echo "signature OK"

date "+Build: %Y-%m-%d %H:%M:%S" > "$DEST/BUILD.txt"
echo "App: $DEST/flatcam-gui.app"
echo "CLI: $DEST/bin/flatcam-cli"
echo "Info: $DEST/BUILD.txt"
