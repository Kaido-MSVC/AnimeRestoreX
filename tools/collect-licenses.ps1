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

# ---- waifu2x-ncnn-vulkan 20250915 and the components linked into its EXE
$upstream = @(
    @{ repo = 'nihui/waifu2x-ncnn-vulkan';   sha = 'a86cfb043e6482b5f08778a114df9b5faf6e73b7'; files = @('LICENSE');                 dir = 'waifu2x-ncnn-vulkan' }
    @{ repo = 'Tencent/ncnn';                sha = 'c4193aadbbb56582aa87b1850dd3d98fb8fd936d'; files = @('LICENSE.txt');             dir = 'ncnn' }
    @{ repo = 'nihui/glslang';               sha = '8cd77a808d0bffa442ae9462d5e3a8141892ba5a'; files = @('LICENSE.txt');             dir = 'glslang' }
    @{ repo = 'webmproject/libwebp';         sha = '4fa21912338357f89e4fd51cf2368325b59e9bd9'; files = @('COPYING', 'PATENTS');      dir = 'libwebp' }
    @{ repo = 'libjpeg-turbo/libjpeg-turbo'; sha = '4e151a4ad91001b3aa8c2ece2205c15f487ce320'; files = @('LICENSE.md', 'README.ijg'); dir = 'libjpeg-turbo' }
    @{ repo = 'pnggroup/libpng';             sha = '2b978915d82377df13fcbb1fb56660195ded868a'; files = @('LICENSE');                 dir = 'libpng' }
    @{ repo = 'zlib-ng/zlib-ng';             sha = '425439062b114a0f6cf625022c41d929c7e879f9'; files = @('LICENSE.md');              dir = 'zlib-ng' }
    @{ repo = 'tronkko/dirent';              sha = 'ee812d43cff8d3aeab3b0200298954b782eb1c47'; files = @('LICENSE');                 dir = 'dirent' }
    # models-cunet provenance
    @{ repo = 'nagadomi/waifu2x';            sha = 'f2f5c882eb3ebe735061f8113b184db58bd2972c'; files = @('LICENSE');                 dir = 'waifu2x-models\nagadomi-waifu2x' }
    @{ repo = 'lltcggie/waifu2x-caffe';      sha = 'b272b6fec0dfa27fc204ceda1f2e2e9d263c97ed'; files = @('LICENSE');                 dir = 'waifu2x-models\waifu2x-caffe' }
)
foreach ($u in $upstream) {
    foreach ($f in $u.files) {
        Get-Raw $u.repo $u.sha $f (Join-Path $Out (Join-Path $u.dir $f))
        Write-Host "  $($u.repo)@$($u.sha.Substring(0,7)) $f"
    }
}

# ---- Qt 6.12.0 (LGPLv3) and third-party code built into the shipped Qt binaries
$qtLicenseFiles = @(
    @{ zip = 'qtbase'; entry = 'LICENSES/LGPL-3.0-only.txt'; dest = 'Qt\LGPL-3.0-only.txt' }
    @{ zip = 'qtbase'; entry = 'LICENSES/GPL-3.0-only.txt';  dest = 'Qt\GPL-3.0-only.txt' }
    # Google's patent grant for libwebp (not listed in qt_attribution.json).
    @{ zip = 'qtimageformats'; entry = 'src/3rdparty/libwebp/PATENTS'; dest = 'Qt\third_party\libwebp\PATENTS' }
)
# qt_attribution.json directories (relative to <module>/src) for code in
# Qt6Core / Qt6Gui / Qt6Widgets, qwindows, qmodernwindowsstyle, qjpeg, qwebp.
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
