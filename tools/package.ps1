# Builds AnimeRestoreX from a clean build folder, assembles a fresh staging folder from an
# explicit allowlist, checks it, writes MANIFEST.txt, zips it and writes SHA256SUMS.
#
#   pwsh -File tools/package.ps1 [-Suffix rc1] [-IncludeModels] [-SkipBuild]
#
# Nothing is taken from build\ or dist\: the release is configured and built in
# .package\build (deleted first), the staging folder is .package\stage (deleted first).
# Sources: the clean build (exe, THIRD_PARTY_NOTICES.md), the Qt 6.12.0 install (windeployqt),
# the VS 2026 VC redist folder, the pinned upstream runtime (third_party\onnxruntime\1.24.4,
# third_party\directml\1.15.4; models\manifest.json "runtime"), the hash-checked models
# (third_party\models, only with -IncludeModels) and files tracked in git (LICENSE, README.txt,
# models\manifest.json, licenses\). Only the upstream runtime flavor is packaged.
#
# Models: not included by default. -IncludeModels adds the 14 models; if a source in
# models\manifest.json says license UNRESOLVED, such a package is INTERNAL: it is
# written to tools\winml-probe\_work\p5\ (never dist\) and its name says so.
[CmdletBinding()]
param(
    [ValidatePattern('^$|^[a-z0-9]+$')]
    [string]$Suffix = '',
    [switch]$IncludeModels,
    # Reuse .package\build from the previous run of this script (never build\).
    [switch]$SkipBuild
)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$Root    = Split-Path -Parent $PSScriptRoot
$Qt      = 'D:\qt\6.12.0\msvc2022_64'
$VsRoot  = 'C:\Program Files\Microsoft Visual Studio\18\Community'
$VcVars  = '14.51'
$CMake   = 'C:\Program Files\CMake\bin\cmake.exe'
$CrtDir  = Join-Path $VsRoot 'VC\Redist\MSVC\14.51.36231\x64\Microsoft.VC145.CRT'
$CrtDlls = @('msvcp140.dll', 'msvcp140_1.dll', 'msvcp140_2.dll', 'vcruntime140.dll', 'vcruntime140_1.dll')
$ModelsSrc = Join-Path $Root 'third_party\models'
$QtSrc   = Join-Path $Root 'third_party\_download\qt-src'
$Work    = Join-Path $Root '.package'
$Build   = Join-Path $Work 'build'

# ---------------------------------------------------------------- version and names
$cmakeText = Get-Content (Join-Path $Root 'CMakeLists.txt') -Raw
$Version = [regex]::Match($cmakeText, 'project\(AnimeRestoreX VERSION ([0-9.]+)').Groups[1].Value
if (-not $Version) { throw 'Cannot read the project version from CMakeLists.txt' }

$ManifestPath = Join-Path $Root 'models\manifest.json'
$Manifest = Get-Content -Raw -Encoding utf8 $ManifestPath | ConvertFrom-Json
if ($Manifest.runtime.flavor -ne 'upstream') { throw 'models\manifest.json: only the upstream runtime is packaged' }
$ModelSources = @($Manifest.sources.PSObject.Properties.Value)
$ModelLicense = (@($ModelSources.license) | Sort-Object -Unique) -join ', '
$ModelLicenseResolved = $ModelLicense -and @($ModelSources | Where-Object license -eq 'UNRESOLVED').Count -eq 0 -and
    $Manifest.status -notlike 'DRAFT*'
$Internal = $IncludeModels -and -not $ModelLicenseResolved

$Name = "AnimeRestoreX-$Version" + $(if ($Suffix) { "-$Suffix" } else { '' }) + '-win64'
if ($IncludeModels) { $Name += '-with-models' }
if ($Internal)      { $Name += '-INTERNAL' }
$OutDir = if ($Internal) { Join-Path $Root 'tools\winml-probe\_work\p5' } else { Join-Path $Root 'dist' }
$StageRoot = Join-Path $Work 'stage'
$Stage = Join-Path $StageRoot $Name

