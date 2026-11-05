# P1 redo: inference validation of the self-exported models (docs/v2.0-P1重做報告.md).
# Runtime, exe and model folders are parameters; results go to -OutDir. One GPU job at a time.
#   -Stage rewrite : rewrite_pad.py on v3 (9) + swin_unet/art (14), three-way hash check
#   -Stage load    : session creation on DML + CPU for every model in use, node placement
#   -Stage correct : 6 representative models at tile 256 on DML + CPU vs PyTorch
#   -Stage tile    : v3 at 160/256/352/448 on DML vs PyTorch, VRAM peak
#   -Stage illegal : illegal tiles on DML + CPU: error or silent wrong output?
#   -Stage cancel  : SetTerminate() on DML
#   -Stage rawpad  : allowzero fix only (Pad not rewritten) on DML: is the Pad rewrite still needed?
#   -Stage azfix   : EXPERIMENT fix_reshape_allowzero.py on top of the rewritten models (models-az)
#   -Stage bitwise : original vs rewritten + allowzero fix, CPU, must be bit-identical
#   -Stage oldgraph: 20250502 rewritten 4x graphs on this runtime vs PyTorch + timing
#   -Stage image   : whole images through tile_runner vs the nunif CLI
[CmdletBinding()]
param(
    [ValidateSet('rewrite', 'load', 'correct', 'tile', 'illegal', 'cancel', 'rawpad', 'image', 'azfix', 'bitwise', 'oldgraph')]
    [string]$Stage = 'load',
    [string]$ModelsRoot = '',     # holds swin_unet\art and swin_unet_v3\art
    [string]$RuntimeDir = '',     # onnxruntime.dll + DirectML.dll; empty = exe folder
    [string]$ExpectOrt  = '1.24.4',
    [string]$OutDir = '',
    [string]$Exe = ''
)
$ErrorActionPreference = 'Stop'
$Root = $PSScriptRoot
$W    = Join-Path $Root '_work'
if (-not $OutDir)     { $OutDir = Join-Path $W 'p1redo' }
if (-not $ModelsRoot) { $ModelsRoot = Join-Path $OutDir 'models' }
if (-not $Exe)        { $Exe = Join-Path $Root 'build-upstream\winml_probe.exe' }
$Runner = Join-Path (Split-Path $Exe) 'tile_runner.exe'
$Py     = Join-Path $W 'venv\Scripts\python.exe'
$PyT    = Join-Path $W 'venv-torch\Scripts\python.exe'
$Nunif  = Join-Path $W 'upstream\nunif'
$Onnx   = Join-Path $Nunif 'waifu2x\onnx_models'
$Pth    = Join-Path $Nunif 'waifu2x\pretrained_models'
$Tools  = Join-Path $Root 'scripts\p1redo_tools.py'
$Ver    = Join-Path $Root 'scripts\verify_rewrite.py'
$Ref    = Join-Path $Root 'scripts\reference.py'
$TRef   = Join-Path $Root 'scripts\torch_ref.py'
$Fix    = Join-Path $Root 'scripts\fix_reshape_allowzero.py'
$Core   = Join-Path $W 'p1fix\t256\input_core.png'
$V3 = 'swin_unet_v3\art'; $Art = 'swin_unet\art'
$V3All  = 'noise0','noise1','noise2','noise3','noise0_scale2x','noise1_scale2x','noise2_scale2x','noise3_scale2x','scale2x'
$Art4x  = 'noise0_scale4x','noise1_scale4x','noise2_scale4x','noise3_scale4x','scale4x'
$ArtAll = 'noise0','noise1','noise2','noise3','noise0_scale2x','noise1_scale2x','noise2_scale2x','noise3_scale2x','scale2x' + $Art4x
$Correct = @(@($V3,'noise0'), @($V3,'noise3'), @($V3,'scale2x'), @($V3,'noise2_scale2x'), @($Art,'scale4x'), @($Art,'noise3_scale4x'))

