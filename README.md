# BluRemote

A Bluesound / BluOS touch controller for an old 32-bit Windows tablet (built for an Acer Iconia W1-810: Atom, 1 GB RAM, Windows 10 32-bit).

It is one small program. It serves the controller page on the tablet itself (`http://127.0.0.1:11011`) and passes commands to the player's local API on port 11000. No cloud, no account.

## Install on the tablet
1. It opens the controller full-screen in Edge (the new Chromium Edge). Firefox or Chrome are used if Edge is missing.
2. Download `BluRemote.exe` from the `bluremote-latest` release and run it.
   Windows may warn about an unknown app: **More info → Run anyway**. Allow it on private networks if the firewall asks.
3. It copies itself to `%LOCALAPPDATA%\BluRemote`, starts with Windows, keeps the screen awake and finds the player.

Update: download and run the new `BluRemote.exe`; it replaces the old one.
Uninstall: run `BluRemote.exe --uninstall`.

## What it does
Now playing with cover art, play/pause, skip, volume and mute, presets, switching between players, and a dim clock screen when idle.

## Build
Pushing to this branch builds `BluRemote.exe` (Go, `GOOS=windows GOARCH=386`) and publishes it to the `bluremote-latest` release.
