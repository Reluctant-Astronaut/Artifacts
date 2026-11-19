#!/bin/bash
# The Artifacts Button: one press puts the whole Artifacts suite on this Mac, ready for Cubase.
#
#  1. fetches the latest build of the suite from the GitHub repository (the "latest-build" release the workflow publishes)
#  2. checks every plug-in is there and signed, and lifts the download quarantine
#  3. asks Cubase to quit if it is open (Cubase only looks for plug-ins when it starts; it asks to save first, as always)
#  4. installs or updates the VST3s (Cubase) and Audio Units (Logic) - in your own plug-in folders, or where the
#     Mac installer put them before (then it asks for your password once), so there is never a second copy
#  5. lets Cubase try again if it had blocklisted an Artifacts plug-in, and refreshes the Audio Unit cache
#  6. checks the result and offers to open Cubase
#
# Written for the bash 3.2 that ships with macOS. Log: ~/Library/Logs/Artifacts Button.log
set -u
export PATH="/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin:$PATH"

HERE="$(cd "$(dirname "$0")" && pwd)"
export ARTI_ICON="${ARTI_ICON:-$HERE/AppIcon.icns}"
SUPPORT="$HOME/Library/Application Support/Artifacts Button"
LOG="$HOME/Library/Logs/Artifacts Button.log"
TAG="${ARTIFACTS_TAG:-latest-build}"
SYSROOT="${ARTIFACTS_SYSROOT:-}"          # tests only: a stand-in for the system disk
PLUGINS="Console RA-1
Kintsugi
Duality
Anemoia
Equanimity"
RETIRED="Equanimity-1A"                    # names a plug-in had before; removed so Cubase shows one of each
USER_VST3="$HOME/Library/Audio/Plug-Ins/VST3";  SYS_VST3="$SYSROOT/Library/Audio/Plug-Ins/VST3"
USER_AU="$HOME/Library/Audio/Plug-Ins/Components"; SYS_AU="$SYSROOT/Library/Audio/Plug-Ins/Components"

mkdir -p "$SUPPORT" "$(dirname "$LOG")"
log() { echo "$(date '+%Y-%m-%d %H:%M:%S')  $*" >> "$LOG"; }
log "---- button pressed"

# ---------------------------------------------------------------- talking to the person at the Mac
APP="${ARTIFACTS_APP:-}"                  # set when the button app runs this: report with @lines, no extra dialogs
status() { log "$1"; if [ -n "$APP" ]; then echo "@status $1"; else note "$1"; fi; }
note() { osascript -e 'on run argv' -e 'display notification (item 1 of argv) with title "Artifacts"' -e 'end run' "$1" >/dev/null 2>&1; }
# ask "message" "default button" "button" ["button" ...]  -> prints the button pressed ("Cancel" if the dialog was closed)
ask() {
osascript - "$@" 2>/dev/null <<'AS'
on run argv
    set msg to item 1 of argv
    set def to item 2 of argv
    set btns to items 3 thru -1 of argv
    activate
    try
        set ic to POSIX file (system attribute "ARTI_ICON")
        set r to display dialog msg buttons btns default button def with title "Artifacts" with icon ic
    on error number n
        if n is -128 then return "Cancel"
        try
            set r to display dialog msg buttons btns default button def with title "Artifacts"
        on error
            return "Cancel"
        end try
    end try
    return button returned of r
end run
AS
}
# askText "message" "default answer" -> prints the text ("" if cancelled)
askText() {
osascript - "$@" 2>/dev/null <<'AS'
on run argv
    activate
    try
        set r to display dialog (item 1 of argv) default answer (item 2 of argv) buttons {"Cancel", "OK"} default button "OK" with title "Artifacts"
        return text returned of r
    on error
        return ""
    end try
end run
AS
}
fail() { log "FAILED: $1"; if [ -n "$APP" ]; then echo "@error $(echo "$1" | tr '\n' ' ' | tr -s ' ')"; else ask "$1" "OK" "OK" >/dev/null; fi; exit 1; }
cancel() { log "cancelled: $1"; [ -n "$APP" ] && echo "@cancel"; exit 0; }