$RtArgs = @('--expect-ort', $ExpectOrt)
if ($RuntimeDir) { $RtArgs += @('--runtime-dir', $RuntimeDir) }
function ScaleOf([string]$n) { if ($n -match 'scale4x') { 4 } elseif ($n -match 'scale2x') { 2 } else { 1 } }
function OffsetOf([string]$n) { 8 * (ScaleOf $n) }
function Probe([string]$Log, [string[]]$A, [switch]$AllowFail) {
    & $Exe @RtArgs @A *> $Log
    $c = $LASTEXITCODE
    # Normal inference must never hit a DXGI device error: stop the whole matrix and report.
    $dev = Select-String -Path $Log -Pattern '887A00(05|06|07|20)' -List
    if ($dev -and -not $AllowFail) { throw "DXGI device error ($Log): $($dev.Line.Trim())" }
    if ($c -and -not $AllowFail) { Get-Content $Log | Select-Object -Last 8; throw "probe exit $c ($Log)" }
    $c
}
function Prepare-Tile([int]$T) {
    $R = Join-Path $OutDir "t$T"
    New-Item -ItemType Directory -Force $R | Out-Null
    if (-not (Test-Path "$R\input_$T.bin")) {
        Copy-Item $Core $R
        & $Py -I $Ver make-input $R $T | Out-Host
        if ($LASTEXITCODE) { throw "make-input failed for $T" }
    }
    $R
}
function Torch-Ref([string]$R, [int]$T, [string]$Family, [string[]]$Names) {
    $need = @($Names | Where-Object { -not (Test-Path "$R\ref_$_.bin") })
    if (-not $need) { return }
    & $PyT -I $TRef $Nunif (Join-Path $Pth $Family) $R $T @need | Out-Host
    if ($LASTEXITCODE) { throw "torch reference failed" }
}

