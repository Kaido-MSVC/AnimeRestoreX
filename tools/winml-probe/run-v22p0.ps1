# v2.2 P0: are the model rewrites still needed on the Windows ML runtime (ORT 1.28.3)?
# (docs/v2.2-WinML-P0-P1報告.md) Three versions of the graphs the app uses, at tile 256,
# DML (and CPU) vs the PyTorch reference of _work\p1redo\t256:
#   raw   : the original export (v3: nunif 0d3d92c; 4x: 20250502 zip)
#   pad   : rewrite_pad.py only
#   padaz : rewrite_pad.py + fix_reshape_allowzero.py (what the app ships)
# The other P0 checks reuse run-p1redo.ps1 with -Exe build\winml_probe.exe (see the report).
[CmdletBinding()]
param(
    [string]$RuntimeDir = '',     # default: third_party\windowsml\2.5.96\runtimes\win-x64\native
    [string]$ExpectOrt  = '1.28.3',
    [string]$OutDir = '',
    [string]$Exe = ''
)
$ErrorActionPreference = 'Stop'
$Root = $PSScriptRoot
$W    = Join-Path $Root '_work'
if (-not $RuntimeDir) { $RuntimeDir = (Resolve-Path (Join-Path $Root '..\..\third_party\windowsml\2.5.96\runtimes\win-x64\native')).Path }
if (-not $OutDir) { $OutDir = Join-Path $W 'v22p0\variants' }
if (-not $Exe)    { $Exe = Join-Path $Root 'build\winml_probe.exe' }
$Py   = Join-Path $W 'venv\Scripts\python.exe'
$Ref  = Join-Path $Root 'scripts\reference.py'
$Fix  = Join-Path $Root 'scripts\fix_reshape_allowzero.py'
$T256 = Join-Path $W 'p1redo\t256'

$families = @(
    @{ Name = 'v3'; Models = @('noise3', 'noise2_scale2x', 'scale2x'); Dirs = [ordered]@{
        raw = Join-Path $W 'upstream\nunif\waifu2x\onnx_models\swin_unet_v3\art'
        pad = Join-Path $W 'p1redo\models\swin_unet_v3\art'
        padaz = Join-Path $W 'p1redo\models-az\swin_unet_v3\art' } },
    @{ Name = 'old4x'; Models = @('scale4x', 'noise3_scale4x'); Dirs = [ordered]@{
        raw = Join-Path $W 'models\onnx_models\swin_unet\art'
        pad = Join-Path $W 'models-rw\swin_unet\art'
        padaz = Join-Path $OutDir 'old4x-padaz-models' } }
)
function ScaleOf([string]$n) { if ($n -match 'scale4x') { 4 } elseif ($n -match 'scale2x') { 2 } else { 1 } }

# The allowzero step on the rewritten 20250502 graphs (expected to be a no-op).
$az = $families[1].Dirs.padaz
New-Item -ItemType Directory -Force $az | Out-Null
foreach ($n in $families[1].Models) {
    & $Py -I $Fix (Join-Path $families[1].Dirs.pad "$n.onnx") (Join-Path $az "$n.onnx") | Out-Null
    if ($LASTEXITCODE) { throw "allowzero fix failed on old $n" }
    $a = (Get-FileHash (Join-Path $families[1].Dirs.pad "$n.onnx")).Hash
    $b = (Get-FileHash (Join-Path $az "$n.onnx")).Hash
    "old4x $n allowzero step: $(if ($a -eq $b) { 'no-op (same file)' } else { 'CHANGED' })"
}

foreach ($f in $families) {
    foreach ($v in $f.Dirs.Keys) {
        $R = Join-Path $OutDir "$($f.Name)-$v"
        New-Item -ItemType Directory -Force $R | Out-Null
        Copy-Item (Join-Path $T256 'input_256.bin') $R
        foreach ($n in $f.Models) {
            Copy-Item (Join-Path $T256 "ref_$n.bin"), (Join-Path $T256 "ref_$n.shape.json") $R
            $s = ScaleOf $n
            $eps = if ($v -eq 'raw') { 'dml', 'cpu' } else { 'dml' }
            foreach ($ep in $eps) {
                $log = Join-Path $R "run_${n}_$ep.txt"
                & $Exe --expect-ort $ExpectOrt --runtime-dir $RuntimeDir --model (Join-Path $f.Dirs[$v] "$n.onnx") `
                    --input (Join-Path $R 'input_256.bin') --ep $ep --tile 256 --scale $s --offset (8 * $s) --warm 1 `
                    --output (Join-Path $R "out_${n}_$ep.bin") --json (Join-Path $R 'run.jsonl') *> $log
                $c = $LASTEXITCODE
                $err = Select-String -Path $log -Pattern 'ERROR|error' -List
                "$($f.Name) $v $n $ep exit $c$(if ($c -and $err) { ': ' + $err.Line.Trim() })"
                if ($c) { Remove-Item (Join-Path $R "out_${n}_$ep.bin") -ErrorAction SilentlyContinue }
            }
        }
        & $Py -I $Ref compare $R @($f.Models)
    }
}
