# Artifacts

A Reluctant Astronaut plugin suite, made with AI and GitHub.

| Plugin | What it is |
|---|---|
| Console RA-1 | Desk channel: drive, transformer (Nickel/Steel/Iron), channel tolerance, compression, VU meters |
| Kintsugi | Valve, tape (wow, flutter, hum, wear) and a living Instability knob |
| Duality | Reverbs (Room/Hall/Plate), Impulse Responses, and Perspective: Object, Kensho (both fused) or Subject |
| Anemoia | Echos (free or tempo-synced) and Memories (granular cloud), with a Nostalgia knob |
| Equanimity | A Pultec EQP-1A program equalizer: coupled low boost and atten, high boost with bandwidth, midrange peaks and dip (MEQ-5 style), high atten, auto gain, tube makeup stage, Path (L/R, M/S, solo) |

## Build
`cmake -B build && cmake --build build --config Release` (JUCE 8.0.9 is fetched automatically; pass `-DJUCE_DIR=/path/to/JUCE` to use a local copy).
Push to GitHub: the workflow in `.github/workflows/build.yml` builds universal (Apple Silicon + Intel) VST3 and AU, signs them ad hoc, runs pluginval, and makes a single Mac installer (`Artifacts-Mac-Installer.zip`) plus a Windows VST3 zip.

## Test
`tools/run-tests.sh` builds the offline test rigs, runs pluginval (strictness 10) and the sonic tests: latency, null, response and phase, harmonics, aliasing, tone balance, mono compatibility, reverb quality, echo timing, stability and click checks. Results are tab-separated rows (`plugin test metric value verdict`) in `reports/data`.
See `reports/Artifacts-test-report.md`.

## The Artifacts Button (one-press install on a Mac)
`tools/ArtifactsButton` holds a small Mac app with a big red button as its icon. Click it and it:
fetches the latest build from this repository's `latest-build` release (published by the GitHub workflow on every push to main),
checks every plug-in, asks Cubase to quit if it is open, installs or updates the VST3s and Audio Units (in your own plug-in
folders, or where the Mac installer put them earlier, so there is never a second copy), clears an Artifacts entry from
Cubase's VST3 blocklist if there is one, and offers to open Cubase.

- First press: it asks for the repository name (owner/name). Change it later by deleting `~/Library/Application Support/Artifacts Button/repo`.
- Private repository: install the GitHub command-line tool (`brew install gh`) and run `gh auth login` once.
- First launch only: macOS blocks apps from unknown developers. Click Done, then System Settings > Privacy & Security > Open Anyway.
- Log: `~/Library/Logs/Artifacts Button.log`.
- Build the app yourself: `bash tools/ArtifactsButton/build-app.sh <folder>`; the workflow also attaches `Artifacts-Button.zip` to the release.
