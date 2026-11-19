#!/bin/bash
# Puts "Artifacts Button.app" together (no compiler needed): a tiny app bundle around press.sh, with the red button icon.
# usage: build-app.sh <output folder>
set -e
here="$(cd "$(dirname "$0")" && pwd)"
out="${1:-.}"; app="$out/Artifacts Button.app"
rm -rf "$app"; mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources"
cp "$here/press.sh" "$app/Contents/Resources/press.sh"
cp "$here/AppIcon.icns" "$app/Contents/Resources/AppIcon.icns"
if command -v swiftc >/dev/null; then
    # on a Mac (the GitHub build): the real button window, for Apple Silicon and Intel
    swiftc -swift-version 5 -O -target arm64-apple-macos11 "$here/ArtifactsButton.swift" -framework AppKit -o "$out/ab-arm64"
    swiftc -swift-version 5 -O -target x86_64-apple-macos11 "$here/ArtifactsButton.swift" -framework AppKit -o "$out/ab-x86_64"
    lipo -create "$out/ab-arm64" "$out/ab-x86_64" -output "$app/Contents/MacOS/Artifacts Button"
    rm -f "$out/ab-arm64" "$out/ab-x86_64"
else
    # anywhere else: a plain script app (no window; dialogs only)
    cat > "$app/Contents/MacOS/Artifacts Button" <<'SH'
#!/bin/bash
exec /bin/bash "$(dirname "$0")/../Resources/press.sh"
SH
fi
chmod 755 "$app/Contents/MacOS/Artifacts Button" "$app/Contents/Resources/press.sh"
cat > "$app/Contents/Info.plist" <<'PL'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key><string>Artifacts Button</string>
    <key>CFBundleDisplayName</key><string>Artifacts Button</string>
    <key>CFBundleIdentifier</key><string>com.reluctantastronaut.artifacts.button</string>
    <key>CFBundleExecutable</key><string>Artifacts Button</string>
    <key>CFBundleIconFile</key><string>AppIcon</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleShortVersionString</key><string>1.0</string>
    <key>CFBundleVersion</key><string>1</string>
    <key>LSMinimumSystemVersion</key><string>11.0</string>
    <key>ArtifactsRepo</key><string>REPO_PLACEHOLDER</string>
    <key>NSHighResolutionCapable</key><true/>
    <key>NSAppleEventsUsageDescription</key><string>Artifacts Button asks Cubase to quit (so it can see the new plug-ins) and shows its messages.</string>
</dict>
</plist>
PL
# the GitHub repository it installs from (the workflow passes its own)
sed -i.bak "s#REPO_PLACEHOLDER#${ARTIFACTS_REPO:-}#" "$app/Contents/Info.plist" && rm -f "$app/Contents/Info.plist.bak"
# ad-hoc signature when built on a Mac
command -v codesign >/dev/null && codesign --force --deep --sign - "$app" || true
echo "$app"
