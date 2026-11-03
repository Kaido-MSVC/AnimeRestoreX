# Collects the original third-party license texts into licenses/.
# Upstream files are downloaded from pinned commits (byte-for-byte); Qt files are
# copied out of the official Qt 6.12.0 source archives (fetch-qt-sources.ps1).
# Re-running produces the same tree; the result is committed to git.
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
Add-Type -AssemblyName System.IO.Compression.FileSystem

$Root = Split-Path -Parent $PSScriptRoot
$Out  = Join-Path $Root 'licenses'
$QtSrc = Join-Path $Root 'third_party\_download\qt-src'

function Get-Raw([string]$repo, [string]$sha, [string]$path, [string]$dest) {
    $url = "https://raw.githubusercontent.com/$repo/$sha/$path"
    New-Item -ItemType Directory -Force (Split-Path -Parent $dest) | Out-Null
    Invoke-WebRequest -Uri $url -OutFile $dest -UseBasicParsing
}

# "src/gui/painting/../../3rdparty/x" -> "src/3rdparty/x"
function Resolve-ZipPath([string]$path) {
    $parts = New-Object System.Collections.Generic.List[string]
    foreach ($p in $path -split '/') {
        if ($p -eq '..') { $parts.RemoveAt($parts.Count - 1) }
        elseif ($p -ne '.' -and $p -ne '') { $parts.Add($p) }
    }
    return ($parts -join '/')
}

# Folders of components that are no longer shipped (v1.x: waifu2x-ncnn-vulkan, ncnn and
# the libraries linked into its EXE, CUnet model provenance). Removed so that a re-run
# cannot leave stale notices behind. Qt/ is rebuilt below.
foreach ($old in 'waifu2x-ncnn-vulkan', 'ncnn', 'glslang', 'libwebp', 'libjpeg-turbo', 'libpng',
                 'zlib-ng', 'dirent', 'waifu2x-models') {
    $p = Join-Path $Out $old
    if (Test-Path $p) { Remove-Item $p -Recurse -Force; Write-Host "  removed licenses\$old" }
}

# ---- nunif (the project the swin_unet/art models come from), at the commit the
# ONNX models were exported with (models/manifest.json source.exported_with_commit).
# The licence of the model weights themselves is UNRESOLVED (docs/v2.0-P0-P1報告.md §5).
$upstream = @(
    @{ repo = 'nagadomi/nunif'; sha = '31c0137dc949807e382d918dc325c65fd6703092'; files = @('LICENSE', 'NOTICE'); dir = 'nunif' }
)
foreach ($u in $upstream) {
    foreach ($f in $u.files) {
        Get-Raw $u.repo $u.sha $f (Join-Path $Out (Join-Path $u.dir $f))
        Write-Host "  $($u.repo)@$($u.sha.Substring(0,7)) $f"
    }
}