switch ($Stage) {
    'rewrite' {
        $rw = @(@($V3, $V3All), @($Art, $ArtAll))
        foreach ($f in $rw) { foreach ($n in $f[1]) {
            $d = Join-Path $ModelsRoot $f[0]
            & $Py -I (Join-Path $Root 'scripts\rewrite_pad.py') (Join-Path $Onnx "$($f[0])\$n.onnx") "$d\$n.onnx" --report "$d\$n.json" | Select-Object -Last 1
            if ($LASTEXITCODE) { throw "rewrite stopped on $($f[0])\$n (exit $LASTEXITCODE)" }
        } }
        & $Py -I $Tools hashes (Join-Path $OutDir 'hashes.json') `
            "$(Join-Path $ModelsRoot $V3)=$(Join-Path $Onnx $V3)" "$(Join-Path $ModelsRoot $Art)=$(Join-Path $Onnx $Art)"
        if ($LASTEXITCODE) { throw "hash check failed" }
    }
    'load' {
        $R = Join-Path $OutDir 'load'
        New-Item -ItemType Directory -Force $R | Out-Null
        $json = Join-Path $R 'load.jsonl'
        if (Test-Path $json) { Remove-Item $json }
        $logs = @()
        foreach ($f in @(@($V3, $V3All), @($Art, $Art4x))) { foreach ($n in $f[1]) {
            $tag = ($f[0] -replace '\\', '_') + "_$n"
            $m = Join-Path $ModelsRoot "$($f[0])\$n.onnx"
            $c1 = Probe "$R\${tag}_dml.txt" @('--model', $m, '--load-only', '--ep', 'dml', '--verbose', '--log', "$R\ort_${tag}_dml.log", '--json', $json) -AllowFail
            $c2 = Probe "$R\${tag}_cpu.txt" @('--model', $m, '--load-only', '--ep', 'cpu', '--json', $json) -AllowFail
            "$tag dml=$c1 cpu=$c2"
            $logs += "$R\ort_${tag}_dml.log"
        } }
        & $Py -I $Tools placement (Join-Path $R 'placement.json') @logs
    }
    'correct' {
        $R = Prepare-Tile 256
        foreach ($f in $Correct) {
            $n = $f[1]
            Torch-Ref $R 256 $f[0] @($n)
            foreach ($ep in 'dml', 'cpu') {
                "== $($f[0]) $n $ep"
                $null = Probe "$R\run_${n}_$ep.txt" @('--model', (Join-Path $ModelsRoot "$($f[0])\$n.onnx"), '--input', "$R\input_256.bin",
                    '--ep', $ep, '--tile', 256, '--scale', (ScaleOf $n), '--offset', (OffsetOf $n), '--warm', 3,
                    '--output', "$R\out_${n}_$ep.bin", '--json', "$R\run.jsonl")
            }
        }
        & $Py -I $Ref compare $R @($Correct | ForEach-Object { $_[1] })
    }
    'tile' {
        foreach ($t in 160, 256, 352, 448) {
            $R = Prepare-Tile $t
            $names = 'noise3', 'noise2_scale2x'
            Torch-Ref $R $t $V3 $names
            foreach ($n in $names) {
                "== v3 $n tile $t dml"
                $null = Probe "$R\tile_${n}_dml.txt" @('--model', (Join-Path $ModelsRoot "$V3\$n.onnx"), '--input', "$R\input_$t.bin",
                    '--ep', 'dml', '--tile', $t, '--scale', (ScaleOf $n), '--offset', (OffsetOf $n), '--warm', 3,
                    '--output', "$R\out_${n}_dml.bin", '--json', (Join-Path $OutDir 'tile.jsonl'))
            }
            & $Py -I $Ref compare $R @names
        }
    }
    'illegal' {
        foreach ($t in 400, 240) {
            $R = Prepare-Tile $t
            $names = 'noise3', 'noise2_scale2x'
            Torch-Ref $R $t $V3 $names
            foreach ($n in $names) { foreach ($ep in 'dml', 'cpu') {
                $c = Probe "$R\illegal_${n}_$ep.txt" @('--model', (Join-Path $ModelsRoot "$V3\$n.onnx"), '--input', "$R\input_$t.bin",
                    '--ep', $ep, '--tile', $t, '--scale', (ScaleOf $n), '--offset', (OffsetOf $n), '--warm', 0,
                    '--output', "$R\out_${n}_$ep.bin", '--json', (Join-Path $OutDir 'illegal.jsonl')) -AllowFail
                "== v3 $n tile $t $ep exit $c"
                Get-Content "$R\illegal_${n}_$ep.txt" | Select-String 'output shape|ERROR' | ForEach-Object { "   $($_.Line)" }
            } }
        }
    }
    'cancel' {
        $R = Prepare-Tile 256
        $big = Prepare-Tile 448
        $cases = @(@($V3, 'noise2_scale2x', 448, $big), @($Art, 'noise3_scale4x', 256, $R))
        foreach ($k in $cases) { foreach ($d in 50, 150) {
            $n = $k[1]; $t = $k[2]
            "== cancel $($k[0]) $n tile $t delay $d"
            $log = Join-Path $OutDir "cancel_${n}_$d.txt"
            $c = Probe $log @('--model', (Join-Path $ModelsRoot "$($k[0])\$n.onnx"), '--input', "$($k[3])\input_$t.bin", '--ep', 'dml',
                '--tile', $t, '--scale', (ScaleOf $n), '--offset', (OffsetOf $n), '--warm', 1, '--cancel-delay-ms', $d,
                '--json', (Join-Path $OutDir 'cancel.jsonl')) -AllowFail
            Get-Content $log | Select-String 'warm Run|cancel test|after cancel|ERROR' | ForEach-Object { "   $($_.Line)" }
            "   exit $c"
        } }
    }
    'rawpad' {
        $R = Prepare-Tile 256
        $sub = Join-Path $OutDir 'rawpad'
        New-Item -ItemType Directory -Force $sub | Out-Null
        foreach ($f in @(@($V3, 'noise3'), @($Art, 'noise3_scale4x'))) {
            $n = $f[1]
            $raw = "$sub\$n.onnx"
            & $Py -I $Fix (Join-Path $Onnx "$($f[0])\$n.onnx") $raw
            if ($LASTEXITCODE) { throw "allowzero fix failed on $n" }
            $c = Probe "$sub\raw_${n}_dml.txt" @('--model', $raw, '--input', "$R\input_256.bin", '--ep', 'dml',
                '--tile', 256, '--scale', (ScaleOf $n), '--offset', (OffsetOf $n), '--warm', 0,
                '--output', "$sub\out_${n}_dml.bin", '--json', "$sub\run.jsonl") -AllowFail
            "$n raw on DML exit $c"
            Copy-Item "$R\ref_$n.bin", "$R\ref_$n.shape.json" $sub
        }
        & $Py -I $Ref compare $sub noise3 noise3_scale4x
    }
    'azfix' {
        $src = Join-Path $OutDir 'models'
        $dst = Join-Path $OutDir 'models-az'
        foreach ($f in @(@($V3, $V3All), @($Art, $ArtAll))) { foreach ($n in $f[1]) {
            & $Py -I $Fix (Join-Path $src "$($f[0])\$n.onnx") (Join-Path $dst "$($f[0])\$n.onnx") --report (Join-Path $dst "$($f[0])\$n.json")
            if ($LASTEXITCODE) { throw "allowzero fix stopped on $n" }
        } }
    }
    'bitwise' {
        foreach ($f in @(@($V3, @('noise0', 'noise3', 'scale2x', 'noise2_scale2x')), @($Art, @('scale4x', 'noise3_scale4x')))) {
            $R = Join-Path $OutDir ("bitwise\" + ($f[0] -replace '\\', '_'))
            New-Item -ItemType Directory -Force $R | Out-Null
            Copy-Item (Join-Path $OutDir 't256\input_256.bin') $R
            & $Py -I $Ver cpu-bitwise $R 256 (Join-Path $Onnx $f[0]) (Join-Path $ModelsRoot $f[0]) @($f[1])
            if ($LASTEXITCODE) { throw "CPU bitwise check failed for $($f[0])" }
        }
    }
    'oldgraph' {
        $src = Join-Path $W 'models-rw\swin_unet\art'
        $t = Join-Path $OutDir 't256'
        $R = Join-Path $OutDir 'oldgraph'
        New-Item -ItemType Directory -Force $R | Out-Null
        foreach ($n in 'scale4x', 'noise3_scale4x') {
            Copy-Item (Join-Path $t "ref_$n.bin"), (Join-Path $t "ref_$n.shape.json") $R
            foreach ($ep in 'dml', 'cpu') {
                "== old $n $ep"
                $null = Probe (Join-Path $R "run_${n}_$ep.txt") @('--model', (Join-Path $src "$n.onnx"), '--input', (Join-Path $t 'input_256.bin'), '--ep', $ep,
                    '--tile', 256, '--scale', 4, '--offset', 32, '--warm', 3, '--output', (Join-Path $R "out_${n}_$ep.bin"), '--json', (Join-Path $R 'run.jsonl'))
            }
        }
        & $Py -I $Ref compare $R scale4x noise3_scale4x
    }
    'image' {
        $R = Join-Path $OutDir 'image'
        New-Item -ItemType Directory -Force $R | Out-Null
        $mf = Join-Path $R 'manifest-v3.json'
        & $Py -I $Tools manifest (Join-Path $Root '..\..\models\manifest.json') $mf $ModelsRoot 'swin_unet_v3/art'
        if ($LASTEXITCODE) { throw "manifest failed" }
        $In = Join-Path $W 'p2\inputs'
        $cli = @{ noise3 = @('-m', 'noise', '-n', '3'); noise2_scale2x = @('-m', 'noise_scale', '-n', '2'); scale2x = @('-m', 'scale') }
        $p2 = Join-Path $Root 'scripts\p2_tools.py'
        foreach ($n in $cli.Keys) {
            $s = ScaleOf $n; $o = OffsetOf $n
            $ref = Join-Path $R "ref\$n"
            New-Item -ItemType Directory -Force $ref | Out-Null
            foreach ($img in 's01_scene_640x480', '01_sakura_portrait') {
                if (-not (Test-Path "$ref\$img.png")) {
                    Push-Location $Nunif
                    try {
                        & $PyT -m waifu2x.cli --model-dir (Join-Path $Pth $V3) @($cli[$n]) --gpu -1 --tile-size 256 --batch-size 1 --disable-amp -i "$In\$img.png" -o $ref 2>&1 | Select-Object -Last 2
                        if ($LASTEXITCODE) { throw "nunif CLI failed: $n $img" }
                    } finally { Pop-Location }
                }
                $out = "$R\${n}_$img.arx"
                & $Runner --manifest $mf --models-root $ModelsRoot --model "swin_unet_v3/art/$n" --ep dml --tile 256 --input "$In\$img.arx" `
                    --output $out --json (Join-Path $R 'run.jsonl') --stride-pad 3 *> "$out.log"
                if ($LASTEXITCODE) { Get-Content "$out.log" | Select-Object -Last 5; throw "tile_runner failed: $n $img" }
                & $Py -I $p2 compare $out "$ref\$img.png" $s 256 $o (4 * $s) "$R\${n}_${img}_heat.png" "$n/$img" (Join-Path $R 'compare.jsonl')
            }
        }
    }
}
