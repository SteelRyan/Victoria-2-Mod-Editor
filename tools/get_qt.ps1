# Fetches Qt 6.11.2's C++ kit (MSVC 2022, 64-bit) from Qt's own servers into ..\Qt\6.11.2\msvc2022_64.
# Why not aqtinstall: its newest release (3.3.0) still looks for the old repository layout (qt6_6112/qt6_6112/),
# while Qt now keeps every kit in a folder of its own (qt6_6112/qt6_6112_msvc2022_64/) — found 2026-10-19.
# Only the parts the editor needs are taken; every archive is checked against Qt's own .sha1 before unpacking.
# -Package: an add-on lives in a package of its own (qtimageformats: the .tga / .tiff / .webp readers PySide6 has)
param([string[]]$Parts = @("qtbase", "qtsvg"), [string]$Package = "qt.qt6.6112.win64_msvc2022_64")
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent          # the work folder
$repo = "https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/qt6_6112/qt6_6112_msvc2022_64"
$pkg = $Package
$dl = Join-Path $root "_downloads\qt 6.11.2"
$dest = Join-Path $root "Qt\6.11.2\msvc2022_64"
$sz = "C:\Program Files\7-Zip\7z.exe"
New-Item -ItemType Directory -Force $dl, $dest | Out-Null
[xml]$x = (Invoke-WebRequest -Uri "$repo/Updates.xml" -UseBasicParsing -TimeoutSec 60).Content
$p = $x.Updates.PackageUpdate | Where-Object { $_.Name -eq $pkg }
if (-not $p) { throw "package $pkg not in Updates.xml" }
$archives = $p.DownloadableArchives -split "," | ForEach-Object { $_.Trim() } |
    Where-Object { $a = $_; $Parts | Where-Object { $a.StartsWith($_ + "-") } }
foreach ($a in $archives) {
    $url = "$repo/$pkg/$($p.Version)$a"
    $file = Join-Path $dl $a
    if (-not (Test-Path $file)) {
        Write-Output "download $a"
        Invoke-WebRequest -Uri $url -OutFile $file -UseBasicParsing -TimeoutSec 1800
    }
    $raw = (Invoke-WebRequest -Uri "$url.sha1" -UseBasicParsing -TimeoutSec 60).Content
    if ($raw -is [byte[]]) { $raw = [Text.Encoding]::ASCII.GetString($raw) }      # (served as bytes, not text)
    $want = (([string]$raw).Trim() -split "\s+")[0].ToLower()
    $have = (Get-FileHash $file -Algorithm SHA1).Hash.ToLower()
    if ($want -ne $have) { throw "SHA-1 mismatch for ${a}: Qt says $want, file is $have" }
    Write-Output ("ok   {0}  {1:N1} MB  sha1 {2}" -f $a, ((Get-Item $file).Length / 1MB), $have)
    & $sz x $file "-o$dest" -y | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "7-Zip could not unpack $a" }
}
# Qt's archives are built for C:\Qt\...: a qt.conf beside the tools makes them look here instead
$conf = "[Paths]`r`nPrefix=..`r`n"
foreach ($bin in @("bin", "libexec")) {
    $d = Join-Path $dest $bin
    if (Test-Path $d) { [IO.File]::WriteAllText((Join-Path $d "qt.conf"), $conf) }
}
Write-Output "Qt is in $dest"
