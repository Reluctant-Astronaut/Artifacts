#!/bin/bash
# Puts "Patch Book Button.app" together (no compiler needed): a tiny app bundle around press.sh, with the red button icon (same as the Artifacts Button).
# usage: build-app.sh <output folder>
set -e
here="$(cd "$(dirname "$0")" && pwd)"
out="${1:-.}"; app="$out/Patch Book Button.app"
rm -rf "$app"; mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources"
cp "$here/press.sh" "$app/Contents/Resources/press.sh"
cp "$here/AppIcon.icns" "$app/Contents/Resources/AppIcon.icns"
if command -v swiftc >/dev/null; then
    # on a Mac (the GitHub build): the real button window, for Apple Silicon and Intel
    swiftc -swift-version 5 -O -target arm64-apple-macos11 "$here/PatchBookButton.swift" -framework AppKit -o "$out/ab-arm64"
    swiftc -swift-version 5 -O -target x86_64-apple-macos11 "$here/PatchBookButton.swift" -framework AppKit -o "$out/ab-x86_64"
    lipo -create "$out/ab-arm64" "$out/ab-x86_64" -output "$app/Contents/MacOS/Patch Book Button"
    rm -f "$out/ab-arm64" "$out/ab-x86_64"
else
    # anywhere else: a plain script app (no window; dialogs only)
    cat > "$app/Contents/MacOS/Patch Book Button" <<'SH'
#!/bin/bash
exec /bin/bash "$(dirname "$0")/../Resources/press.sh"
SH
fi
chmod 755 "$app/Contents/MacOS/Patch Book Button" "$app/Contents/Resources/press.sh"
cat > "$app/Contents/Info.plist" <<'PL'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key><string>Patch Book Button</string>
    <key>CFBundleDisplayName</key><string>Patch Book Button</string>
    <key>CFBundleIdentifier</key><string>studio.onebrightpearl.patchbook.button</string>
    <key>CFBundleExecutable</key><string>Patch Book Button</string>
    <key>CFBundleIconFile</key><string>AppIcon</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleShortVersionString</key><string>1.0</string>
    <key>CFBundleVersion</key><string>1</string>
    <key>LSMinimumSystemVersion</key><string>11.0</string>
    <key>PatchBookRepo</key><string>REPO_PLACEHOLDER</string>
    <key>NSHighResolutionCapable</key><true/>
    <key>NSAppleEventsUsageDescription</key><string>Patch Book Button asks Patch Book to quit (it saves first) before installing a new version.</string>
</dict>
</plist>
PL
# the GitHub repository it installs from (the workflow passes its own)
sed -i.bak "s#REPO_PLACEHOLDER#${PATCHBOOK_REPO:-}#" "$app/Contents/Info.plist" && rm -f "$app/Contents/Info.plist.bak"
# ad-hoc signature when built on a Mac
command -v codesign >/dev/null && codesign --force --deep --sign - "$app" || true
echo "$app"
