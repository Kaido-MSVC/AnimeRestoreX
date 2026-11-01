#requires -Version 7.2
<#
.SYNOPSIS
Developer-only A9 benchmark; never copy into the runtime package.
.EXAMPLE
pwsh -File tools/measure-a9.ps1 -Images D:\don\1.jpeg,D:\don\2.jpeg
.NOTES
Uses the installed waifu2x directly, not the GUI/Qt conversion pipeline.
2x/4x-direct/4x-two-stage x tile 0/200, plus noise 0..3 x 2x (tile 0).
Both 2x stages use the same noise, matching 20250915's internal -s 4 loop.
No warmup/repetition; compare images yourself before choosing defaults.
GPU memory is WHOLE-CARD MiB, not per-process attribution. Baseline subtraction
is only an estimate and can include unrelated desktop/GPU activity.
PeakWorkingSet64 is the OS lifetime high-water mark sampled every 200 ms.
The upstream CLI uses threads, not child workers: RAM is the launched engine
PID only. Never sum stage peaks. Timeout/cancellation kills its process tree.
Ctrl+C runs finally cleanup; forcibly terminating PowerShell bypasses finally.
#>
[CmdletBinding()]
param(
    [string[]]$Images = @('D:\don\1.jpeg', 'D:\don\2.jpeg'),
    [string]$AppDirectory = (Join-Path (Split-Path $PSScriptRoot -Parent) 'build\release'),
    [string]$OutputRoot = (Join-Path (Split-Path $PSScriptRoot -Parent) 'measurements'),
    # Optional exact IDs for rerunning only selected cases; default runs all.
    [string[]]$CaseIds = @(),
    [ValidateRange(0,3)][int]$Noise = 1,
    [ValidateRange(1,240)][int]$TimeoutMinutes = 30
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if (-not $IsWindows) { throw 'Windows / PowerShell 7.2+ required.' }
Add-Type -AssemblyName System.Drawing
if (-not ('A9GpuPump' -as [type])) {
    Add-Type -TypeDefinition @'
using System.Diagnostics;
using System.IO;
using System.Threading.Tasks;
public static class A9GpuPump {
    public static async Task Save(Process p, string path) {
        using (var writer = new StreamWriter(path)) {
            writer.AutoFlush = true;
            string line;
            while ((line = await p.StandardOutput.ReadLineAsync().ConfigureAwait(false)) != null)
                await writer.WriteLineAsync(line).ConfigureAwait(false);
        }
    }
}
'@
}
$engine = Join-Path $AppDirectory 'waifu2x\waifu2x-ncnn-vulkan.exe'
$models = Join-Path $AppDirectory 'waifu2x\models-cunet'
if (-not (Test-Path -LiteralPath $engine -PathType Leaf)) { throw "Engine missing: $engine. Set -AppDirectory explicitly; no silent fallback." }
foreach ($n in 0..3) {
    foreach ($ext in 'param','bin') {
        $p = Join-Path $models "noise${n}_scale2.0x_model.$ext"
        if (-not (Test-Path -LiteralPath $p)) { throw "Model missing: $p" }
    }
}
if ((Join-Path $models 'noise0_scale2.0x_model.param').Length -ge 256) { throw 'Model path exceeds the pinned engine limit.' }
foreach ($image in $Images) {
    if (-not (Test-Path -LiteralPath $image -PathType Leaf)) { throw "Input missing: $image" }
}
$run = Join-Path ([IO.Path]::GetFullPath($OutputRoot)) ('a9-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0,6))
New-Item -ItemType Directory -Path $run -Force | Out-Null
$rows = [Collections.Generic.List[object]]::new()
$smi = Get-Command nvidia-smi -ErrorAction SilentlyContinue

function New-ProcessInfo([string]$file, [string[]]$arguments) {
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $file
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    foreach ($arg in $arguments) { $info.ArgumentList.Add($arg) }
    return $info
}
function Stop-OwnedProcess($process, $completion = $null) {
    if ($null -ne $process) {
        try {
            if (-not $process.HasExited) {
                $process.Kill($true)
                if (-not $process.WaitForExit(10000)) { throw "Could not stop owned PID $($process.Id)" }
            }
            if ($null -ne $completion) { $null = $completion.GetAwaiter().GetResult() }
        } finally {
            $process.Dispose()
        }
    }
}
function Read-Size([string]$path) {
    $img = [Drawing.Image]::FromFile($path)
    try { return @($img.Width, $img.Height) } finally { $img.Dispose() }
}
function Read-Gpu([string]$path) {
    if (Test-Path -LiteralPath $path) {
        foreach ($line in [IO.File]::ReadAllLines($path)) {
            $v = @($line.Split(',') | ForEach-Object { $_.Trim() })
            $value = 0.0
            if ($v.Count -eq 3 -and [double]::TryParse($v[2], [Globalization.NumberStyles]::Float, [Globalization.CultureInfo]::InvariantCulture, [ref]$value)) {
                $date = [datetime]::MinValue
                if ([datetime]::TryParseExact($v[0], 'yyyy/MM/dd HH:mm:ss.fff', [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::None, [ref]$date)) {
                    [pscustomobject]@{Time=$date; Gpu=$v[1]; MiB=$value}
                }
            }
        }
    }
}
function Invoke-Stage([string]$inputFile, [string]$outputFile, [int]$scale, [int]$tile, [int]$denoise, [string]$prefix) {
    $argsList = @('-i',$inputFile,'-o',$outputFile,'-n',"$denoise",'-s',"$scale",'-t',"$tile",'-m',$models,'-f','png')
    $argsList | ConvertTo-Json | Set-Content -LiteralPath "$prefix.arguments.json" -Encoding utf8
    $info = New-ProcessInfo $engine $argsList
    $info.WorkingDirectory = Split-Path $engine -Parent
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $proc = [Diagnostics.Process]::new()
    $proc.StartInfo = $info
    $writer = [IO.StreamWriter]::new("$prefix.ram.csv")
    $writer.WriteLine('ElapsedSeconds,PID,PeakWorkingSetBytes')
    $watch = [Diagnostics.Stopwatch]::new()
    $peak = 0L; $samples = 0; $exitCode = $null; $failure = ''; $started = $false
    try {
        $watch.Start()
        $started = $proc.Start()
        $stdout = $proc.StandardOutput.ReadToEndAsync()
        $stderr = $proc.StandardError.ReadToEndAsync()
        do {
            $proc.Refresh()
            try {
                $current = $proc.PeakWorkingSet64
                $peak = [math]::Max($peak, $current)
                $writer.WriteLine(('{0},{1},{2}' -f $watch.Elapsed.TotalSeconds.ToString('F6',[Globalization.CultureInfo]::InvariantCulture),$proc.Id,$current))
                $samples++
            } catch { if (-not $proc.HasExited) { throw } }
            if ($watch.Elapsed.TotalMinutes -gt $TimeoutMinutes) { throw "Stage timeout ($TimeoutMinutes minutes)" }
            $next = ([math]::Floor($watch.Elapsed.TotalMilliseconds / 200) + 1) * 200
        } while (-not $proc.WaitForExit([math]::Max(1,[int]($next - $watch.Elapsed.TotalMilliseconds))))
        $watch.Stop()
        $proc.Refresh()
        try { $peak = [math]::Max($peak,$proc.PeakWorkingSet64) } catch { }
        $exitCode = $proc.ExitCode
        if ($exitCode -ne 0) { throw "Engine exit code $exitCode" }
        if (-not (Test-Path -LiteralPath $outputFile -PathType Leaf)) { throw 'Engine returned success without an output image.' }
        $before = Read-Size $inputFile
        $after = Read-Size $outputFile
        if ($after[0] -ne $before[0]*$scale -or $after[1] -ne $before[1]*$scale) { throw "Wrong output dimensions: $after" }
    } catch { $failure = $_.Exception.Message }
    finally {
        $watch.Stop()
        if ($started) {
            if (-not $proc.HasExited) { $proc.Kill($true); $null = $proc.WaitForExit(10000) }
            $stdout.GetAwaiter().GetResult() | Set-Content -LiteralPath "$prefix.stdout.txt" -Encoding utf8
            $stderr.GetAwaiter().GetResult() | Set-Content -LiteralPath "$prefix.stderr.txt" -Encoding utf8
        }
        $writer.Dispose()
        if ($started) { Stop-OwnedProcess $proc } else { $proc.Dispose() }
    }
    [pscustomobject]@{Seconds=$watch.Elapsed.TotalSeconds; PeakBytes=$peak; Samples=$samples; ExitCode=$exitCode; Error=$failure}
}

$meta = [ordered]@{ Engine=$engine; EngineSha256=(Get-FileHash -LiteralPath $engine).Hash; PowerShell="$($PSVersionTable.PSVersion)"; Noise=$Noise; SampleMs=200; AppDirectory=$AppDirectory; Started=(Get-Date).ToString('o'); Images=@($Images | ForEach-Object { @{Path=[IO.Path]::GetFullPath($_); Sha256=(Get-FileHash -LiteralPath $_).Hash} }) }
$meta | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $run 'environment.json') -Encoding utf8
$sourceIndex = 0
try {
    foreach ($image in $Images) {
        $sourceIndex++
        $image = [IO.Path]::GetFullPath($image)
        $sourceSize = Read-Size $image
        $cases = @(
            foreach ($tile in 0,200) {
                foreach ($mode in '2x','4x-direct','4x-two-stage') { [pscustomobject]@{Mode=$mode; Tile=$tile; Noise=$Noise; Group='scale'} }
            }
            foreach ($n in 0..3) { [pscustomobject]@{Mode='2x'; Tile=0; Noise=$n; Group='denoise'} }
        )
        foreach ($case in $cases) {
            $id = '{0}-{1}-{2}-tile{3}-noise{4}' -f $sourceIndex,$case.Group,$case.Mode,$case.Tile,$case.Noise
            if ($CaseIds.Count -gt 0 -and $id -notin $CaseIds) { continue }
            Write-Host "Starting $id ($image)"
            $dir = Join-Path $run $id
            New-Item -ItemType Directory -Path $dir | Out-Null
            $gpu = $null; $gpuPump = $null; $gpuError = ''; $gpuExit = $null; $stages = @(); $failure = ''
            $gpuFile = Join-Path $dir 'gpu-raw.csv'
            $caseWatch = [Diagnostics.Stopwatch]::new()
            $begin = Get-Date
            try {
                if ($null -ne $smi) {
                    try {
                        $gi = New-ProcessInfo $smi.Source @('--query-gpu=timestamp,uuid,memory.used','--format=csv,noheader,nounits','-lms','200')
                        $gi.RedirectStandardOutput = $true
                        $gpu = [Diagnostics.Process]::Start($gi)
                        $gpuPump = [A9GpuPump]::Save($gpu, $gpuFile)
                        Start-Sleep -Milliseconds 650
                        if ($gpu.HasExited) { throw "nvidia-smi exited $($gpu.ExitCode)" }
                    } catch { $gpuError = $_.Exception.Message }
                } else { $gpuError = 'nvidia-smi unavailable; GPU fields are blank, never zero.' }
                $begin = Get-Date
                $caseWatch.Start()
                $count = if ($case.Mode -eq '4x-two-stage') { 2 } else { 1 }
                $inputStage = $image
                for ($stage=1; $stage -le $count; $stage++) {
                    $out = Join-Path $dir "stage$stage.png"
                    $scale = if ($case.Mode -eq '4x-direct') { 4 } else { 2 }
                    $result = Invoke-Stage $inputStage $out $scale $case.Tile $case.Noise (Join-Path $dir "stage$stage")
                    $stages += $result
                    if ($result.Error) { throw "Stage ${stage}: $($result.Error)" }
                    $inputStage = $out
                }
            } catch { $failure = $_.Exception.Message }
            finally {
                $caseWatch.Stop(); $end = Get-Date
                if ($null -ne $gpu -and $gpu.HasExited) { $gpuExit = $gpu.ExitCode; $gpuError = "GPU sampler exited early ($gpuExit); samples may be incomplete." }
                try { Stop-OwnedProcess $gpu $gpuPump } catch { $gpuError = "GPU monitor cleanup/read error: $($_.Exception.Message)" }
                $gpuRows = @(Read-Gpu $gpuFile)
                $cards = @(
                    foreach ($group in ($gpuRows | Group-Object Gpu)) {
                        $baseline = @($group.Group | Where-Object Time -lt $begin)
                        $active = @($group.Group | Where-Object { $_.Time -ge $begin -and $_.Time -le $end })
                        $base = if ($baseline.Count) { ($baseline | Select-Object -Last 1).MiB } else { $null }
                        $max = if ($active.Count) { ($active | Measure-Object MiB -Maximum).Maximum } else { $null }
                        [pscustomobject]@{Uuid=$group.Name; BaselineMiB=$base; WholeCardPeakMiB=$max; DeltaMiB=$(if ($null -ne $base -and $null -ne $max) {$max-$base} else {$null}); Samples=$active.Count}
                    }
                )
                if (-not $cards.Count -and -not $gpuError) { $gpuError = 'No valid GPU readings.' }
                $cards | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $dir 'gpu-summary.json') -Encoding utf8
                $row = [pscustomobject]@{
                    Case=$id; Input=$image; InputWidth=$sourceSize[0]; InputHeight=$sourceSize[1]; Group=$case.Group; Mode=$case.Mode; Tile=$case.Tile; Noise=$case.Noise
                    Status=$(if ($failure) {'Failed'} else {'OK'}); Error=$failure
                    EngineSeconds=[math]::Round(($stages | Measure-Object Seconds -Sum).Sum,3); WallSeconds=[math]::Round($caseWatch.Elapsed.TotalSeconds,3)
                    PeakWorkingSetMiB=$(if ($stages.Count) {[math]::Round(($stages | Measure-Object PeakBytes -Maximum).Maximum/1MB,2)} else {$null})
                    RamSamples=($stages | Measure-Object Samples -Sum).Sum; GpuCardsJson=(ConvertTo-Json -InputObject $cards -Depth 4 -Compress); GpuError=$gpuError
                    Output=$(if (-not $failure) {$inputStage} else {''}); Directory=$dir
                }
                $rows.Add($row)
                $rows | Export-Csv -LiteralPath (Join-Path $run 'results.csv') -NoTypeInformation -Encoding utf8BOM
                Write-Host "$id : $($row.Status), $($row.EngineSeconds) s, RAM $($row.PeakWorkingSetMiB) MiB"
            }
        }
    }
} finally {
    $summary = @(
        '# A9 developer benchmark'
        "Engine: $engine (20250915 pinned by project); PowerShell $($PSVersionTable.PSVersion)"
        "Completed cases: $($rows.Count); OK: $(@($rows | Where-Object Status -eq OK).Count); failed: $(@($rows | Where-Object Status -eq Failed).Count)"
        'EngineSeconds sums process start-to-exit durations; WallSeconds also includes output checks and inter-stage overhead. Input JPEG is sent directly to CLI; GUI Qt decode/encode is not benchmarked.'
        'RAM: launched waifu2x PID PeakWorkingSet64 every 200 ms plus exit read, maximum across sequential stages (NOT a sum or process-tree aggregate). Upstream uses worker threads. Tree termination is used for timeout/Ctrl+C cleanup.'
        'GPU: nvidia-smi --query-gpu=timestamp,uuid,memory.used --format=csv,noheader,nounits -lms 200. Per UUID: last pre-run baseline, maximum active whole-card MiB, signed peak-minus-baseline estimate. NOT exclusive process VRAM. Other applications affect the figures. Missing readings stay null; see GpuError.'
        'Two-stage: -n N -s 2 then -n N -s 2 via retained PNG, matching direct -s 4 reuse of the same model. Quantization/intermediate encoding can change pixels.'
        'One cold/uncontrolled run per case; no statistical speed claim. Sampling may miss short GPU spikes; actual cadence may drift. Raw timestamps/RAM samples and command arguments are retained.'
        'denoise cases: 2x, tile 0, noise 0..3 for EACH source. No default settings changed. All source images are read-only; each run uses a unique output folder.'
        ''
        '| Case | Status | Engine s | RAM MiB |'
        '|---|---|---:|---:|'
        foreach ($r in $rows) { "| $($r.Case) | $($r.Status) | $($r.EngineSeconds) | $($r.PeakWorkingSetMiB) |" }
    )
    $summary | Set-Content -LiteralPath (Join-Path $run 'summary.md') -Encoding utf8
    Write-Host "Results: $run"
}
if (@($rows | Where-Object Status -ne OK).Count) { exit 1 }
