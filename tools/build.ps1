# Builds the C++ editor: sets up MSVC (vcvars64), configures with CMake + Ninja against ..\Qt\6.11.2\msvc2022_64,
# builds, and (Release) makes the release folder release\ModEditor with only the Qt files the editor needs
# (PLAN.md §4.6), then copies it into the user's
# editor folder <work>\ModEditor (its data there left alone), and packs it as a RAR to send to a tester:
# <work>\ModEditor releases\ModEditor <date time>.rar (the user 2026-11-14: "whenever you build the version, I want
# you to give me a RAR for it ... just the mod editor ... minimal space usage"). The RAR holds only the program: never
# the editor's data (backups, changelogs, ...) and nothing for development. Usage: tools\build.ps1
# [-Config Release|Debug] [-NoDeploy] [-NoInstall]
# [-Target ModEditor] builds the editor only (no checks) - what the public build on GitHub does.
# [-Out <folder>] makes the release folder there instead of release\ModEditor (its last part names the RAR's folder).
param([string]$Config = "Release", [switch]$NoDeploy, [switch]$NoInstall, [string]$Target = "", [string]$Out = "")
$ErrorActionPreference = "Stop"
$port = Split-Path $PSScriptRoot -Parent
$work = Split-Path $port -Parent
$qt = Join-Path $work "Qt\6.11.2\msvc2022_64"
$vs = "C:\Program Files\Microsoft Visual Studio\18\Community"
if (-not (Test-Path $vs)) {                                         # (another PC: the newest Visual Studio with C++)
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { throw "no Visual Studio with the C++ tools found" }
}
$vcvars = Join-Path $vs "VC\Auxiliary\Build\vcvars64.bat"
$cmake = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$ninja = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
$build = Join-Path $port ("build\" + $Config.ToLower())

# the MSVC environment, taken over from vcvars64.bat into this PowerShell
cmd /c "`"$vcvars`" >nul && set" | ForEach-Object {
    if ($_ -match "^([^=]+)=(.*)$") { Set-Item -Path "env:$($matches[1])" -Value $matches[2] }
}
& $cmake -S $port -B $build -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" "-DCMAKE_BUILD_TYPE=$Config" "-DCMAKE_PREFIX_PATH=$qt" | Out-Null
if ($LASTEXITCODE) { throw "configure failed" }
if ($Target) { & $cmake --build $build --target $Target } else { & $cmake --build $build }
if ($LASTEXITCODE) { throw "build failed" }
if ($Config -ne "Release" -or $NoDeploy) { return }

# the release folder: the exe + the Qt files it needs, nothing else
$rel = if ($Out) { $Out } else { Join-Path $port "release\ModEditor" }
if (Test-Path $rel) {                                               # (our own build output, made again each time -
    # but never a folder an editor runs from, nor one holding more than the program: the editor keeps its data next
    # to its exe, library, backups, changelogs ...)
    $busy = Get-Process -Name ModEditor -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "$rel\*" }
    if ($busy) { throw "ModEditor runs from $rel - close it, or build with -Out <another folder>" }
    $ours = @("ModEditor.exe", "*.dll", "imageformats", "platforms", "styles", "licenses")
    $extra = @(Get-ChildItem $rel -Force | Where-Object { $n = $_.Name; -not ($ours | Where-Object { $n -like $_ }) })
    if ($extra) { throw ("$rel holds more than the program (" + ($extra.Name -join ", ") + ") - move it aside, or build with -Out <another folder>") }
    Remove-Item $rel -Recurse -Force
}
New-Item -ItemType Directory -Force $rel | Out-Null
Copy-Item (Join-Path $build "ModEditor.exe") $rel
& (Join-Path $qt "bin\windeployqt.exe") --release --no-translations --no-system-d3d-compiler --no-system-dxc-compiler --no-opengl-sw `
    --no-compiler-runtime --skip-plugin-types generic,networkinformation,tls,iconengines `
    --exclude-plugins qtiff,qicns,qico,qwbmp,qsvg `
    (Join-Path $rel "ModEditor.exe") | Out-Null
if ($LASTEXITCODE) { throw "windeployqt failed" }
# the VC runtime the DLLs need (the user's PC may not have it): the two DLLs, not the installer
$crt = Get-ChildItem (Join-Path $vs "VC\Redist\MSVC") -Directory | Sort-Object Name -Descending |
    ForEach-Object { Join-Path $_.FullName "x64\Microsoft.VC145.CRT" } | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $crt) {
    $crt = Get-ChildItem (Join-Path $vs "VC\Redist\MSVC") -Recurse -Directory -Filter "Microsoft.VC*.CRT" |
        Where-Object { $_.FullName -match "\\x64\\" -and $_.FullName -notmatch "onecore|debug" } | Select-Object -First 1 -ExpandProperty FullName
}
foreach ($d in @("msvcp140.dll", "msvcp140_1.dll", "vcruntime140.dll", "vcruntime140_1.dll")) {
    Copy-Item (Join-Path $crt $d) $rel
}
# (picture formats the editor never reads are left out above: TIFF, ICNS, ICO, WBMP, SVG - 2026-11-14. Kept: TGA,
# JPEG for the pages that load pictures, GIF / WEBP for the picture boards; PNG, BMP and DDS need no plugin.)
foreach ($x in @("Qt6Svg.dll")) {                                   # (only the SVG plugin wanted it)
    $f = Join-Path $rel $x
    if (Test-Path $f) { Remove-Item $f -Force }
}
# Qt's licence (LGPL-3.0 asks for its text and a notice next to the DLLs) and the third-party notices (2026-11-15)
Copy-Item (Join-Path $port "licenses") (Join-Path $rel "licenses") -Recurse
# the editor's own licence (Apache-2.0, the user's choice 2026-11-17) and its NOTICE: Apache asks for both in every copy
foreach ($x in @("LICENSE", "NOTICE")) {
    $f = @((Join-Path $port $x), (Join-Path $port "public\$x")) | Where-Object { Test-Path $_ } | Select-Object -First 1
    if ($f) { Copy-Item $f (Join-Path $rel "licenses\ModEditor $x.txt") }   # (the repository's top level, or public\ here)
}
Write-Output "release folder: $rel"

