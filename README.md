# Patch Book — One Bright Pearl Studio

A native macOS app (SwiftUI, macOS 14+) for keeping track of the studio: inventory, patch tabs for the SSL 18, ISA One,
MOTU 828 MkII and VoiceMaster Pro, a signal-flow canvas, connections, setups and history.

This branch (`patch-book`) is separate from the Artifacts plug-ins on `main`; its workflow only runs for this branch.

## How it gets onto the Mac
Push to `patch-book` → `.github/workflows/patchbook.yml` builds the app on GitHub's Macs (Apple Silicon + Intel),
signs it ad hoc and publishes it as the `patchbook-latest` release (with `PatchBook.zip` and `Patch-Book-Button.zip`).
The build log is always published as the `patchbook-log` release.

**The Patch Book Button** (`tools/PatchBookButton`, made from the Artifacts Button): one press waits for a running
GitHub build, downloads the newest Patch Book, replaces it in Applications and opens it. Your data in
`~/Library/Application Support/PatchBook` is never touched. Log: `~/Library/Logs/Patch Book Button.log`.

## Layout
- `Sources/PatchBook` — the app
- `Support` — Info.plist and icon
- `build.sh` — builds `build/Patch Book.app` (on GitHub, or on a Mac with Xcode)
- `build_seed.py` — generates the starting data (`Sources/PatchBook/Seed.swift`)
