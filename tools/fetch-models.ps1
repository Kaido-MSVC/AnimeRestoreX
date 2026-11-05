# Produce the inference models listed in models\manifest.json into third_party\models.
#
#   default : download the pinned nunif ONNX zip (or take it from -Cache), check its SHA-256,
#             extract swin_unet/art and run every model through the pipeline of the manifest:
#             rewrite_pad.py, then fix_reshape_allowzero.py, always in that order.
#             Three-way check: source model, both script hashes, result of each step.
#   -FromDir: copy already processed models (e.g. tools\winml-probe\_work\models-rw) and check
#             every hash against the manifest.
#   -V3Dir  : also run the "v3_pending" models through the same pipeline from a folder holding
#             the nunif exports (swin_unet_v3\art). Without it those models are SKIPPED and the
#             script says so: their Release has no download URL yet.
# Any mismatch stops the script; nothing is installed into third_party\models unless every
# model passed (staging folder, then a single move). The script never packages anything.
[CmdletBinding()]
param(
    [string]$FromDir = '',
    [string]$V3Dir = '',
    # Folder that may already hold the nunif zip; used only if its SHA-256 matches.
    [string]$Cache = '',
    # Python 3.13 with the packages of tools\models-requirements.txt. Without it a private
    # venv is created in third_party\_download\venv-models.
    [string]$Python = '',
    [switch]$Force
)
$ErrorActionPreference = 'Stop'

$Root     = Split-Path -Parent $PSScriptRoot
$Manifest = Get-Content -Raw -Encoding utf8 (Join-Path $Root 'models\manifest.json') | ConvertFrom-Json
$Work     = Join-Path $Root 'third_party\_download'
$Dest     = Join-Path $Root 'third_party\models'
$Stage    = Join-Path $Work 'models-staging'

function Get-Sha([string]$Path) { (Get-FileHash $Path -Algorithm SHA256).Hash.ToLowerInvariant() }

function Assert-Sha([string]$Path, [string]$Expected, [string]$What) {
    if (-not (Test-Path $Path)) { throw "$What missing: $Path" }
    $h = Get-Sha $Path
    if ($h -ne $Expected.ToLowerInvariant()) { throw "SHA256 mismatch for $What`n expected $Expected`n actual   $h" }
}

# Manifest file entries are relative, '/' separated, and must stay below the models root.
function Get-SafeRelative([string]$File) {
    if ($File -match '(^[\\/])|(:)|(^|[\\/])\.\.([\\/]|$)') { throw "unsafe model path in manifest: $File" }
    return $File -replace '/', '\'
}