# the RAR for a tester: the release folder packed as "ModEditor\..." - the program only
$rar = "C:\Program Files\WinRAR\Rar.exe"
if (Test-Path $rar) {
    $outdir = Join-Path $work "ModEditor releases"
    New-Item -ItemType Directory -Force $outdir | Out-Null
    # the version from src\app\version.h (the one place it is written), e.g. "ModEditor 1.0.1 2026-11-15 1230.rar"
    $vh = [IO.File]::ReadAllText((Join-Path $PSScriptRoot "..\src\app\version.h"))
    $ver = if ($vh -match 'MODEDITOR_VERSION_TEXT\s+"([^"]+)"') { $Matches[1] + " " } else { "" }
    $pack = Join-Path $outdir ("ModEditor " + $ver + (Get-Date -Format "yyyy-MM-dd HHmm") + ".rar")
    if (Test-Path $pack) { Remove-Item $pack -Force }               # (our own pack of the same minute)
    & $rar a -m5 -s -md256m -ep1 -r -idq $pack $rel | Out-Null
    if ($LASTEXITCODE) { throw "rar failed ($LASTEXITCODE)" }
    Write-Output ("RAR for a tester: {0} ({1:N1} MB)" -f $pack, ((Get-Item $pack).Length / 1MB))
    Write-Output ("SHA-256: " + (Get-FileHash $pack -Algorithm SHA256).Hash.ToLower())   # (for the release notes)
} else {
    Write-Output "NO RAR: WinRAR's Rar.exe is not at $rar"
}
if ($NoInstall) { return }

# the user's editor, <work>\ModEditor (2026-11-08, the user's word: the C++ editor first in the work folder, the
# Python one in _old versions). Only the program's own files are copied over it; the editor's data there (library,
# backups, changelogs, almanacs, flagdesigns, errors.log …) is never touched.
$inst = Join-Path $work "ModEditor"
$busy = Get-Process -Name ModEditor -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "$inst\*" }
if ($busy) {
    Write-Output "NOT installed: ModEditor is open from $inst - close it, then run tools\build.ps1 again"
    return
}
New-Item -ItemType Directory -Force $inst | Out-Null
& robocopy $rel $inst /E /R:1 /W:1 /NJH /NJS /NFL /NDL /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw "install into $inst failed (robocopy $LASTEXITCODE)" }
$global:LASTEXITCODE = 0
Write-Output "installed: $inst"
