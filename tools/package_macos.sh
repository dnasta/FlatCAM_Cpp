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

APP="$DEST/flatcam-gui.app"
# Bundle GEOS (Homebrew instalado con deployment target alto no es portable).
GEOS_DIR_CACHE="$(sed -n 's/^GEOS_DIR:[^=]*=//p' "$BUILD/CMakeCache.txt" | head -1)"
if [ -n "$GEOS_DIR_CACHE" ]; then
    GEOS_LIB_DIR="$(cd "$(dirname "$GEOS_DIR_CACHE")/.." && pwd)"
else
    GEOS_LIB_DIR="$(brew --prefix geos 2>/dev/null)/lib"
fi
if [ -d "$GEOS_LIB_DIR" ]; then
    mkdir -p "$APP/Contents/Frameworks"
    cp -L "$GEOS_LIB_DIR"/libgeos*.dylib "$APP/Contents/Frameworks/" 2>/dev/null || true
fi

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
mkdir -p "$DEST/flatcam-gui.app/Contents/Resources/licenses"
cp licenses/* "$DEST/flatcam-gui.app/Contents/Resources/licenses/"
/usr/libexec/PlistBuddy -c "Set :CFBundleIconFile appicon" \
    "$DEST/flatcam-gui.app/Contents/Info.plist" 2>/dev/null || \
    /usr/libexec/PlistBuddy -c "Add :CFBundleIconFile string appicon" \
    "$DEST/flatcam-gui.app/Contents/Info.plist"
# CLI y JS dentro del bundle, con rpath a los Frameworks.
for tool in flatcam-cli flatcam-js; do
    if [ -f "$DEST/bin/$tool" ]; then
        cp "$DEST/bin/$tool" "$APP/Contents/MacOS/$tool"
        if ! otool -l "$APP/Contents/MacOS/$tool" | grep -q "@executable_path/../Frameworks"; then
            install_name_tool -add_rpath "@executable_path/../Frameworks" "$APP/Contents/MacOS/$tool"
        fi
        printf '#!/bin/sh\nexec "$(dirname "$0")/../flatcam-gui.app/Contents/MacOS/%s" "$@"\n' "$tool" > "$DEST/bin/$tool"
        chmod +x "$DEST/bin/$tool"
    fi
done

codesign --force --deep --sign - --timestamp=none "$DEST/flatcam-gui.app" || \
    echo "ad-hoc codesign failed"
codesign --verify --deep --strict "$DEST/flatcam-gui.app" && echo "signature OK"

date "+Build: %Y-%m-%d %H:%M:%S" > "$DEST/BUILD.txt"
echo "App: $DEST/flatcam-gui.app"
echo "CLI: $DEST/bin/flatcam-cli"
echo "Info: $DEST/BUILD.txt"
