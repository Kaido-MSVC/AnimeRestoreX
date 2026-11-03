# P2 validation driver (dev only). Results go to _work\p2. GPU work runs one item at a time.
#   -Stage inputs    : copy synthetic + real test images into _work\p2\inputs and convert to ARX8
#   -Stage reference : nunif CLI (torch CPU, pinned commit, 20250502 swin_unet/art) per model, tile 256
#   -Stage run       : tile_runner (DML, tile 256) per model and image, compare with the reference
#   -Stage seams     : tiles 160/256/400 and a single-tile run (images small enough) compared to each other
#   -Stage tile160   : winml_probe single-tile timing / VRAM at tile 160 (3 models, 2 processes each)
#   -Stage full      : noise3_scale4x on a 2600x1730 synthetic image, tile 256, once
#   -Stage cancel    : full-size run cancelled mid-way (DML), and a CPU cancel on a smaller image
[CmdletBinding()]
param(
    [ValidateSet('inputs', 'reference', 'run', 'seams', 'tile160', 'full', 'cancel')]
    [string]$Stage = 'run',
    [string]$Only = '',
    [int]$CancelAfterMs = 4000
)
$ErrorActionPreference = 'Stop'
$Root   = $PSScriptRoot
$W      = Join-Path $Root '_work'
$P2     = Join-Path $W 'p2'
$In     = Join-Path $P2 'inputs'
$Py     = Join-Path $W 'venv\Scripts\python.exe'
$PyT    = Join-Path $W 'venv-torch\Scripts\python.exe'
$Tools  = Join-Path $Root 'scripts\p2_tools.py'
$Runner = Join-Path $Root 'build\tile_runner.exe'
$Probe  = Join-Path $Root 'build\winml_probe.exe'
$Manifest = (Resolve-Path (Join-Path $Root '..\..\models\manifest.json')).Path
$ModelsRoot = Join-Path $W 'models-rw'
$Nunif  = Join-Path $W 'upstream\nunif'
$ArtDir = Join-Path $W 'nunif-home\waifu2x\pretrained_models\swin_unet\art'
$Models = @{
    'noise3'         = @{ Cli = @('-m', 'noise', '-n', '3');         Scale = 1; Offset = 8;  Blend = 4 }
    'scale2x'        = @{ Cli = @('-m', 'scale');                     Scale = 2; Offset = 16; Blend = 8 }
    'noise3_scale4x' = @{ Cli = @('-m', 'noise_scale4x', '-n', '3'); Scale = 4; Offset = 32; Blend = 16 }
}
$Names = if ($Only) { $Only -split ',' | Where-Object { $_ } } else { 'noise3', 'scale2x', 'noise3_scale4x' }

