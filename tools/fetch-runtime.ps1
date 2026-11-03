# Fetch the pinned Windows ML package (Microsoft.Windows.AI.MachineLearning) into
# third_party\windowsml\<version>. Only the ONNX Runtime C/C++ API, onnxruntime.dll and
# DirectML.dll from it are used by the build (no EP catalog, no WinML Runtime API).
#
# Pin: 2.5.96 (ONNX Runtime 1.28.3, DirectML 1.15.4), verified in docs/v2.0-P0-P1報告.md.
# SHA-256 from the P0/P1 download (tools/winml-probe/fetch-deps.ps1).
[CmdletBinding()]
param(
    # Folder that may already hold the .nupkg (e.g. tools\winml-probe\_work\dl\nupkg);
    # used only if its SHA-256 matches.
    [string]$Cache = '',
    [switch]$Force
)
$ErrorActionPreference = 'Stop'

$Version = '2.5.96'
$Name    = "microsoft.windows.ai.machinelearning.$Version.nupkg"
$Url     = "https://api.nuget.org/v3-flatcontainer/microsoft.windows.ai.machinelearning/$Version/$Name"
$Sha256  = '35d6e84feb4142e8cecaf36704fa3ab0fc8251d892850fb80e8341f7d09653a3'

$Root = Split-Path -Parent $PSScriptRoot
$Work = Join-Path $Root 'third_party\_download'
$Dest = Join-Path $Root "third_party\windowsml\$Version"
$Pkg  = Join-Path $Work $Name

function Test-Sha([string]$Path) {
    (Test-Path $Path) -and ((Get-FileHash $Path -Algorithm SHA256).Hash.ToLowerInvariant() -eq $Sha256)
}

if ((Test-Path (Join-Path $Dest 'build\cmake')) -and -not $Force) {
    Write-Host "third_party\windowsml\$Version already present (use -Force to refetch)."
    exit 0
}

New-Item -ItemType Directory -Force $Work | Out-Null
if (-not (Test-Sha $Pkg)) {
    $cached = if ($Cache) { Join-Path $Cache $Name } else { '' }
    if ($cached -and (Test-Sha $cached)) {
        Write-Host "Using cached $cached"
        Copy-Item $cached $Pkg -Force
    } else {
        Write-Host "Downloading $Url"
        Invoke-WebRequest -Uri $Url -OutFile $Pkg -UseBasicParsing
    }
}
if (-not (Test-Sha $Pkg)) {
    Remove-Item $Pkg -Force
    throw "SHA256 mismatch for $Name (expected $Sha256)"
}
Write-Host "SHA256 OK: $Name"

if (Test-Path $Dest) { Remove-Item $Dest -Recurse -Force }
New-Item -ItemType Directory -Force $Dest | Out-Null
# bsdtar (System32\tar.exe) reads the nupkg (zip) and refuses absolute and ".." member paths.
& "$env:SystemRoot\System32\tar.exe" -xf $Pkg -C $Dest
if ($LASTEXITCODE) { throw "extract failed: $Pkg" }
Write-Host "Installed to $Dest"
