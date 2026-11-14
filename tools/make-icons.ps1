# Regenerates resources/icons/app.ico and app_<size>.png from app.svg.
# Developer-only: builds tools/icongen (needs Qt Svg) in build/icongen.
# The generated files are committed; normal builds never need this script.
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'

$Root   = Split-Path -Parent $PSScriptRoot
$VsRoot = 'C:\Program Files\Microsoft Visual Studio\18\Community'
$Qt     = 'D:\qt\6.12.0\msvc2022_64'
$Ninja  = 'C:\Users\max00\AppData\Local\Microsoft\WinGet\Packages\Ninja-build.Ninja_Microsoft.Winget.Source_8wekyb3d8bbwe\ninja.exe'
$CMake  = 'C:\Program Files\CMake\bin\cmake.exe'
$Build  = Join-Path $Root 'build\icongen'
$Icons  = Join-Path $Root 'resources\icons'

if (-not $env:VSCMD_VER) {
    Import-Module (Join-Path $VsRoot 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
    Enter-VsDevShell -VsInstallPath $VsRoot -SkipAutomaticLocation `
        -DevCmdArguments '-arch=x64 -host_arch=x64 -vcvars_ver=14.52' | Out-Null
}
& $CMake -S (Join-Path $Root 'tools\icongen') -B $Build -G Ninja `
    "-DCMAKE_MAKE_PROGRAM=$Ninja" "-DCMAKE_PREFIX_PATH=$Qt" -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=cl | Out-Null
if ($LASTEXITCODE) { throw 'configure failed' }
& $CMake --build $Build | Out-Null
if ($LASTEXITCODE) { throw 'build failed' }

$env:PATH = "$Qt\bin;$env:PATH"   # this process only
& (Join-Path $Build 'icongen.exe') (Join-Path $Icons 'app.svg') $Icons
if ($LASTEXITCODE) { throw 'icongen failed' }