# ---------------------------------------------------------------- 1. which repository, and fetch the build
REPO="${ARTIFACTS_REPO:-$(cat "$SUPPORT/repo" 2>/dev/null | tr -d '[:space:]')}"   # the app knows its repository
chooseRepo() {
    local r
    r="$(askText "Which GitHub repository holds Artifacts? Write it as owner/name, the way it appears in the web address (github.com/owner/name)." "${REPO:-yourname/Artifacts}")"
    r="$(echo "$r" | sed -e 's#^https\{0,1\}://github.com/##' -e 's#\.git$##' -e 's#/$##' | tr -d '[:space:]')"
    [ -z "$r" ] && cancel "at repository"
    echo "$r" | grep -Eq '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$' || fail "\"$r\" is not a repository name. It should look like owner/name."
    REPO="$r"; echo "$REPO" > "$SUPPORT/repo"; log "repository: $REPO"
}
[ -z "$REPO" ] && chooseRepo

TMP="$(mktemp -d "${TMPDIR:-/tmp}/artifacts-button.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

fetch() {
    rm -f "$TMP"/*.zip "$TMP/build-info.txt"
    local base="${ARTIFACTS_BASE_URL:-https://github.com/$REPO/releases/download/$TAG}" ok=1 f
    for f in Artifacts-VST3.zip Artifacts-AU.zip; do
        curl -fsSL --retry 3 --connect-timeout 15 -o "$TMP/$f" "$base/$f" >> "$LOG" 2>&1 || ok=0
    done
    curl -fsSL --retry 2 -o "$TMP/build-info.txt" "$base/build-info.txt" >> "$LOG" 2>&1
    if [ $ok = 0 ] && command -v gh >/dev/null && gh auth status >/dev/null 2>&1; then   # a private repository
        log "plain download failed; trying the GitHub command-line tool"
        gh release download "$TAG" -R "$REPO" -p 'Artifacts-VST3.zip' -p 'Artifacts-AU.zip' -p 'build-info.txt' -D "$TMP" --clobber >> "$LOG" 2>&1 && ok=1
    fi
    [ $ok = 1 ]
}
# A build still running on GitHub (a change was just pushed)? Wait for it, so the newest version is the one installed.
waitForBuild() {
    local api="https://api.github.com/repos/$REPO/actions/runs?per_page=1&branch=main" j="$TMP/run.json" st concl n=0
    [ -n "${ARTIFACTS_BASE_URL:-}" ] && return 0
    while :; do
        curl -fsSL --connect-timeout 15 -H "Accept: application/vnd.github+json" -o "$j" "$api" 2>>"$LOG" || return 0   # private or offline: just use the last release
        st="$(plutil -extract workflow_runs.0.status raw -o - "$j" 2>/dev/null)"
        concl="$(plutil -extract workflow_runs.0.conclusion raw -o - "$j" 2>/dev/null)"
        [ -z "$st" ] && return 0
        if [ "$st" = "completed" ]; then
            [ "$concl" = "success" ] && return 0
            log "latest GitHub build: $concl"
            status "The newest GitHub build did not succeed ($concl); installing the last good build."
            sleep 2; return 0
        fi
        [ $n -ge 90 ] && fail "GitHub has been building for over 45 minutes. Press again later."
        status "GitHub is building the newest version... ($((n / 2)) min so far). This can take 15 minutes."
        sleep 30; n=$((n + 1))
    done
}
status "Checking GitHub for the newest build..."
waitForBuild
status "Fetching Artifacts from GitHub..."
while ! fetch; do
    log "download failed for $REPO"
    [ -n "$APP" ] && fail "Couldn't fetch Artifacts from github.com/$REPO. Is the Mac online? Press again to retry."
    a="$(ask "Couldn't fetch the Artifacts build from github.com/$REPO.

Either: the repository name is wrong, the GitHub build hasn't finished yet (it takes about 15 minutes after a push), the repository is private (install the GitHub command-line tool and sign in with: gh auth login), or the Mac is offline." "Try again" "Cancel" "Change repository" "Try again")"
    case "$a" in
        "Change repository") chooseRepo ;;
        "Try again") ;;
        *) cancel "after failed download" ;;
    esac
done
BUILD="$(head -n 3 "$TMP/build-info.txt" 2>/dev/null | tr '\n' ' ')"
log "downloaded build: ${BUILD:-unknown}"

# ---------------------------------------------------------------- 2. unpack and check
mkdir -p "$TMP/x"
ditto -x -k "$TMP/Artifacts-VST3.zip" "$TMP/x" >> "$LOG" 2>&1 || fail "The downloaded VST3 archive is damaged. Press the button again; if it keeps happening, look at the GitHub build."
ditto -x -k "$TMP/Artifacts-AU.zip"   "$TMP/x" >> "$LOG" 2>&1 || fail "The downloaded Audio Unit archive is damaged. Press the button again; if it keeps happening, look at the GitHub build."
MISSING=""
IFS='
'
for p in $PLUGINS; do
    for b in "$TMP/x/vst3/$p.vst3" "$TMP/x/au/$p.component"; do
        if [ ! -d "$b" ]; then MISSING="$MISSING
  $(basename "$b")"; continue; fi
        xattr -dr com.apple.quarantine "$b" 2>/dev/null
        if ! codesign --verify --deep "$b" >/dev/null 2>&1; then
            log "re-signing $(basename "$b")"
            codesign --force --deep --sign - "$b" >> "$LOG" 2>&1
        fi
    done
done
[ -n "$MISSING" ] && fail "The build is missing:$MISSING

Nothing was changed. Check the latest GitHub build of $REPO."

# ---------------------------------------------------------------- 3. Cubase must be closed to see new plug-ins
cubaseRunning() { ps -axo comm= | grep -E '/(Cubase|Nuendo)[^/]*\.app/Contents/MacOS/' | head -n 1; }
running="$(cubaseRunning)"
if [ -n "$running" ]; then
    appName="$(echo "$running" | sed -E 's#.*/([^/]*)\.app/Contents/MacOS/.*#\1#')"
    a="$(ask "$appName is open. It only looks for plug-ins when it starts, so it needs to close first.

Quit asks $appName to quit the normal way: it will ask you to save anything unsaved." "Quit $appName" "Cancel" "Install anyway" "Quit $appName")"
    case "$a" in
        "Quit $appName")
            log "asking $appName to quit"
            osascript -e 'on run argv' -e 'tell application (item 1 of argv) to quit' -e 'end run' "$appName" >/dev/null 2>&1
            n=0; while [ -n "$(cubaseRunning)" ] && [ $n -lt 300 ]; do sleep 1; n=$((n + 1)); done
            [ -n "$(cubaseRunning)" ] && fail "$appName is still open, so nothing was installed. Quit it and press the button again." ;;
        "Install anyway") log "installing with $appName open" ;;
        *) cancel "Cubase open" ;;
    esac
