# ModEditor — a mod editor for Victoria II

ModEditor opens a Victoria II mod (or the base game) and edits it: the map and its provinces, pops, nations,
states, technologies, reforms, units, events and decisions, the interface's pictures and the 4K submod — and
writes the changes back into the mod's own files.

## Download

The program is on this repository's **Releases** page: the files are below each release, under *Assets*. Unpack
the RAR into any folder and run `ModEditor.exe`; there is no installer.

The source code of every release is here in the repository, open for anyone to read and build.

### Check a download

Its SHA-256 must match the one in the release notes (PowerShell, in the folder with the download):

```powershell
Get-FileHash "ModEditor 1.0.2.rar" -Algorithm SHA256
```

A copy from anywhere else, or one whose check fails, is not from here.

## What it does on your computer

- Reads the game folder and the mods you open; writes only into the mod you edit (plus its own data folder: backups,
  changelogs, settings).
- Keeps a copy of the original mod before the first change of a session (can be switched off).
- **Network:** once a day at most it asks GitHub's public API which release is the newest
  (`api.github.com/repos/SteelRyan/Victoria-2-Mod-Editor/releases/latest`) and shows a note if there is a newer
  one. It sends nothing about you or your mods, downloads nothing and runs nothing. Switch it off with the tick
  "Tell me as soon as a new version of ModEditor is out" in the opening window. The code: `src/app/update.cpp`.

## Build it yourself

Needs Windows 10/11 (64-bit), Visual Studio 2022 or newer with "Desktop development with C++", and 7-Zip.

```powershell
tools\get_qt.ps1 -Parts qtbase
tools\get_qt.ps1 -Parts qtimageformats -Package qt.qt6.6112.addons.qtimageformats.win64_msvc2022_64
tools\build.ps1 -Target ModEditor -NoInstall
```

`get_qt.ps1` fetches Qt 6.11.2 from Qt's own servers (each archive checked against Qt's SHA-1) into `..\Qt`. The
editor lands in `release\ModEditor`.

## Licences

ModEditor is open source under the **Apache License 2.0** (`LICENSE`): free to use, change and share, also in your
own projects. If you share it or a changed version, keep `LICENSE` and `NOTICE` with it (the credit to the original
author) and mark the files you changed. It comes as it is, with no warranty: see the licence for the full terms.

It uses Qt 6.11.2 under the LGPL-3.0, linked dynamically — `licenses\` has the texts and the third-party notices;
they ship in every release too.

Victoria II is a game by Paradox Interactive. ModEditor is not made or endorsed by Paradox Interactive and contains
none of the game's files.
