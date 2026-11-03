# P1 fix verification for the Pad->Slice rewritten models (dev only).
# Rewritten models: _work\rewritten (scripts\rewrite_pad.py). Results: _work\p1fix.
#   -Stage rewrite : rewrite all 14 swin_unet/art models + per-model JSON report
#   -Stage run     : 14 models x {dml,cpu} at tile 256 (cold + 3 warm), then compare
#   -Stage nodes   : node-by-node DML vs CPU check for every rewritten model (tile 256)
#   -Stage speed   : 3 representative models x tiles 256/400/640 on DML (one process each)
#                    + DML output check against the Python CPU reference at each tile
[CmdletBinding()]
param(
    [ValidateSet('rewrite', 'run', 'nodes', 'speed')]
    [string]$Stage = 'run',
    [string[]]$Only = @(),
    [string]$Tiles = '256,400,640',  # string: pwsh -File does not split arrays
    [int]$Repeat = 1
)
$ErrorActionPreference = 'Stop'
$Root = $PSScriptRoot
$Exe  = Join-Path $Root 'build\winml_probe.exe'
$W    = Join-Path $Root '_work'
$Orig = Join-Path $W 'models\onnx_models\swin_unet\art'
$Rw   = Join-Path $W 'rewritten'
$Out  = Join-Path $W 'p1fix'
$Py   = Join-Path $W 'venv\Scripts\python.exe'
$Ver  = Join-Path $Root 'scripts\verify_rewrite.py'
$All = 'noise0','noise1','noise2','noise3','noise0_scale2x','noise1_scale2x','noise2_scale2x','noise3_scale2x',
       'scale2x','noise0_scale4x','noise1_scale4x','noise2_scale4x','noise3_scale4x','scale4x'
$Models = if ($Only) { $Only -split ',' | Where-Object { $_ } } else { $All }  # -File passes "a,b" as one string
function ScaleOf([string]$n) { if ($n -match 'scale4x') { 4 } elseif ($n -match 'scale2x') { 2 } else { 1 } }
function OffsetOf([string]$n) { 8 * (ScaleOf $n) }
function Probe([string]$Log, [string[]]$A) {
    & $Exe @A *> $Log
    $c = $LASTEXITCODE
    if ($c) { Get-Content $Log | ForEach-Object { "  $_" } }
    "  exit=$c"
    # Normal inference must never hit a DXGI device error: stop the whole matrix and report.
    $dev = Select-String -Path $Log -Pattern '887A00(05|06|07|20)' -List
    if ($dev) { throw "DXGI device error in normal inference ($Log): $($dev.Line.Trim())" }
}

switch ($Stage) {
    'rewrite' {
        foreach ($n in $Models) {
            & $Py -I (Join-Path $Root 'scripts\rewrite_pad.py') "$Orig\$n.onnx" "$Rw\$n.onnx" --report "$Rw\$n.json"
            if ($LASTEXITCODE) { throw "rewrite stopped on $n (exit $LASTEXITCODE)" }
        }
    }
    'run' {
        $R = Join-Path $Out 't256'
        foreach ($n in $Models) { foreach ($ep in 'dml', 'cpu') {
            "== $n $ep"
            Probe "$R\run_${n}_$ep.txt" @('--model', "$Rw\$n.onnx", '--input', "$R\input_256.bin", '--ep', $ep,
                '--scale', (ScaleOf $n), '--offset', (OffsetOf $n), '--warm', 3,
                '--output', "$R\out_${n}_$ep.bin", '--json', "$R\run.jsonl")
        } }
        & $Py -I $Ver compare $R @Models
    }
    'nodes' {
        $R = Join-Path $Out 'nodes'
        foreach ($n in $Models) {
            "== nodes $n"
            & $Py -I $Ver nodes $Exe "$Rw\$n.onnx" (Join-Path $Out 't256\input_256.bin') 256 $R 64
            if ($LASTEXITCODE) { throw "node check failed on $n" }
        }
    }
    'speed' {
        foreach ($t in ($Tiles -split ',' | Where-Object { $_ } | ForEach-Object { [int]$_ })) {
            $R = Join-Path $Out "t$t"
            New-Item -ItemType Directory -Force $R | Out-Null
            if (-not (Test-Path "$R\input_$t.bin")) {
                Copy-Item (Join-Path $Out 't256\input_core.png') $R
                & $Py -I $Ver make-input $R $t
                & $Py -I $Ver cpu-bitwise $R $t $Orig $Rw @Models | Out-Host
            }
            foreach ($n in $Models) { for ($i = 1; $i -le $Repeat; $i++) {
                "== speed $n tile $t dml #$i"
                Probe "$R\speed_${n}_dml_$i.txt" @('--model', "$Rw\$n.onnx", '--input', "$R\input_$t.bin", '--ep', 'dml',
                    '--tile', $t, '--scale', (ScaleOf $n), '--offset', (OffsetOf $n), '--warm', 5,
                    '--output', "$R\out_${n}_dml.bin", '--json', (Join-Path $Out 'speed.jsonl'))
            } }
            & $Py -I $Ver compare $R @Models
        }
    }
}
