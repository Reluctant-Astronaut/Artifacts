#!/bin/bash
# The Patch Book Button: one press puts the newest Patch Book on this Mac and opens it.
#
#  1. waits if GitHub is still building a change, so the newest version is the one installed
#  2. downloads the finished app from the "patchbook-latest" release (built on GitHub's Macs - no Xcode needed here)
#  3. checks the app and lifts the download quarantine
#  4. quits Patch Book if it is open (it saves first), replaces it in Applications, and opens the new one
#
# Your Patch Book data (~/Library/Application Support/PatchBook) is never touched.
# Reports to the button app with lines starting @status, @done or @error. Log: ~/Library/Logs/Patch Book Button.log
# Written for the bash 3.2 that ships with macOS.
set -u
export PATH="/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin:$PATH"

REPO="${PATCHBOOK_REPO:-Reluctant-Astronaut/Artifacts}"
BRANCH="patch-book"
TAG="patchbook-latest"
LOG="$HOME/Library/Logs/Patch Book Button.log"
APPNAME="Patch Book.app"

mkdir -p "$(dirname "$LOG")"
log() { echo "$(date '+%Y-%m-%d %H:%M:%S')  $*" >> "$LOG"; }
status() { log "$1"; echo "@status $1"; }
fail() { log "FAILED: $1"; echo "@error $(echo "$1" | tr '\n' ' ' | tr -s ' ')"; exit 1; }
log "---- button pressed ($REPO)"

TMP="$(mktemp -d "${TMPDIR:-/tmp}/patchbook-button.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

# ---------------------------------------------------------------- 1. a build still running on GitHub?
waitForBuild() {
    local api="https://api.github.com/repos/$REPO/actions/runs?per_page=1&branch=$BRANCH" j="$TMP/run.json" st concl n=0
    while :; do
        curl -fsSL --connect-timeout 15 -H "Accept: application/vnd.github+json" -o "$j" "$api" 2>>"$LOG" || return 0
        st="$(plutil -extract workflow_runs.0.status raw -o - "$j" 2>/dev/null)"
        concl="$(plutil -extract workflow_runs.0.conclusion raw -o - "$j" 2>/dev/null)"
        [ -z "$st" ] && return 0
        if [ "$st" = "completed" ]; then
            [ "$concl" = "success" ] && return 0
            log "latest GitHub build: $concl"
            status "The newest GitHub build didn't succeed; installing the last good build."
            sleep 2
            return 0
        fi
        [ $n -ge 60 ] && fail "GitHub has been building for over 30 minutes. Press again later."
        status "GitHub is building the newest Patch Book… ($((n / 2)) min so far, usually 5–10)"
        sleep 30
        n=$((n + 1))
    done
}
status "Checking GitHub for the newest build…"
waitForBuild

# ---------------------------------------------------------------- 2. download
status "Downloading Patch Book…"
URL="https://github.com/$REPO/releases/download/$TAG/PatchBook.zip"
if ! curl -fsSL --retry 3 --connect-timeout 15 -o "$TMP/PatchBook.zip" "$URL" >> "$LOG" 2>&1; then
    fail "Couldn't download Patch Book from GitHub. Is the Mac online? If GitHub hasn't finished its first build yet, wait a few minutes and press again."
fi
curl -fsSL --retry 2 -o "$TMP/build-info.txt" "https://github.com/$REPO/releases/download/$TAG/build-info.txt" >> "$LOG" 2>&1 || true
[ -f "$TMP/build-info.txt" ] && log "build: $(tr '\n' ' ' < "$TMP/build-info.txt")"

# ---------------------------------------------------------------- 3. check
status "Checking the app…"
mkdir -p "$TMP/x"
ditto -x -k "$TMP/PatchBook.zip" "$TMP/x" >> "$LOG" 2>&1 || fail "The download was damaged. Press again to retry."
NEW="$TMP/x/$APPNAME"
[ -x "$NEW/Contents/MacOS/PatchBook" ] || fail "The download didn't contain Patch Book. Press again to retry."
xattr -dr com.apple.quarantine "$NEW" 2>/dev/null || true
codesign --verify --deep "$NEW" >> "$LOG" 2>&1 || log "warning: signature check failed (ad-hoc signed app)"

# ---------------------------------------------------------------- 4. install and open
if pgrep -x PatchBook >/dev/null 2>&1; then
    status "Closing Patch Book (it saves first)…"
    osascript -e 'tell application id "studio.onebrightpearl.patchbook" to quit' >/dev/null 2>&1 || true
    for i in 1 2 3 4 5 6 7 8 9 10; do pgrep -x PatchBook >/dev/null 2>&1 || break; sleep 0.5; done
    pkill -x PatchBook 2>/dev/null || true
fi

status "Installing in Applications…"
DEST="/Applications"
if ! { rm -rf "$DEST/$APPNAME" && ditto "$NEW" "$DEST/$APPNAME"; } >> "$LOG" 2>&1; then
    DEST="$HOME/Applications"
    mkdir -p "$DEST"
    { rm -rf "$DEST/$APPNAME" && ditto "$NEW" "$DEST/$APPNAME"; } >> "$LOG" 2>&1 || fail "Couldn't copy Patch Book into Applications."
fi
log "installed in $DEST"
echo "$DEST/$APPNAME" > "$HOME/Library/Logs/.patchbook-installed-path"

status "Opening Patch Book…"
open "$DEST/$APPNAME" >> "$LOG" 2>&1 || fail "Patch Book is installed but didn't open. Open it from $DEST."
echo "@done Patch Book is installed and open. Right-click its Dock icon → Options → Keep in Dock."
