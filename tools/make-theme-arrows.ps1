# Developer-only: renders the small chevrons used by the style sheet
# (combo box drop-down, spin box buttons) into resources/theme/.
# Light / dark colors match text_muted in src/ui/Theme.cpp. The generated
# PNGs are committed; normal builds do not run this script.
# Uses System.Drawing from PowerShell 7 on Windows; nothing is installed.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$Out = Join-Path (Split-Path -Parent $PSScriptRoot) 'resources\theme'
New-Item -ItemType Directory -Force $Out | Out-Null

$colors = @{ light = '#7A6C60'; dark = '#B9A9A4' }
foreach ($theme in $colors.Keys) {
    $color = [System.Drawing.ColorTranslator]::FromHtml($colors[$theme])
    foreach ($dir in 'down', 'up') {
        foreach ($scale in 1, 2) {
            $size = 12 * $scale
            $bmp = New-Object System.Drawing.Bitmap $size, $size, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
            $g = [System.Drawing.Graphics]::FromImage($bmp)
            $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
            $g.Clear([System.Drawing.Color]::Transparent)
            $pen = New-Object System.Drawing.Pen $color, (1.6 * $scale)
            $pen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
            $pen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
            $pen.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
            $x0 = 2.5 * $scale; $x1 = 6 * $scale; $x2 = 9.5 * $scale
            if ($dir -eq 'down') { $yA = 4.5 * $scale; $yB = 8 * $scale } else { $yA = 8 * $scale; $yB = 4.5 * $scale }
            $pts = [System.Drawing.PointF[]]@(
                (New-Object System.Drawing.PointF $x0, $yA),
                (New-Object System.Drawing.PointF $x1, $yB),
                (New-Object System.Drawing.PointF $x2, $yA))
            $g.DrawLines($pen, $pts)
            $suffix = if ($scale -eq 2) { '@2x' } else { '' }
            $bmp.Save((Join-Path $Out "chevron-$dir-$theme$suffix.png"), [System.Drawing.Imaging.ImageFormat]::Png)
            $pen.Dispose(); $g.Dispose(); $bmp.Dispose()
        }
    }
}
Get-ChildItem $Out -Filter 'chevron-*.png' | ForEach-Object { '{0}  {1} bytes' -f $_.Name, $_.Length }
