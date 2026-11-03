# Build winml_probe (Release) in a VS 2026 x64 developer environment with the
# same MSVC toolset as the main project (14.51). Changes only this process.
[CmdletBinding()]
param([switch]$Clean)
$ErrorActionPreference = 'Stop'

$VsRoot = 'C:\Program Files\Microsoft Visual Studio\18\Community'
$VcVars = '14.51'
$CMake  = 'C:\Program Files\CMake\bin\cmake.exe'
$Ninja  = 'C:\Users\max00\AppData\Local\Microsoft\WinGet\Packages\Ninja-build.Ninja_Microsoft.Winget.Source_8wekyb3d8bbwe\ninja.exe'
$Root   = $PSScriptRoot
$Build  = Join-Path $Root 'build'

if (-not (Test-Path (Join-Path $Root '_work\pkg\2.5.96\build\cmake'))) {
    throw "Windows ML package not extracted; run fetch-deps.ps1 first"
}
if (-not $env:VSCMD_VER) {
    Import-Module (Join-Path $VsRoot 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
    Enter-VsDevShell -VsInstallPath $VsRoot -SkipAutomaticLocation `
        -DevCmdArguments "-arch=x64 -host_arch=x64 -vcvars_ver=$VcVars" | Out-Null
}
Write-Host "MSVC tools : $env:VCToolsVersion"
Write-Host "Windows SDK: $env:WindowsSDKVersion"

if ($Clean -and (Test-Path $Build)) { Remove-Item $Build -Recurse -Force }
& $CMake -S $Root -B $Build -G Ninja "-DCMAKE_MAKE_PROGRAM=$Ninja" -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=cl
if ($LASTEXITCODE) { throw "configure failed" }
& $CMake --build $Build
if ($LASTEXITCODE) { throw "build failed" }
