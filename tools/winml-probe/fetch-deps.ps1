# Download (or reuse) the pinned probe dependencies into _work\ and verify SHA-256.
# Nothing is installed system-wide; everything stays under tools\winml-probe\_work
# (git-ignored). Use -AlsoOld to fetch the 2.4.89 package for comparison.
[CmdletBinding()]
param([switch]$AlsoOld, [switch]$NoVenv)
$ErrorActionPreference = 'Stop'
$Root = $PSScriptRoot
$W    = Join-Path $Root '_work'

$Assets = @(
    @{ Url  = 'https://api.nuget.org/v3-flatcontainer/microsoft.windows.ai.machinelearning/2.5.96/microsoft.windows.ai.machinelearning.2.5.96.nupkg'
       File = 'dl\nupkg\microsoft.windows.ai.machinelearning.2.5.96.nupkg'
       Sha  = '35d6e84feb4142e8cecaf36704fa3ab0fc8251d892850fb80e8341f7d09653a3'
       To   = 'pkg\2.5.96'; Members = @() },
    @{ Url  = 'https://github.com/nagadomi/nunif/releases/download/0.0.0/waifu2x_onnx_models_20250502.zip'
       File = 'dl\nunif\waifu2x_onnx_models_20250502.zip'
       Sha  = '4d531e299884f8895df82947939f85c5ff6fca15236b9b72f1aa8ea476132e08'
       To   = 'models'; Members = @('onnx_models/swin_unet/art', 'onnx_models/utils') },
    # Upstream runtime (v2.0 decision, docs/v2.0-P1重做報告.md). Extracted hashes:
    #   runtimes/win-x64/native/onnxruntime.dll e7eedec6a6f26dc39dc948276a75ef6d2bee3fff944d874ceed0bbd3b97bff40
    #   bin/x64-win/DirectML.dll                9c9e6d822561c6c41b90e6994b3e8857cf1d66dbfb1e0c4c799c7c89b4e92da1
    # onnxruntime_providers_shared.dll is in the package but was not needed (DML + CPU).
    @{ Url  = 'https://api.nuget.org/v3-flatcontainer/microsoft.ml.onnxruntime.directml/1.24.4/microsoft.ml.onnxruntime.directml.1.24.4.nupkg'
       File = 'dl\nupkg-upstream\microsoft.ml.onnxruntime.directml.1.24.4.nupkg'
       Sha  = '57e9f11b73437bef7a309496135d4c1f96b1a8e9ddba60013fa27bfc1d788681'
       To   = 'pkg\ort-dml-1.24.4'; Members = @() },
    @{ Url  = 'https://api.nuget.org/v3-flatcontainer/microsoft.ai.directml/1.15.4/microsoft.ai.directml.1.15.4.nupkg'
       File = 'dl\nupkg-upstream\microsoft.ai.directml.1.15.4.nupkg'
       Sha  = '4e7cb7ddce8cf837a7a75dc029209b520ca0101470fcdf275c1f49736a3615b9'
       To   = 'pkg\directml-1.15.4'; Members = @() }
)
if ($AlsoOld) {
    $Assets += @{ Url  = 'https://api.nuget.org/v3-flatcontainer/microsoft.windows.ai.machinelearning/2.4.89/microsoft.windows.ai.machinelearning.2.4.89.nupkg'
                  File = 'dl\nupkg\microsoft.windows.ai.machinelearning.2.4.89.nupkg'
                  Sha  = '5c68ecfb947223267abf159a023f5192ad42725e4e9cc995e7c1470dd54dff63'
                  To   = 'pkg\2.4.89'; Members = @() }
}

foreach ($a in $Assets) {
    $file = Join-Path $W $a.File
    New-Item -ItemType Directory -Force (Split-Path $file) | Out-Null
    $ok = (Test-Path $file) -and ((Get-FileHash $file -Algorithm SHA256).Hash -eq $a.Sha)
    if (-not $ok) {
        Write-Host "download $($a.Url)"
        Invoke-WebRequest -Uri $a.Url -OutFile $file
        $h = (Get-FileHash $file -Algorithm SHA256).Hash
        if ($h -ne $a.Sha) { Remove-Item $file; throw "SHA-256 mismatch for $($a.File): $h" }
    }
    Write-Host "verified $($a.File)"
    $to = Join-Path $W $a.To
    New-Item -ItemType Directory -Force $to | Out-Null
    # bsdtar (System32\tar.exe) reads zip/nupkg and refuses absolute and ".." member paths.
    & "$env:SystemRoot\System32\tar.exe" -xf $file -C $to @($a.Members)
    if ($LASTEXITCODE) { throw "extract failed: $file" }
}

if (-not $NoVenv -and -not (Test-Path (Join-Path $W 'venv\Scripts\python.exe'))) {
    & py -3.13 -m venv (Join-Path $W 'venv')
    & (Join-Path $W 'venv\Scripts\python.exe') -m pip install --disable-pip-version-check -r (Join-Path $Root 'scripts\requirements.txt')
    if ($LASTEXITCODE) { throw "pip install failed" }
}