function Run-Runner([string]$Model, [string]$Image, [int]$Tile, [string]$Out, [string]$Json, [string[]]$Extra = @()) {
    $log = "$Out.log"
    & $Runner --manifest $Manifest --models-root $ModelsRoot --model "swin_unet/art/$Model" --ep dml --tile $Tile `
        --input "$In\$Image.arx" --output $Out --json $Json --stride-pad 3 @Extra *> $log
    $code = $LASTEXITCODE
    # Normal inference must never see a DXGI device error: stop everything.
    if (Select-String -Path $log -Pattern '887A00(05|06|07|20)' -List) { throw "DXGI device error: $log" }
    if ($code -ne 0) { Get-Content $log | Select-Object -Last 3; throw "tile_runner exit $code ($Model $Image $Tile)" }
}

switch ($Stage) {
    'inputs' {
        New-Item -ItemType Directory -Force $In | Out-Null
        Copy-Item (Join-Path $P2 'images\*.png') $In
        Copy-Item (Join-Path $W 'test-images\*.png') $In
        Get-ChildItem "$In\*.png" | ForEach-Object { & $Py -I $Tools to-arx $_.FullName ($_.FullName -replace '\.png$', '.arx') }
        Get-ChildItem "$In\*.arx" | Measure-Object | ForEach-Object { "$($_.Count) ARX8 inputs" }
    }
    'reference' {
        $env:NUNIF_HOME = Join-Path $W 'nunif-home'
        foreach ($n in $Names) {
            $out = Join-Path $P2 "ref\$n"
            New-Item -ItemType Directory -Force $out | Out-Null
            $cliArgs = @('-m', 'waifu2x.cli', '--model-dir', $ArtDir) + $Models[$n].Cli +
                       @('--gpu', '-1', '--tile-size', '256', '--batch-size', '1', '--disable-amp', '-i', $In, '-o', $out)
            "== reference $n : python $($cliArgs -join ' ')"
            Push-Location $Nunif
            try { & $PyT @cliArgs 2>&1 | Select-Object -Last 2 } finally { Pop-Location }
            if ($LASTEXITCODE) { throw "nunif CLI failed for $n" }
        }
    }
    'run' {
        foreach ($n in $Names) {
            $m = $Models[$n]
            $out = Join-Path $P2 "ours\$n"
            New-Item -ItemType Directory -Force $out | Out-Null
            foreach ($img in (Get-ChildItem "$In\*.arx").BaseName) {
                Run-Runner $n $img 256 "$out\$img.arx" (Join-Path $P2 'run.jsonl')
                & $Py -I $Tools compare "$out\$img.arx" (Join-Path $P2 "ref\$n\$img.png") $m.Scale 256 $m.Offset $m.Blend `
                    "$out\${img}_heat.png" "$n/$img" (Join-Path $P2 'compare.jsonl')
            }
        }
    }
    'seams' {
        $imgs = 's07_seam_300x300', 's02_odd_257x131', 's05_tiny_37x23', 's01_scene_640x480', '01_sakura_portrait'
        foreach ($n in $Names) {
            $m = $Models[$n]
            $out = Join-Path $P2 "seams\$n"
            New-Item -ItemType Directory -Force $out | Out-Null
            foreach ($img in $imgs) {
                foreach ($t in 160, 400) {
                    Run-Runner $n $img $t "$out\${img}_t$t.arx" (Join-Path $P2 'seams.jsonl') @('--manual')
                    & $Py -I $Tools compare "$out\${img}_t$t.arx" (Join-Path $P2 "ours\$n\$img.arx") $m.Scale $t $m.Offset $m.Blend `
                        "$out\${img}_t${t}_vs256_heat.png" "$n/$img t$t vs t256" (Join-Path $P2 'seams_compare.jsonl')
                }
            }
            # Single Run (no tiling): the smallest legal T with h_blocks = w_blocks = 1, for small images only.
            foreach ($pair in @(@('s07_seam_300x300', 352), @('s02_odd_257x131', 304), @('s05_tiny_37x23', 64))) {
                $img, $t = $pair
                Run-Runner $n $img $t "$out\${img}_single.arx" (Join-Path $P2 'seams.jsonl') @('--manual')
                foreach ($cmp in 160, 256, 400) {
                    $o = if ($cmp -eq 256) { Join-Path $P2 "ours\$n\$img.arx" } else { "$out\${img}_t$cmp.arx" }
                    & $Py -I $Tools compare $o "$out\${img}_single.arx" $m.Scale $cmp $m.Offset $m.Blend `
                        "$out\${img}_t${cmp}_vs_single_heat.png" "$n/$img t$cmp vs single T$t" (Join-Path $P2 'seams_compare.jsonl')
                }
            }
        }
    }
    'tile160' {
        $R = Join-Path $P2 'tile160'
        New-Item -ItemType Directory -Force $R | Out-Null
        & $Py -I (Join-Path $Root 'scripts\verify_rewrite.py') make-input $R 160 | Out-Null
        foreach ($n in $Names) { foreach ($i in 1, 2) {
            $m = $Models[$n]
            $log = "$R\speed_${n}_$i.txt"
            & $Probe --model "$ModelsRoot\swin_unet\art\$n.onnx" --input "$R\input_160.bin" --ep dml --tile 160 `
                --scale $m.Scale --offset $m.Offset --warm 5 --json "$R\speed160.jsonl" *> $log
            if (Select-String -Path $log -Pattern '887A00(05|06|07|20)' -List) { throw "DXGI device error: $log" }
            "$n #$i exit=$LASTEXITCODE"
        } }
    }
    'full' {
        $img = 's99_full_2600x1730'
        if (-not (Test-Path "$In\$img.arx")) {
            & $Py -I $Tools full "$In\$img.png" 2600 1730
            & $Py -I $Tools to-arx "$In\$img.png" "$In\$img.arx"
        }
        $out = Join-Path $P2 'full'
        New-Item -ItemType Directory -Force $out | Out-Null
        Run-Runner 'noise3_scale4x' $img 256 "$out\$img.arx" (Join-Path $P2 'full.jsonl') @('--progress-log', "$out\progress.txt")
        Get-Content (Join-Path $P2 'full.jsonl') | Select-Object -Last 1
    }
    'cancel' {
        $img = 's99_full_2600x1730'
        $out = Join-Path $P2 'cancel'
        New-Item -ItemType Directory -Force $out | Out-Null
        Remove-Item "$out\*.arx" -ErrorAction SilentlyContinue
        $log = "$out\cancel_dml.log"
        & $Runner --manifest $Manifest --models-root $ModelsRoot --model 'swin_unet/art/noise3_scale4x' --ep dml --tile 256 `
            --input "$In\$img.arx" --output "$out\cancel_dml.arx" --json (Join-Path $P2 'cancel.jsonl') `
            --cancel-after-ms $CancelAfterMs --progress-log "$out\progress_dml.txt" *> $log
        "dml exit=$LASTEXITCODE (5 = cancelled)"
        if (Select-String -Path $log -Pattern '887A00(05|06|07|20)' -List) { throw "DXGI device error: $log" }
        $log = "$out\cancel_cpu.log"
        & $Runner --manifest $Manifest --models-root $ModelsRoot --model 'swin_unet/art/noise3_scale4x' --ep cpu --tile 256 `
            --input "$In\s01_scene_640x480.arx" --output "$out\cancel_cpu.arx" --json (Join-Path $P2 'cancel.jsonl') `
            --cancel-after-ms 2500 *> $log
        "cpu exit=$LASTEXITCODE (5 = cancelled)"
        Get-Content (Join-Path $P2 'cancel.jsonl') | Select-Object -Last 2
    }
}
