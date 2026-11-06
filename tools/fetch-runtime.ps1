# Fetch the pinned AI runtime into third_party\. Only the ONNX Runtime C/C++ API, onnxruntime.dll
# and DirectML.dll are used by the build.
#
#   -Flavor upstream  (default): Microsoft.ML.OnnxRuntime.DirectML 1.24.4 + Microsoft.AI.DirectML
#                     1.15.4, verified in docs/v2.0-P1重做報告.md
#   -Flavor windowsml : Microsoft.Windows.AI.MachineLearning 2.5.96 (ONNX Runtime 1.28.3,
#                     DirectML 1.15.4), the runtime of the earlier 2.0 work, kept for switching
#                     back (docs/v2.0-P0-P1報告.md). tools\package.ps1 does not package it.
[CmdletBinding()]
param(
    [ValidateSet('upstream', 'windowsml')]
    [string]$Flavor = 'upstream',
    # Folder that may already hold the .nupkg files (e.g. tools\winml-probe\_work\dl\nupkg-upstream);
    # used only if the SHA-256 matches.
    [string]$Cache = '',
    [switch]$Force
)
$ErrorActionPreference = 'Stop'

$Packages = if ($Flavor -eq 'upstream') {
    @(
        @{ Id = 'microsoft.ml.onnxruntime.directml'; Version = '1.24.4'; Folder = 'third_party\onnxruntime\1.24.4'
           Sha = '57e9f11b73437bef7a309496135d4c1f96b1a8e9ddba60013fa27bfc1d788681'; Marker = 'runtimes\win-x64\native\onnxruntime.dll' },
        @{ Id = 'microsoft.ai.directml'; Version = '1.15.4'; Folder = 'third_party\directml\1.15.4'
           Sha = '4e7cb7ddce8cf837a7a75dc029209b520ca0101470fcdf275c1f49736a3615b9'; Marker = 'bin\x64-win\DirectML.dll' }
    )
} else {
    @(
        @{ Id = 'microsoft.windows.ai.machinelearning'; Version = '2.5.96'; Folder = 'third_party\windowsml\2.5.96'
           Sha = '35d6e84feb4142e8cecaf36704fa3ab0fc8251d892850fb80e8341f7d09653a3'; Marker = 'build\cmake' }
    )
}

$Root = Split-Path -Parent $PSScriptRoot
$Work = Join-Path $Root 'third_party\_download'

function Test-Sha([string]$Path, [string]$Sha) {
    (Test-Path $Path) -and ((Get-FileHash $Path -Algorithm SHA256).Hash.ToLowerInvariant() -eq $Sha)
}

foreach ($p in $Packages) {
    $name = "$($p.Id).$($p.Version).nupkg"
    $url  = "https://api.nuget.org/v3-flatcontainer/$($p.Id)/$($p.Version)/$name"
    $dest = Join-Path $Root $p.Folder
    $pkg  = Join-Path $Work $name

    if ((Test-Path (Join-Path $dest $p.Marker)) -and -not $Force) {
        Write-Host "$($p.Folder) already present (use -Force to refetch)."
        continue
    }
    New-Item -ItemType Directory -Force $Work | Out-Null
    if (-not (Test-Sha $pkg $p.Sha)) {
        $cached = if ($Cache) { Join-Path $Cache $name } else { '' }
        if ($cached -and (Test-Sha $cached $p.Sha)) {
            Write-Host "Using cached $cached"
            Copy-Item $cached $pkg -Force
        } else {
            Write-Host "Downloading $url"
            Invoke-WebRequest -Uri $url -OutFile $pkg -UseBasicParsing
        }
    }
    if (-not (Test-Sha $pkg $p.Sha)) {
        Remove-Item $pkg -Force
        throw "SHA256 mismatch for $name (expected $($p.Sha))"
    }
    Write-Host "SHA256 OK: $name"

    if (Test-Path $dest) { Remove-Item $dest -Recurse -Force }
    New-Item -ItemType Directory -Force $dest | Out-Null
    # bsdtar (System32\tar.exe) reads the nupkg (zip) and refuses absolute and ".." member paths.
    & "$env:SystemRoot\System32\tar.exe" -xf $pkg -C $dest
    if ($LASTEXITCODE) { throw "extract failed: $pkg" }
    Write-Host "Installed to $dest"
}
