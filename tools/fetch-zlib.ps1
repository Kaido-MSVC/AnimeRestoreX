# Fetch the pinned zlib source into third_party\zlib\1.4 (not in git). The build compiles only
# the inflate files of it into a static library (src/CMakeLists.txt, target arx_zlib), used to
# decompress PNG iCCP with a hard output limit (docs/v2.2-安全審查.md S-01).
#
# zlib 1.4 (2026-11-05) is the newest stable release on 2026-11-15. The SHA-256 below was taken
# on that day: the GitHub release asset and https://zlib.net/zlib-1.4.tar.gz were identical, and
# zlib-1.4.tar.gz.asc verified as a good signature of Mark Adler (key 5ED46A67 21D36558 7791E2AA
# 783FCD8E 58BCAFBA).
[CmdletBinding()]
param([switch]$Force)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$Version = '1.4'
$Name    = "zlib-$Version.tar.gz"
$Url     = "https://github.com/madler/zlib/releases/download/v$Version/$Name"
$Sha     = '02dd159783677a749c04adbdd02b55b8a2bcc7e054f51b76b549199a916ad584'

$Root = Split-Path -Parent $PSScriptRoot
$Work = Join-Path $Root 'third_party\_download'
$Dest = Join-Path $Root "third_party\zlib\$Version"
$Archive = Join-Path $Work $Name

function Test-Sha([string]$Path) {
    (Test-Path $Path) -and ((Get-FileHash $Path -Algorithm SHA256).Hash.ToLowerInvariant() -eq $Sha)
}

if ((Test-Path (Join-Path $Dest 'zlib.h')) -and -not $Force) {
    Write-Host "third_party\zlib\$Version already present (use -Force to refetch)."
    return
}
New-Item -ItemType Directory -Force $Work | Out-Null
if (-not (Test-Sha $Archive)) {
    Write-Host "Downloading $Url"
    Invoke-WebRequest -Uri $Url -OutFile $Archive -UseBasicParsing
}
if (-not (Test-Sha $Archive)) {
    Remove-Item $Archive -Force
    throw "SHA256 mismatch for $Name (expected $Sha)"
}
Write-Host "SHA256 OK: $Name"

if (Test-Path $Dest) { Remove-Item $Dest -Recurse -Force }
New-Item -ItemType Directory -Force $Dest | Out-Null
# bsdtar (System32\tar.exe) refuses absolute and ".." member paths.
& "$env:SystemRoot\System32\tar.exe" -xzf $Archive -C $Dest --strip-components 1
if ($LASTEXITCODE) { throw "extract failed: $Archive" }
if (-not (Test-Path (Join-Path $Dest 'zlib.h'))) { throw "zlib.h missing after extracting $Name" }
Write-Host "Installed to $Dest"