Write-Host "Package : $Name"
Write-Host "Models  : $(if ($IncludeModels) { "included (licence: $ModelLicense)" } else { 'not included' })"
if ($Internal) {
    Write-Warning "Model licence is ${ModelLicense}: INTERNAL test package, written to $OutDir. Do not distribute."
}

function Get-Sha([string]$path) { (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }

# ---------------------------------------------------------------- clean build
if (-not $env:VSCMD_VER) {
    Import-Module (Join-Path $VsRoot 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
    Enter-VsDevShell -VsInstallPath $VsRoot -SkipAutomaticLocation `
        -DevCmdArguments "-arch=x64 -host_arch=x64 -vcvars_ver=$VcVars" | Out-Null
}
$Dumpbin = (Get-Command dumpbin.exe -ErrorAction Stop).Source

if (-not $SkipBuild) {
    & (Join-Path $Root 'tools\fetch-runtime.ps1')
    if ($LASTEXITCODE) { throw 'fetch-runtime failed' }
    & (Join-Path $Root 'tools\fetch-models.ps1')
    if ($LASTEXITCODE) { throw 'fetch-models failed' }
    if (Test-Path $Build) { Remove-Item -LiteralPath $Build -Recurse -Force }
    Push-Location $Root
    try {
        & $CMake --preset release -B $Build -DARX_BUILD_TESTS=OFF
        if ($LASTEXITCODE) { throw 'configure failed' }
        & $CMake --build $Build
        if ($LASTEXITCODE) { throw 'build failed' }
    } finally { Pop-Location }
} elseif (-not (Test-Path (Join-Path $Build 'AnimeRestoreX.exe'))) {
    throw "-SkipBuild: no previous clean build in $Build"
}
$cache = Get-Content (Join-Path $Build 'CMakeCache.txt') -Raw
if ($cache -notmatch '(?m)^CMAKE_BUILD_TYPE:STRING=Release\r?$') { throw "$Build is not a Release build" }
if ($cache -notmatch '(?m)^ARX_BUILD_TESTS:BOOL=OFF\r?$') { throw "$Build was configured with tests" }

# ---------------------------------------------------------------- fresh staging
if (Test-Path $StageRoot) { Remove-Item -LiteralPath $StageRoot -Recurse -Force }
New-Item -ItemType Directory -Force $Stage | Out-Null

$entries = New-Object System.Collections.Generic.List[object]
function Add-File([string]$src, [string]$rel, [string]$origin) {
    if (-not (Test-Path -LiteralPath $src -PathType Leaf)) { throw "Missing source file: $src" }
    $dest = Join-Path $Stage $rel
    New-Item -ItemType Directory -Force (Split-Path -Parent $dest) | Out-Null
    Copy-Item -LiteralPath $src -Destination $dest
    $script:entries.Add([pscustomobject]@{ Path = $rel; Origin = $origin; Source = $src })
}

# 1. Application (clean build)
Add-File (Join-Path $Build 'AnimeRestoreX.exe') 'AnimeRestoreX.exe' "built from this repository (clean Release build, MSVC $VcVars, Qt 6.12.0)"

# 2. Qt via windeployqt: Core/Gui/Widgets, qwindows, qjpeg, qwebp, zh_TW/ja.
$wdq = @(
    '--release', '--no-compiler-runtime', '--no-opengl-sw', '--no-system-d3d-compiler',
    '--no-system-dxc-compiler', '--no-ffmpeg',
    '--skip-plugin-types', 'generic,iconengines,networkinformation,tls,qmltooling,styles',
    '--exclude-plugins', 'qgif,qicns,qico,qpdf,qsvg,qtga,qtiff,qwbmp,qdirect2d,qminimal,qoffscreen',
    '--translations', 'zh_TW,ja',
    '--qtpaths', (Join-Path $Qt 'bin\qtpaths.exe'),
    '--list', 'mapping',
    (Join-Path $Stage 'AnimeRestoreX.exe'))
$mapping = & (Join-Path $Qt 'bin\windeployqt.exe') @wdq
if ($LASTEXITCODE) { throw 'windeployqt failed' }
foreach ($line in $mapping) {
    $m = [regex]::Match($line, '^"(.+)" "(.+)"$')
    if (-not $m.Success) { continue }
    $src = $m.Groups[1].Value; $rel = $m.Groups[2].Value
    if (-not (Test-Path (Join-Path $Stage $rel))) { continue }  # e.g. qtbase_*.qm merged into qt_*.qm
    $origin = "Qt 6.12.0 official binary: $($src.Replace($Qt + '\', ''))"
    $source = $src
    if ($rel -like 'translations\qt_*.qm') {
        $lang = [regex]::Match($rel, 'qt_(.+)\.qm').Groups[1].Value
        $origin = "windeployqt merge of Qt 6.12.0 translations\qtbase_$lang.qm"
        $source = $null  # generated by windeployqt, not a copy
    }
    $entries.Add([pscustomobject]@{ Path = $rel; Origin = $origin; Source = $source })
}

# 3. Visual C++ runtime (AnimeRestoreX.exe and the Qt DLLs import these), app-local
foreach ($dll in $CrtDlls) {
    Add-File (Join-Path $CrtDir $dll) $dll 'VS 2026 VC\Redist\MSVC\14.51.36231\x64\Microsoft.VC145.CRT'
}

# 4. Runtime: only the two DLLs the app loads (ONNX Runtime C API + DirectML).
foreach ($f in $Manifest.runtime.files) {
    Add-File (Join-Path $Root ($f.path -replace '/', '\')) $f.file "NuGet $($f.package) $($f.version) (unmodified)"
}

# 5. Models: manifest always (it documents the models and their licence state); model files only on request
Add-File $ManifestPath 'models\manifest.json' 'this repository (models\manifest.json)'
if ($IncludeModels) {
    foreach ($m in $Manifest.models) {
        $rel = 'models\' + ($m.file -replace '/', '\')
        $zipName = Split-Path $Manifest.sources.($m.source).url -Leaf
        Add-File (Join-Path $ModelsSrc ($m.file -replace '/', '\')) $rel "$zipName, processed by tools\fetch-models.ps1; licence $ModelLicense"
    }
}

# 6. Documents and licence texts
Add-File (Join-Path $Build 'THIRD_PARTY_NOTICES.md') 'THIRD_PARTY_NOTICES.md' 'this repository (THIRD_PARTY_NOTICES.md.in, version filled in by CMake)'
Add-File (Join-Path $Root 'LICENSE') 'LICENSE' 'this repository'
Add-File (Join-Path $Root 'packaging\README.txt') 'README.txt' 'this repository (packaging\README.txt)'
$tracked = @(& git -C $Root ls-files -- licenses)
if ($LASTEXITCODE -or $tracked.Count -eq 0) { throw 'git ls-files licenses failed' }
$untracked = @(& git -C $Root ls-files --others --exclude-standard -- licenses)
if ($untracked.Count) { throw "Untracked files in licenses\ (commit or remove them first): $($untracked -join ', ')" }
$dirty = @(& git -C $Root status --porcelain -- licenses LICENSE packaging models\manifest.json THIRD_PARTY_NOTICES.md.in)
if ($dirty.Count) { Write-Warning "Uncommitted changes in packaged sources: $($dirty -join '; ')" }
foreach ($f in $tracked) {
    $rel = $f -replace '/', '\'
    if ($rel -eq 'licenses\README.md') { $origin = 'this repository' } else { $origin = 'this repository (tools\collect-licenses.ps1)' }
    Add-File (Join-Path $Root $rel) $rel $origin
}

# ---------------------------------------------------------------- checks
$problems = New-Object System.Collections.Generic.List[string]
$all = @(Get-ChildItem -LiteralPath $Stage -Recurse -File)
$relOf = { param($fi) $fi.FullName.Substring($Stage.Length + 1) }

# a) Allowlist: exactly these files, nothing else.
$allow = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($p in @('AnimeRestoreX.exe', 'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll',
                 'platforms\qwindows.dll', 'imageformats\qjpeg.dll', 'imageformats\qwebp.dll',
                 'translations\qt_zh_TW.qm', 'translations\qt_ja.qm',
                 'onnxruntime.dll', 'DirectML.dll', 'models\manifest.json',
                 'THIRD_PARTY_NOTICES.md', 'LICENSE', 'README.txt') + $CrtDlls) { [void]$allow.Add($p) }
foreach ($f in $tracked) { [void]$allow.Add(($f -replace '/', '\')) }
if ($IncludeModels) { foreach ($m in $Manifest.models) { [void]$allow.Add('models\' + ($m.file -replace '/', '\')) } }
$present = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($fi in $all) {
    $rel = & $relOf $fi
    [void]$present.Add($rel)
    if (-not $allow.Contains($rel)) { $problems.Add("not in allowlist: $rel") }
}
foreach ($p in $allow) { if (-not $present.Contains($p)) { $problems.Add("allowlisted file missing: $p") } }
$listed = [System.Collections.Generic.HashSet[string]]::new([string[]]$entries.Path, [StringComparer]::OrdinalIgnoreCase)
foreach ($p in $present) { if (-not $listed.Contains($p)) { $problems.Add("no recorded origin: $p") } }

# b) Old engine and unwanted files, by name (defence in depth).
$forbiddenName = '(?i)(waifu2x|ncnn|cunet|vcomp1\d\d|\.param$|\.bin$|Microsoft\.Windows\.AI|WinML|onnxruntime_providers|opengl32sw|d3dcompiler|dxcompiler|dxil|Qt6Svg|Qt6Network|vulkan-1)'
foreach ($fi in $all) {
    $rel = & $relOf $fi
    if ($rel -match $forbiddenName -and $rel -notlike 'licenses\*') { $problems.Add("forbidden file: $rel") }
    if ($rel -like 'licenses\WindowsML*') { $problems.Add("Windows ML licence left in the package: $rel") }
}
if (-not $IncludeModels -and ($all | Where-Object Extension -eq '.onnx')) { $problems.Add('model files present without -IncludeModels') }
# ...and by content of our own text files (licence texts are upstream copies and skipped).
foreach ($p in 'README.txt', 'THIRD_PARTY_NOTICES.md', 'models\manifest.json') {
    $t = Get-Content -LiteralPath (Join-Path $Stage $p) -Raw -Encoding utf8
    foreach ($word in 'ncnn', 'models-cunet', 'vcomp140', 'Vulkan GPU', 'Windows ML', 'EULA') {
        if ($t.Contains($word)) { $problems.Add("$p mentions '$word'") }
    }
}

# c) Copies are byte-identical to their sources; runtime DLLs and models match models\manifest.json.
foreach ($e in $entries) {
    if (-not $e.Source) { continue }
    if ((Get-Sha (Join-Path $Stage $e.Path)) -ne (Get-Sha $e.Source)) { $problems.Add("differs from its source: $($e.Path)") }
}
foreach ($f in $Manifest.runtime.files) {
    $p = Join-Path $Stage $f.file
    $ver = (Get-Item -LiteralPath $p).VersionInfo
    $fileVer = '{0}.{1}.{2}' -f $ver.ProductMajorPart, $ver.ProductMinorPart, $ver.ProductBuildPart
    if ($fileVer -ne $f.version) { $problems.Add("$($f.file): version $fileVer, manifest says $($f.version)") }
    if ((Get-Sha $p) -ne $f.sha256) { $problems.Add("$($f.file): SHA-256 differs from models\manifest.json") }
}
if ($IncludeModels) {
    foreach ($m in $Manifest.models) {
        $p = Join-Path $Stage ('models\' + ($m.file -replace '/', '\'))
        if ((Get-Sha $p) -ne $m.sha256) { $problems.Add("$($m.id): SHA-256 differs from models\manifest.json") }
    }
}
if ($ModelLicense -match 'UNRESOLVED' -and $Manifest.status -notlike '*licence unresolved*') {
    $problems.Add('models\manifest.json: license UNRESOLVED but status does not say so')
}
# The notices the DLLs need: the licence terms of the runtime and their third-party notices.
foreach ($p in 'licenses\ONNXRuntime\LICENSE', 'licenses\ONNXRuntime\ThirdPartyNotices.txt',
               'licenses\DirectML\LICENSE.txt', 'licenses\DirectML\ThirdPartyNotices.txt',
               'licenses\nunif\LICENSE', 'licenses\nunif\NOTICE') {
    if (-not (Test-Path -LiteralPath (Join-Path $Stage $p))) { $problems.Add("licence text missing: $p") }
}

# d) Version and generated documents.
$notices = Get-Content -LiteralPath (Join-Path $Stage 'THIRD_PARTY_NOTICES.md') -Raw -Encoding utf8
if ($notices -notmatch "(?m)^# AnimeRestoreX $([regex]::Escape($Version)) " -or $notices.Contains('@PROJECT_VERSION@')) {
    $problems.Add("THIRD_PARTY_NOTICES.md does not carry version $Version")
}

# e) Dependencies: every import of every shipped binary is shipped or provided by Windows.
$shipped = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($fi in $all | Where-Object Extension -in '.exe', '.dll') { [void]$shipped.Add($fi.Name) }
$depReport = New-Object System.Collections.Generic.List[string]
foreach ($fi in $all | Where-Object Extension -in '.exe', '.dll') {
    $deps = & $Dumpbin /nologo /dependents $fi.FullName | Where-Object { $_ -match '^\s+(\S+\.dll)\s*$' } | ForEach-Object { $Matches[1] }
    foreach ($d in $deps) {
        if ($shipped.Contains($d)) { continue }
        if ($d -match '^(api|ext)-ms-win-') { continue }
        if (Test-Path (Join-Path $env:SystemRoot "System32\$d")) {
            if ($d -match '(?i)^(msvcp|vcruntime|concrt|vcomp|Qt6|onnxruntime|DirectML)') {
                $problems.Add("$(& $relOf $fi) imports $d, which would come from System32") }
            continue
        }
        $problems.Add("$(& $relOf $fi) imports $d, which is neither shipped nor in System32")
    }
    $depReport.Add("$(& $relOf $fi): $($deps -join ', ')")
}

if ($problems.Count) {
    $problems | ForEach-Object { Write-Host "  PROBLEM: $_" -ForegroundColor Red }
    throw "Package checks failed ($($problems.Count) problems); nothing was zipped."
}
Write-Host "Checks  : allowlist, forbidden files, source hashes, runtime/model hashes, versions, licence texts, dependencies: OK"

# ---------------------------------------------------------------- MANIFEST.txt
$lines = New-Object System.Collections.Generic.List[string]
$lines.Add("AnimeRestoreX $Version$(if ($Suffix) { " ($Suffix)" }) - file manifest$(if ($Internal) { ' - INTERNAL, NOT FOR DISTRIBUTION (model licence unresolved)' })")
$lines.Add('SHA256  size  path  <- origin')
$lines.Add('')
foreach ($e in $entries | Sort-Object Path) {
    $p = Join-Path $Stage $e.Path
    $lines.Add(('{0}  {1,10}  {2}  <- {3}' -f (Get-Sha $p), (Get-Item -LiteralPath $p).Length, $e.Path, $e.Origin))
}
[IO.File]::WriteAllLines((Join-Path $Stage 'MANIFEST.txt'), $lines, (New-Object Text.UTF8Encoding($false)))

# ---------------------------------------------------------------- zip and verify it
New-Item -ItemType Directory -Force $OutDir | Out-Null
$zip = Join-Path $OutDir "$Name.zip"
if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
Compress-Archive -LiteralPath $Stage -DestinationPath $zip -CompressionLevel Optimal

$verify = Join-Path $Work 'verify'
if (Test-Path $verify) { Remove-Item -LiteralPath $verify -Recurse -Force }
Expand-Archive -LiteralPath $zip -DestinationPath $verify
$vRoot = Join-Path $verify $Name
$stageFiles = @(Get-ChildItem -LiteralPath $Stage -Recurse -File)
$zipFiles = @(Get-ChildItem -LiteralPath $vRoot -Recurse -File)
if ($zipFiles.Count -ne $stageFiles.Count) { throw "zip holds $($zipFiles.Count) files, staging $($stageFiles.Count)" }
foreach ($fi in $stageFiles) {
    $rel = & $relOf $fi
    $z = Join-Path $vRoot $rel
    if (-not (Test-Path -LiteralPath $z) -or (Get-Sha $z) -ne (Get-Sha $fi.FullName)) { throw "zip content differs: $rel" }
}
Remove-Item -LiteralPath $verify -Recurse -Force
Write-Host "Zip     : every file re-extracted and hash-compared with the staging folder: OK"

# ---------------------------------------------------------------- Qt sources and SHA256SUMS
$qtSources = [ordered]@{
    'qtbase-everywhere-src-6.12.0.zip'          = '0529901575c413e34ded504eeda177c4a6dfd0dff1e39ed52c811dc5204b43c7'
    'qtimageformats-everywhere-src-6.12.0.zip'  = '664de2ba60a1e74105da844c061688ff5e6f8f046d5dd98e2d6b6ad3c553b4ca'
    'qttranslations-everywhere-src-6.12.0.zip'  = '286f0ca3ad669bc2d6fec0e209a797a08049d037c7a167e60eb3b4a640405aa5'
}
& (Join-Path $Root 'tools\fetch-qt-sources.ps1')
foreach ($q in $qtSources.Keys) {
    $src = Join-Path $QtSrc $q
    if ((Get-Sha $src) -ne $qtSources[$q]) { throw "$src does not match THIRD_PARTY_NOTICES.md" }
    $dest = Join-Path $OutDir $q
    if (-not (Test-Path -LiteralPath $dest) -or (Get-Sha $dest) -ne $qtSources[$q]) { Copy-Item -LiteralPath $src -Destination $dest -Force }
}
$sumsName = "SHA256SUMS-$Name.txt"
$sums = foreach ($f in @("$Name.zip") + [string[]]$qtSources.Keys) { '{0}  {1}' -f (Get-Sha (Join-Path $OutDir $f)), $f }
[IO.File]::WriteAllLines((Join-Path $OutDir $sumsName), [string[]]$sums, (New-Object Text.UTF8Encoding($false)))
[IO.File]::WriteAllLines((Join-Path $Work "dependencies-$Name.txt"), $depReport, (New-Object Text.UTF8Encoding($false)))

Write-Host ''
Write-Host "Package : $zip ($([math]::Round((Get-Item $zip).Length / 1MB, 1)) MB)"
Write-Host "Files   : $($entries.Count + 1) (incl. MANIFEST.txt)"
Write-Host "Sums    : $(Join-Path $OutDir $sumsName)"
if ($Internal) { Write-Warning 'INTERNAL package (model licence unresolved): do not distribute.' }
else { Write-Host "Publish together: $Name.zip, $($qtSources.Keys -join ', '), $sumsName" }
