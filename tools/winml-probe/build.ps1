# Build winml_probe + tile_runner (Release) in a VS 2026 x64 developer environment with the
# same MSVC toolset as the main project (14.52). Changes only this process.
#   -Runtime upstream : ORT 1.24.4 + DirectML 1.15.4, output in build-upstream (default)
#   -Runtime winml    : Windows ML 2.5.96 (ORT 1.28.3), output in build
[CmdletBinding()]
param(
    [ValidateSet('upstream', 'winml')]
    [string]$Runtime = 'upstream',
    [switch]$Clean
)
$ErrorActionPreference = 'Stop'

$VsRoot = 'C:\Program Files\Microsoft Visual Studio\18\Community'
$VcVars = '14.52'
$CMake  = 'C:\Program Files\CMake\bin\cmake.exe'
$Ninja  = 'C:\Users\max00\AppData\Local\Microsoft\WinGet\Packages\Ninja-build.Ninja_Microsoft.Winget.Source_8wekyb3d8bbwe\ninja.exe'
$Root   = $PSScriptRoot
$Build  = Join-Path $Root ($Runtime -eq 'upstream' ? 'build-upstream' : 'build')

$Pkg = Join-Path $Root ($Runtime -eq 'upstream' ? '_work\pkg\ort-dml-1.24.4\build\native' : '_work\pkg\2.5.96\build\cmake')
if (-not (Test-Path $Pkg)) {
    throw "$Runtime runtime package not extracted; run fetch-deps.ps1 first"
}
if (-not $env:VSCMD_VER) {
    Import-Module (Join-Path $VsRoot 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
    Enter-VsDevShell -VsInstallPath $VsRoot -SkipAutomaticLocation `
        -DevCmdArguments "-arch=x64 -host_arch=x64 -vcvars_ver=$VcVars" | Out-Null
}
Write-Host "MSVC tools : $env:VCToolsVersion"
Write-Host "Windows SDK: $env:WindowsSDKVersion"
Write-Host "Runtime    : $Runtime -> $Build"

if ($Clean -and (Test-Path $Build)) { Remove-Item $Build -Recurse -Force }
& $CMake -S $Root -B $Build -G Ninja "-DCMAKE_MAKE_PROGRAM=$Ninja" -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=cl "-DARX_ORT_FLAVOR=$Runtime"
if ($LASTEXITCODE) { throw "configure failed" }
& $CMake --build $Build
if ($LASTEXITCODE) { throw "build failed" }