# Pipeline order is fixed; a manifest that lists anything else is refused.
function Get-PipelineScripts {
    $steps = @($Manifest.pipeline.steps)
    if (($steps.name -join ',') -ne 'rewrite_pad,fix_reshape_allowzero') {
        throw "unexpected pipeline in manifest: $($steps.name -join ', ')"
    }
    foreach ($s in $steps) {
        $p = Join-Path $Root ($s.script -replace '/', '\')
        Assert-Sha $p $s.script_sha256 "$($s.name) script"
        $p
    }
}

function Get-PipelinePython {
    if ($Python) { return $Python }
    $venv = Join-Path $Work 'venv-models'
    $py = Join-Path $venv 'Scripts\python.exe'
    if (-not (Test-Path $py)) {
        # Out-Host: tool output must not end up in this function's return value.
        & py -3.13 -m venv $venv | Out-Host
        if ($LASTEXITCODE) { throw "venv creation failed (needs Python 3.13 via the py launcher)" }
        & $py -m pip install --disable-pip-version-check -r (Join-Path $PSScriptRoot 'models-requirements.txt') | Out-Host
        if ($LASTEXITCODE) { throw "pip install failed" }
    }
    return $py
}

function Invoke-Pipeline([string]$Py, [string[]]$Scripts, [string]$Orig, [string]$Out, $M) {
    Assert-Sha $Orig $M.source_sha256 "$($M.id) (source)"
    New-Item -ItemType Directory -Force (Split-Path $Out) | Out-Null
    $mid = "$Out.pad.onnx"
    & $Py -I $Scripts[0] $Orig $mid
    if ($LASTEXITCODE) { throw "rewrite_pad failed for $($M.id) (exit $LASTEXITCODE)" }
    Assert-Sha $mid $M.pad_sha256 "$($M.id) (after rewrite_pad)"
    & $Py -I $Scripts[1] $mid $Out
    if ($LASTEXITCODE) { throw "fix_reshape_allowzero failed for $($M.id) (exit $LASTEXITCODE)" }
    Assert-Sha $Out $M.sha256 "$($M.id) (final)"
    Remove-Item $mid
    Write-Host "OK $($M.id)"
}

function Write-V3Skipped {
    Write-Warning ("SKIPPED: $(@($Manifest.v3_pending.models).Count) swin_unet_v3 models, no download " +
        "source yet (v3_pending.source.url is empty). Use -V3Dir <nunif exports> to process them.")
}

if ((Test-Path $Dest) -and -not $Force -and -not $V3Dir) {
    $ok = $true
    foreach ($m in $Manifest.models) {
        $p = Join-Path $Dest (Get-SafeRelative $m.file)
        if (-not (Test-Path $p) -or (Get-Sha $p) -ne $m.sha256) { $ok = $false; break }
    }
    if ($ok) {
        Write-Host "third_party\models already matches the manifest (use -Force to rebuild)."
        Write-V3Skipped
        exit 0
    }
}

New-Item -ItemType Directory -Force $Work | Out-Null
if (Test-Path $Stage) { Remove-Item $Stage -Recurse -Force }
New-Item -ItemType Directory -Force $Stage | Out-Null

if ($FromDir) {
    foreach ($m in $Manifest.models) {
        $rel = Get-SafeRelative $m.file
        $src = Join-Path $FromDir $rel
        Assert-Sha $src $m.sha256 $m.id
        New-Item -ItemType Directory -Force (Split-Path (Join-Path $Stage $rel)) | Out-Null
        Copy-Item $src (Join-Path $Stage $rel)
    }
} else {
    $src = $Manifest.source
    $zipName = Split-Path $src.url -Leaf
    $zip = Join-Path $Work $zipName
    if (-not ((Test-Path $zip) -and (Get-Sha $zip) -eq $src.zip_sha256)) {
        $cached = if ($Cache) { Join-Path $Cache $zipName } else { '' }
        if ($cached -and (Test-Path $cached) -and (Get-Sha $cached) -eq $src.zip_sha256) {
            Write-Host "Using cached $cached"
            Copy-Item $cached $zip -Force
        } else {
            Write-Host "Downloading $($src.url)"
            Invoke-WebRequest -Uri $src.url -OutFile $zip -UseBasicParsing
        }
    }
    Assert-Sha $zip $src.zip_sha256 $zipName
    Write-Host "SHA256 OK: $zipName"

    $extract = Join-Path $Work 'nunif-onnx'
    if (Test-Path $extract) { Remove-Item $extract -Recurse -Force }
    New-Item -ItemType Directory -Force $extract | Out-Null
    $member = $src.path_in_zip.TrimEnd('/')
    & "$env:SystemRoot\System32\tar.exe" -xf $zip -C $extract $member
    if ($LASTEXITCODE) { throw "extract failed: $zip ($member)" }

    $scripts = @(Get-PipelineScripts)
    $py = Get-PipelinePython
    foreach ($m in $Manifest.models) {
        $rel = Get-SafeRelative $m.file
        $name = Split-Path $rel -Leaf
        $orig = Join-Path $extract (Join-Path ($member -replace '/', '\') $name)
        Invoke-Pipeline $py $scripts $orig (Join-Path $Stage $rel) $m
    }
    Remove-Item $extract -Recurse -Force
}

$count = @($Manifest.models).Count
if ($V3Dir) {
    $scripts = @(Get-PipelineScripts)
    $py = Get-PipelinePython
    foreach ($m in $Manifest.v3_pending.models) {
        $rel = Get-SafeRelative $m.file
        Invoke-Pipeline $py $scripts (Join-Path $V3Dir (Split-Path $rel -Leaf)) (Join-Path $Stage $rel) $m
        $count++
    }
} else {
    Write-V3Skipped
}

if (Test-Path $Dest) { Remove-Item $Dest -Recurse -Force }
Move-Item $Stage $Dest
Write-Host "$count models verified and installed to $Dest"
