# Build AnimeRestoreX inside a VS 2026 x64 developer environment (MSVC 14.51).
# The environment change only affects this PowerShell process.
[CmdletBinding()]
param(
    [ValidateSet('debug', 'release')]
    [string]$Preset = 'release',
    [switch]$Test,
    [switch]$Clean
)
$ErrorActionPreference = 'Stop'

$VsRoot   = 'C:\Program Files\Microsoft Visual Studio\18\Community'
$VcVars   = '14.51'
$CMake    = 'C:\Program Files\CMake\bin\cmake.exe'
$CTest    = 'C:\Program Files\CMake\bin\ctest.exe'
$Root     = $PSScriptRoot

if (-not (Test-Path (Join-Path $Root 'third_party\waifu2x\waifu2x-ncnn-vulkan.exe'))) {
    & (Join-Path $Root 'tools\fetch-waifu2x.ps1')
}

if (-not $env:VSCMD_VER) {
    Import-Module (Join-Path $VsRoot 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
    Enter-VsDevShell -VsInstallPath $VsRoot -SkipAutomaticLocation `
        -DevCmdArguments "-arch=x64 -host_arch=x64 -vcvars_ver=$VcVars" | Out-Null
}
Write-Host "MSVC tools : $env:VCToolsVersion"
Write-Host "Windows SDK: $env:WindowsSDKVersion"

Push-Location $Root
try {
    if ($Clean -and (Test-Path "build\$Preset")) { Remove-Item "build\$Preset" -Recurse -Force }
    & $CMake --preset $Preset
    if ($LASTEXITCODE) { throw "configure failed" }
    & $CMake --build --preset $Preset
    if ($LASTEXITCODE) { throw "build failed" }
    if ($Test) {
        & $CTest --preset $Preset
        if ($LASTEXITCODE) { throw "tests failed" }
    }
} finally {
    Pop-Location
}