fi

# ---------------------------------------------------------------- 4. install
status "Installing the plug-ins..."
mkdir -p "$USER_VST3" "$USER_AU"
ADMIN="$TMP/admin.sh"; : > "$ADMIN"
WHERE=""
install1() {   # $1 bundle in the download, $2 user folder, $3 system folder
    local name; name="$(basename "$1")"
    if [ -e "$3/$name" ]; then     # the Mac installer put it in the system folder: update it there
        printf 'rm -rf %q && ditto %q %q\n' "$3/$name" "$1" "$3/$name" >> "$ADMIN"
        WHERE="$WHERE
$3/$name"
        rm -rf "$2/$name"          # and no second copy in the user folder
    else
        rm -rf "$2/$name" && ditto "$1" "$2/$name" >> "$LOG" 2>&1 || fail "Couldn't copy $name into $2."
        WHERE="$WHERE
$2/$name"
    fi
}
for p in $PLUGINS; do
    install1 "$TMP/x/vst3/$p.vst3" "$USER_VST3" "$SYS_VST3"
    install1 "$TMP/x/au/$p.component" "$USER_AU" "$SYS_AU"
done
for r in $RETIRED; do
    rm -rf "$USER_VST3/$r.vst3" "$USER_AU/$r.component"
    for s in "$SYS_VST3/$r.vst3" "$SYS_AU/$r.component"; do [ -e "$s" ] && printf 'rm -rf %q\n' "$s" >> "$ADMIN"; done
