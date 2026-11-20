#!/bin/bash
# Builds "Patch Book.app" into ./build — on GitHub's Mac runners (the normal way) or on a Mac with Xcode.
# Usage:  bash build.sh            build only
#         bash build.sh --install  build, copy to /Applications and open it
set -euo pipefail
cd "$(dirname "$0")"

# SwiftUI needs Xcode's toolchain: the Command Line Tools lack SwiftUI's macro plugin (@State etc.).
if [ -z "${DEVELOPER_DIR:-}" ] && xcode-select -p 2>/dev/null | grep -q CommandLineTools; then
  XC="$(ls -d /Applications/Xcode*.app 2>/dev/null | head -1)"
  if [ -n "$XC" ]; then
    export DEVELOPER_DIR="$XC/Contents/Developer"
  else
    echo "Xcode isn't installed. Patch Book is normally built on GitHub; to build here, install Xcode from the App Store."
    exit 1
  fi
fi
echo "▸ Toolchain: $(xcode-select -p 2>/dev/null) ${DEVELOPER_DIR:+(DEVELOPER_DIR=$DEVELOPER_DIR)}"
swift --version

echo "▸ Compiling (Apple Silicon + Intel)…"
ARCHS="--arch arm64 --arch x86_64"
swift build -c release $ARCHS
BIN="$(swift build -c release $ARCHS --show-bin-path)/PatchBook"

APP="build/Patch Book.app"
echo "▸ Assembling $APP"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$BIN" "$APP/Contents/MacOS/PatchBook"
cp Support/Info.plist "$APP/Contents/Info.plist"

echo "▸ Making the icon"
ICONSET="build/AppIcon.iconset"
rm -rf "$ICONSET"
mkdir -p "$ICONSET"
for s in 16 32 128 256 512; do
  sips -z $s $s Support/AppIcon.png --out "$ICONSET/icon_${s}x${s}.png" >/dev/null
  d=$((s * 2))
  sips -z $d $d Support/AppIcon.png --out "$ICONSET/icon_${s}x${s}@2x.png" >/dev/null
done
iconutil -c icns "$ICONSET" -o "$APP/Contents/Resources/AppIcon.icns"
rm -rf "$ICONSET"

echo "▸ Signing (ad-hoc)"
codesign --force --deep --sign - "$APP"
codesign --verify --deep --verbose "$APP"

echo "✓ Built: $APP"

if [ "${1:-}" = "--install" ]; then
  rm -rf "/Applications/Patch Book.app"
  cp -R "$APP" /Applications/
  echo "✓ Installed in /Applications"
  open "/Applications/Patch Book.app"
fi
