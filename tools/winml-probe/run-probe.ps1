# Runs the P1 probe matrix and the Python comparison. Results go to _work\run.
#   -Stage timing    : 3 models x {dml,cpu}, cold + 5 warm runs, output files
#   -Stage placement : DML runs with verbose log (node placement) + profiling
#   -Stage cancel    : DML (and CPU) SetTerminate() test, delays given in ms
#   -Stage compare   : Python reference comparison (needs ref_*.bin)
[CmdletBinding()]
param(
    [ValidateSet('timing', 'placement', 'cancel', 'compare')]
    [string]$Stage = 'timing',
    [int[]]$CancelDelayMs = @(50, 150),
    [string[]]$CancelEp = @('dml')
)
$ErrorActionPreference = 'Stop'
$Root  = $PSScriptRoot
$Exe   = Join-Path $Root 'build\winml_probe.exe'
$W     = Join-Path $Root '_work'
$ModelDir = Join-Path $W 'models\onnx_models\swin_unet\art'
$R     = Join-Path $W 'run'
$Py    = Join-Path $W 'venv\Scripts\python.exe'
$Ref   = Join-Path $Root 'scripts\reference.py'
$Models = @(
    @{ Name = 'scale2x';        Scale = 2; Offset = 16 },
    @{ Name = 'noise3_scale4x'; Scale = 4; Offset = 32 },
    @{ Name = 'noise3';         Scale = 1; Offset = 8 }
)

function Invoke-Probe([string]$Tag, [string[]]$ProbeArgs) {
    $log = Join-Path $R "$Tag.txt"
    & $Exe @ProbeArgs *> $log
    $code = $LASTEXITCODE
    Get-Content $log | ForEach-Object { "  $_" }
    "  exit=$code"
}

switch ($Stage) {
    'timing' {
        foreach ($mdl in $Models) { foreach ($ep in 'dml', 'cpu') {
            "== $($mdl.Name) $ep"
            Invoke-Probe "timing_$($mdl.Name)_$ep" @('--model', "$ModelDir\$($mdl.Name).onnx", '--input', "$R\input_256.bin",
                '--ep', $ep, '--scale', $mdl.Scale, '--offset', $mdl.Offset, '--warm', 5,
                '--output', "$R\out_$($mdl.Name)_$ep.bin", '--json', "$R\timing.jsonl")
        } }
    }
    'placement' {
        foreach ($mdl in $Models) {
            "== $($mdl.Name) dml (verbose + profile)"
            Invoke-Probe "placement_$($mdl.Name)_dml" @('--model', "$ModelDir\$($mdl.Name).onnx", '--input', "$R\input_256.bin",
                '--ep', 'dml', '--scale', $mdl.Scale, '--offset', $mdl.Offset, '--warm', 0, '--verbose',
                '--log', "$R\ortlog_$($mdl.Name)_dml.txt", '--profile', "$R\profile_$($mdl.Name)_dml",
                '--json', "$R\placement.jsonl")
        }
    }
    'cancel' {
        $mdl = $Models[1]  # noise3_scale4x: the longest single Run
        foreach ($ep in $CancelEp) { foreach ($d in $CancelDelayMs) {
            "== cancel $($mdl.Name) $ep delay $d ms"
            Invoke-Probe "cancel_$($mdl.Name)_${ep}_$d" @('--model', "$ModelDir\$($mdl.Name).onnx", '--input', "$R\input_256.bin",
                '--ep', $ep, '--scale', $mdl.Scale, '--offset', $mdl.Offset, '--warm', 1,
                '--cancel-delay-ms', $d, '--json', "$R\cancel.jsonl")
        } }
    }
    'compare' {
        & $Py -I $Ref compare $R @($Models | ForEach-Object Name)
    }
}