done
if [ -s "$ADMIN" ]; then
    log "updating copies in the system plug-in folders (admin):"; cat "$ADMIN" >> "$LOG"
    if [ -n "$SYSROOT" ]; then /bin/bash "$ADMIN" >> "$LOG" 2>&1   # tests: no password
    else
        osascript -e 'on run argv' -e 'do shell script "/bin/bash " & quoted form of (item 1 of argv) with prompt "Artifacts is updating plug-ins that the installer put in the shared plug-in folders." with administrator privileges' -e 'end run' "$ADMIN" >> "$LOG" 2>&1 \
            || fail "The plug-ins in the shared plug-in folders could not be updated (no password given). Press the button again to retry."
    fi
fi

# ---------------------------------------------------------------- 5. Cubase blocklist and the Audio Unit cache
status "Getting Cubase ready..."
for bl in "$HOME/Library/Preferences/"Cubase*/*"VST3 Cache"/vst3blacklist.xml "$HOME/Library/Preferences/"Nuendo*/*"VST3 Cache"/vst3blacklist.xml; do
    [ -f "$bl" ] || continue
    if grep -Eq 'Console RA-1|Kintsugi|Duality|Anemoia|Equanimity' "$bl"; then
        cp "$bl" "$bl.before-artifacts-$(date +%Y%m%d-%H%M%S)"
        rm -f "$bl"
        log "Cubase had blocklisted an Artifacts plug-in: cleared $bl (backup kept beside it)"
    fi
done
killall -9 AudioComponentRegistrar >/dev/null 2>&1

# ---------------------------------------------------------------- 6. check and finish
BAD=""
for f in $WHERE; do
    [ -z "$f" ] && continue
    if [ ! -d "$f" ] || ! codesign --verify --deep "$f" >/dev/null 2>&1; then BAD="$BAD
  $f"; fi
done
unset IFS
[ -n "$BAD" ] && fail "These did not install correctly:$BAD

See the log: ~/Library/Logs/Artifacts Button.log"
[ -n "$BUILD" ] && echo "$BUILD" > "$SUPPORT/installed.txt"
log "installed OK: $(echo $WHERE | tr '\n' ' ')"

if [ -n "$APP" ]; then
    echo "@done Ready. Open Cubase (or press the green button).${BUILD:+ Build: $BUILD}"
    log "done"; exit 0
fi
CUBASE="$(ls -d /Applications/Cubase*.app /Applications/*/Cubase*.app 2>/dev/null | sort | tail -n 1)"
msg="Artifacts is installed and ready:

Console RA-1, Kintsugi, Duality, Anemoia and Equanimity
(VST3 for Cubase, Audio Units for Logic)${BUILD:+

Build: $BUILD}"
if [ -n "$CUBASE" ]; then
    a="$(ask "$msg" "Open $(basename "$CUBASE" .app)" "Done" "Open $(basename "$CUBASE" .app)")"
    [ "$a" = "Open $(basename "$CUBASE" .app)" ] && open "$CUBASE"
else
    ask "$msg" "Done" "Done" >/dev/null
fi
log "done"
exit 0
