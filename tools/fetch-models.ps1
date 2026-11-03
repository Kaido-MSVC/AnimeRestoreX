# Produce the inference models listed in models\manifest.json into third_party\models.
#
#   default : download the pinned nunif ONNX zip (or take it from -Cache), check its SHA-256,
#             extract swin_unet/art, check every source model hash, rewrite negative Pad -> Slice
#             with tools\winml-probe\scripts\rewrite_pad.py (its hash must match the manifest),
#             then check every rewritten model hash.
#   -FromDir: copy already rewritten models (e.g. tools\winml-probe\_work\models-rw) and check
#             every hash against the manifest.
# Any mismatch stops the script; nothing is installed into third_party\models unless every
# model passed (staging folder, then a single move). The script never packages anything.
[CmdletBinding()]
param(
    [string]$FromDir = '',
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

if ((Test-Path $Dest) -and -not $Force) {
    $ok = $true
    foreach ($m in $Manifest.models) {
        $p = Join-Path $Dest (Get-SafeRelative $m.file)
        if (-not (Test-Path $p) -or (Get-Sha $p) -ne $m.sha256) { $ok = $false; break }
    }
    if ($ok) { Write-Host "third_party\models already matches the manifest (use -Force to rebuild)."; exit 0 }
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

    $script = Join-Path $Root ($Manifest.rewrite_tool.script -replace '/', '\')
    Assert-Sha $script $Manifest.rewrite_tool.script_sha256 'rewrite script'

    if (-not $Python) {
        $venv = Join-Path $Work 'venv-models'
        $Python = Join-Path $venv 'Scripts\python.exe'
        if (-not (Test-Path $Python)) {
            & py -3.13 -m venv $venv
            if ($LASTEXITCODE) { throw "venv creation failed (needs Python 3.13 via the py launcher)" }
            & $Python -m pip install --disable-pip-version-check -r (Join-Path $PSScriptRoot 'models-requirements.txt')
            if ($LASTEXITCODE) { throw "pip install failed" }
        }
    }

    foreach ($m in $Manifest.models) {
        $rel = Get-SafeRelative $m.file
        $name = Split-Path $rel -Leaf
        $orig = Join-Path $extract (Join-Path ($member -replace '/', '\') $name)
        Assert-Sha $orig $m.source_sha256 "$($m.id) (source)"
        $out = Join-Path $Stage $rel
        New-Item -ItemType Directory -Force (Split-Path $out) | Out-Null
        & $Python -I $script $orig $out
        if ($LASTEXITCODE) { throw "rewrite failed for $($m.id) (exit $LASTEXITCODE)" }
        Assert-Sha $out $m.sha256 "$($m.id) (rewritten)"
        Write-Host "OK $($m.id)"
    }
    Remove-Item $extract -Recurse -Force
}

if (Test-Path $Dest) { Remove-Item $Dest -Recurse -Force }
Move-Item $Stage $Dest
Write-Host "$(@($Manifest.models).Count) models verified and installed to $Dest"
