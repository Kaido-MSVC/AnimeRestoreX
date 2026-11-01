# Downloads the official Qt 6.12.0 source archives of the modules shipped with
# AnimeRestoreX (qtbase, qtimageformats, qttranslations; unmodified) and checks
# them against download.qt.io's md5sums.txt. These archives are published next
# to the release zip to satisfy LGPLv3 / GPLv3 section 6 (Corresponding Source).
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$Root = Split-Path -Parent $PSScriptRoot
$Dest = Join-Path $Root 'third_party\_download\qt-src'
$Base = 'https://download.qt.io/official_releases/qt/6.12/6.12.0/submodules/'

# SHA256 recorded on 2026-11-01 after the md5 check passed.
$Expected = [ordered]@{
    'qtbase-everywhere-src-6.12.0.zip'         = '0529901575c413e34ded504eeda177c4a6dfd0dff1e39ed52c811dc5204b43c7'
    'qtimageformats-everywhere-src-6.12.0.zip' = '664de2ba60a1e74105da844c061688ff5e6f8f046d5dd98e2d6b6ad3c553b4ca'
    'qttranslations-everywhere-src-6.12.0.zip' = '286f0ca3ad669bc2d6fec0e209a797a08049d037c7a167e60eb3b4a640405aa5'
}

New-Item -ItemType Directory -Force $Dest | Out-Null
foreach ($name in $Expected.Keys) {
    $file = Join-Path $Dest $name
    if (-not (Test-Path $file)) {
        Write-Host "Downloading $name"
        Invoke-WebRequest -Uri ($Base + $name) -OutFile $file -UseBasicParsing
    }
    $sha = (Get-FileHash $file -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($sha -ne $Expected[$name]) {
        throw "SHA256 mismatch for $name`n expected $($Expected[$name])`n actual   $sha"
    }
    Write-Host "OK  $name"
}