# ---- Microsoft Windows ML Runtime 2.5.96 (onnxruntime.dll, DirectML.dll): the licence
# terms and third-party notices from the pinned, hash-checked NuGet package
# (tools/fetch-runtime.ps1), copied byte for byte.
$winml = Join-Path $Root 'third_party\windowsml\2.5.96'
$winmlFiles = @{
    'license.txt'           = '66395f8cb219087fae2bd025010bd9076b736c14f03b48f20295471c0c376814'
    'ThirdPartyNotices.txt' = 'fb0af774b4d7cffc5b9d046f2aaeade2f37df2f80abf8033c95dfffcc77a8866'
}
New-Item -ItemType Directory -Force (Join-Path $Out 'WindowsML') | Out-Null
foreach ($f in $winmlFiles.Keys) {
    $src = Join-Path $winml $f
    if (-not (Test-Path $src)) { throw "Missing $src; run tools/fetch-runtime.ps1" }
    $h = (Get-FileHash $src -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($h -ne $winmlFiles[$f]) { throw "SHA256 mismatch for $src ($h)" }
    Copy-Item $src (Join-Path $Out "WindowsML\$f") -Force
    Write-Host "  Windows ML 2.5.96 $f"
}

# ---- Qt 6.12.0 (LGPLv3) and third-party code built into the shipped Qt binaries
$qtLicenseFiles = @(
    @{ zip = 'qtbase'; entry = 'LICENSES/LGPL-3.0-only.txt'; dest = 'Qt\LGPL-3.0-only.txt' }
    @{ zip = 'qtbase'; entry = 'LICENSES/GPL-3.0-only.txt';  dest = 'Qt\GPL-3.0-only.txt' }
    # Google's patent grant for libwebp (not listed in qt_attribution.json).
    @{ zip = 'qtimageformats'; entry = 'src/3rdparty/libwebp/PATENTS'; dest = 'Qt\third_party\libwebp\PATENTS' }
)
# qt_attribution.json directories (relative to <module>/src) for code in
# Qt6Core / Qt6Gui / Qt6Widgets, qwindows, qjpeg, qwebp. (The style plugin
# qmodernwindowsstyle, shipped until 1.1.1, has no third-party code of its own.)
$qtAttributions = @{
    qtbase = @('3rdparty/D3D12MemoryAllocator', '3rdparty/VulkanMemoryAllocator', '3rdparty/blake2',
               '3rdparty/double-conversion', '3rdparty/easing', '3rdparty/emoji-segmenter',
               '3rdparty/freetype', '3rdparty/harfbuzz-ng', '3rdparty/icc', '3rdparty/libjpeg',
               '3rdparty/libpng', '3rdparty/md4', '3rdparty/md4c', '3rdparty/md5', '3rdparty/pcre2',
               '3rdparty/rfc6234', '3rdparty/sha1', '3rdparty/sha3', '3rdparty/siphash',
               '3rdparty/tinycbor', '3rdparty/wintab', '3rdparty/zlib', 'corelib/global',
               'corelib/mimetypes/3rdparty', 'corelib/text', 'gui/opengl', 'gui/painting',
               'gui/rhi', 'gui/text', 'gui/util', 'gui/vulkan')
    qtimageformats = @('3rdparty/libwebp')
}
# Not shipped / not built for Windows desktop.
$qtSkipIds = @('qeventdispatcher_cf')

$index = New-Object System.Collections.Generic.List[object]
foreach ($module in 'qtbase', 'qtimageformats') {
    $zipPath = Join-Path $QtSrc "$module-everywhere-src-6.12.0.zip"
    if (-not (Test-Path $zipPath)) { throw "Missing $zipPath; run tools/fetch-qt-sources.ps1" }
    $zip = [IO.Compression.ZipFile]::OpenRead($zipPath)
    try {
        $prefix = "$module-everywhere-src-6.12.0/"
        $byName = @{}
        foreach ($e in $zip.Entries) { $byName[$e.FullName] = $e }

        $copy = {
            param([string]$entryName, [string]$destRel)
            $entry = $byName[$entryName]
            if (-not $entry) { throw "Not in archive: $entryName" }
            $dest = Join-Path $Out $destRel
            New-Item -ItemType Directory -Force (Split-Path -Parent $dest) | Out-Null
            [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $dest, $true)
        }

        foreach ($l in $qtLicenseFiles | Where-Object { $_.zip -eq $module }) {
            & $copy ($prefix + $l.entry) $l.dest
        }

        foreach ($rel in $qtAttributions[$module]) {
            $jsonName = "${prefix}src/$rel/qt_attribution.json"
            $reader = New-Object IO.StreamReader($byName[$jsonName].Open())
            $items = @($reader.ReadToEnd() | ConvertFrom-Json)
            $reader.Close()
            foreach ($a in $items) {
                if ($qtSkipIds -contains $a.Id) { continue }
                $files = @(@($a.LicenseFile) + @($a.LicenseFiles) | Where-Object { $_ })
                $copied = @()
                if ($files.Count -gt 0) {
                    foreach ($f in $files) {
                        $entryName = $prefix + (Resolve-ZipPath "src/$rel/$f")
                        $name = Split-Path -Leaf $f
                        & $copy $entryName "Qt\third_party\$($a.Id)\$name"
                        $copied += $name
                    }
                } else {
                    # No dedicated file: use the SPDX text from <module>/LICENSES.
                    $ids = ($a.LicenseId -split '\s+(?:AND|OR)\s+') | ForEach-Object { $_.Trim('()') }
                    foreach ($id in $ids) {
                        & $copy "${prefix}LICENSES/$id.txt" "Qt\third_party\$($a.Id)\$id.txt"
                        $copied += "$id.txt"
                    }
                }
                $index.Add([pscustomobject]@{
                    Module = $module; Id = $a.Id; Name = $a.Name; Version = $a.Version
                    License = $a.LicenseId; Files = ($copied -join ', ')
                    Copyright = ((@($a.Copyright) -join ' ') -replace '\s+', ' ')
                })
            }
        }
    } finally {
        $zip.Dispose()
    }
}
$index | Sort-Object Module, Id | Format-Table Module, Id, Version, License, Files -AutoSize | Out-String -Width 220 | Write-Host
$index | Sort-Object Module, Id | ConvertTo-Json -Depth 3 |
    Set-Content -Path (Join-Path $Out 'Qt\third_party\index.json') -Encoding utf8
Write-Host "Done: $Out"
