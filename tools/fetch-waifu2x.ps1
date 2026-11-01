# Fetch the pinned waifu2x-ncnn-vulkan prebuilt release into third_party/waifu2x.
# Only the executable, its runtime DLL, its LICENSE and models-cunet are kept.
#
# Pin: tag 20250915 (commit a86cfb043e6482b5f08778a114df9b5faf6e73b7)
# SHA256 below was computed locally on 2026-11-01 and matched the GitHub
# release asset "digest" field (sha256:7425be94...dbfdc87).
[CmdletBinding()]
param(
    [switch]$Force
)
$ErrorActionPreference = 'Stop'

$Tag      = '20250915'
$Repo     = 'nihui/waifu2x-ncnn-vulkan'
$Asset    = "waifu2x-ncnn-vulkan-$Tag-windows.zip"
$Sha256   = '7425be94b94e4c8f37a1e433ac0e0100c43790e2c37418f4b65d8235adfbdc87'
$Url      = "https://github.com/$Repo/releases/download/$Tag/$Asset"

$Root     = Split-Path -Parent $PSScriptRoot
$Dest     = Join-Path $Root 'third_party\waifu2x'
$Work     = Join-Path $Root 'third_party\_download'
$Zip      = Join-Path $Work $Asset

if ((Test-Path (Join-Path $Dest 'waifu2x-ncnn-vulkan.exe')) -and -not $Force) {
    Write-Host "third_party\waifu2x already present (use -Force to refetch)."
    exit 0
}

New-Item -ItemType Directory -Force $Work | Out-Null
if (-not (Test-Path $Zip)) {
    Write-Host "Downloading $Url"
    Invoke-WebRequest -Uri $Url -OutFile $Zip -UseBasicParsing
}

$actual = (Get-FileHash $Zip -Algorithm SHA256).Hash.ToLowerInvariant()
if ($actual -ne $Sha256) {
    Remove-Item $Zip -Force
    throw "SHA256 mismatch for $Asset`n expected $Sha256`n actual   $actual"
}
Write-Host "SHA256 OK: $actual"

$Extract = Join-Path $Work 'extract'
if (Test-Path $Extract) { Remove-Item $Extract -Recurse -Force }
Expand-Archive $Zip -DestinationPath $Extract
$Src = Join-Path $Extract "waifu2x-ncnn-vulkan-$Tag-windows"

if (Test-Path $Dest) { Remove-Item $Dest -Recurse -Force }
New-Item -ItemType Directory -Force $Dest | Out-Null
foreach ($f in 'waifu2x-ncnn-vulkan.exe', 'vcomp140.dll', 'LICENSE') {
    Copy-Item (Join-Path $Src $f) $Dest
}
Copy-Item (Join-Path $Src 'models-cunet') $Dest -Recurse

Remove-Item $Extract -Recurse -Force
Write-Host "Installed to $Dest"
