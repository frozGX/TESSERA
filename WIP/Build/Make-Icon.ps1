<#
.SYNOPSIS
    Draws Tessera's two icons into WIP\Src\Tessera: Tessera.ico and Tessera-Admin.ico.

.DESCRIPTION
    Tessera.ico is a white frog on sky blue with navy line work. Tessera-Admin.ico, shown while Tessera runs
    as administrator, is the same creature grown up and all in gold, with gradients and highlights so it reads as
    metal rather than yellow: the frog's outline exactly, and a toad's face in it, heavy-lidded, with an
    straight, outlined mouth between puffed cheeks, and a few scales that make its outline knobbly, each fading from the body's colour to a deeper gold. Nothing in it is dark
    but the line work, so it stays readable on the taskbar.

    Each is drawn afresh at every size (256 down to 16) rather than shrunk, with thicker lines and fewer
    details at the small sizes so it still reads in the tray. A sheet of both is written to the folder given
    by -Preview.
#>

[CmdletBinding()]
param([string] $Preview = '')

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Add-Type -AssemblyName System.Drawing

function New-Sheet([int] $size) {
    $bitmap = New-Object System.Drawing.Bitmap $size, $size, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bitmap)
    $g.SmoothingMode = 'AntiAlias'
    $g.PixelOffsetMode = 'HighQuality'
    $g.Clear([System.Drawing.Color]::Transparent)
    $g.ScaleTransform($size / 256.0, $size / 256.0)   # everything is drawn on a 256 x 256 sheet
    return $bitmap, $g
}

# $paint: a colour, or a brush (a gradient, say).
function Add-Tile($g, $paint) {
    if ($paint -is [System.Drawing.Color]) { $paint = New-Object System.Drawing.SolidBrush $paint }
    $tile = New-Object System.Drawing.Drawing2D.GraphicsPath
    $r = 56; $d = 2 * $r
    $tile.AddArc(0, 0, $d, $d, 180, 90); $tile.AddArc(256 - $d, 0, $d, $d, 270, 90)
    $tile.AddArc(256 - $d, 256 - $d, $d, $d, 0, 90); $tile.AddArc(0, 256 - $d, $d, $d, 90, 90)
    $tile.CloseFigure()
    $g.FillPath($paint, $tile)
}

# Ellipses given as centre x, centre y, radius x, radius y. The outline colour first, a line's width larger,
# then the body colour on top, which leaves one outline around the whole shape.
function Add-Body($g, $parts, [System.Drawing.Color] $outline, [System.Drawing.Color] $body, [double] $line) {
    $outer = New-Object System.Drawing.SolidBrush $outline
    $inner = New-Object System.Drawing.SolidBrush $body
    foreach ($p in $parts) { $g.FillEllipse($outer, $p[0] - $p[2] - $line, $p[1] - $p[3] - $line, 2 * ($p[2] + $line), 2 * ($p[3] + $line)) }
    foreach ($p in $parts) { $g.FillEllipse($inner, $p[0] - $p[2], $p[1] - $p[3], 2 * $p[2], 2 * $p[3]) }
}

# $layer, here and for the toad: 'all', or only the 'tile' behind, or only the 'figure' in front of it.
function Draw-Frog([int] $size, [string] $layer = 'all') {
    $sky   = [System.Drawing.Color]::FromArgb(255, 79, 169, 232)
    $navy  = [System.Drawing.Color]::FromArgb(255, 28, 72, 150)
    $blush = [System.Drawing.Color]::FromArgb(255, 176, 218, 247)
    $bitmap, $g = New-Sheet $size
    $line = [Math]::Max(9.0, 300.0 / $size)           # the outline, at least about a pixel wide
    if ($layer -ne 'figure') { Add-Tile $g $sky }
    if ($layer -eq 'tile') { $g.Dispose(); return $bitmap }
    Add-Body $g @(@(84, 92, 40, 40), @(172, 92, 40, 40), @(128, 156, 96, 70)) $navy ([System.Drawing.Color]::White) $line

    $navyBrush = New-Object System.Drawing.SolidBrush $navy
    $pupil = [Math]::Max(15.0, 400.0 / $size)
    foreach ($x in 84, 172) { $g.FillEllipse($navyBrush, $x - $pupil, 94 - $pupil, 2 * $pupil, 2 * $pupil) }
    if ($size -ge 32) {
        $blushBrush = New-Object System.Drawing.SolidBrush $blush
        foreach ($x in 62, 194) { $g.FillEllipse($blushBrush, $x - 16, 168 - 11, 32, 22) }
    }
    $pen = New-Object System.Drawing.Pen $navy, $line
    $pen.StartCap = 'Round'; $pen.EndCap = 'Round'
    $g.DrawArc($pen, 84, 118, 88, 70, 25, 130)        # a smile
    $g.Dispose()
    return $bitmap
}

function Draw-Toad([int] $size, [string] $layer = 'all') {
    # Gold throughout, and nothing dark but the line work: a dark tile or border is lost against the taskbar.
    $body   = [System.Drawing.Color]::FromArgb(255, 255, 240, 196)   # the toad: pale gold, as the frog is white
    $scale  = [System.Drawing.Color]::FromArgb(255, 236, 178, 60)    # its scales: the tile's gold, a shade deeper
    $ink    = [System.Drawing.Color]::FromArgb(255, 150, 96, 14)     # the lines: deep amber, where the frog has navy
    $white  = [System.Drawing.Color]::White
    $bitmap, $g = New-Sheet $size
    $line = [Math]::Max(9.0, 300.0 / $size)

    # What makes it gold rather than yellow is light: the tile runs from a bright corner to a deep one, with
    # a soft sheen across its top, as polished metal has.
    $corner = New-Object System.Drawing.PointF 0, 0
    $far = New-Object System.Drawing.PointF 256, 256
    $metal = New-Object System.Drawing.Drawing2D.LinearGradientBrush $corner, $far, ([System.Drawing.Color]::FromArgb(255, 255, 226, 122)), ([System.Drawing.Color]::FromArgb(255, 222, 158, 40))
    if ($layer -ne 'figure') {
        Add-Tile $g $metal
        $state = $g.Save()
        $clip = New-Object System.Drawing.Drawing2D.GraphicsPath
        $clip.AddEllipse(-40, -150, 336, 250)
        $g.SetClip($clip)
        Add-Tile $g ([System.Drawing.Color]::FromArgb(46, 255, 255, 255))
        $g.Restore($state)
    }
    if ($layer -eq 'tile') { $g.Dispose(); return $bitmap }

    # The frog's own outline, to the pixel: the same eye bumps on the same head. Whatever is added to the
    # toad only ever adds to that outline; nothing is taken from it.
    $eyes = @(@(84, 92, 40, 40), @(172, 92, 40, 40))
    $head = @(128, 156, 96, 70)

    # Scales: a few round ones set on the edge of the head, four to a side, so that half of each
    # stands out from it and the outline itself goes knobbly, as a toad's does. Too fine for the smallest
    # sizes, where the outline stays smooth.
    $scales = @()
    if ($size -ge 40) {
        # Angle round the head, and size: the topmost pair the largest, each row below a little smaller.
        foreach ($row in @(-10, 18), @(18, 16), @(44, 14), @(70, 12), @(110, 12), @(136, 14), @(162, 16), @(190, 18)) {
            $a = $row[0] * [Math]::PI / 180
            $scales += ,@(($head[0] + $head[2] * [Math]::Cos($a)), ($head[1] + $head[3] * [Math]::Sin($a)), $row[1], $row[1])
        }
    }
    $parts = $eyes + ,$head + $scales
    Add-Body $g $parts $ink $body $line

    # The toad is lit the same way as the tile: near white on top, warming to gold underneath, with a glint
    # on each eye bump.
    $shape = New-Object System.Drawing.Drawing2D.GraphicsPath
    $shape.FillMode = 'Winding'
    foreach ($p in $parts) { $shape.AddEllipse([single]($p[0] - $p[2]), [single]($p[1] - $p[3]), [single](2 * $p[2]), [single](2 * $p[3])) }
    $top = New-Object System.Drawing.PointF 0, 50
    $bottom = New-Object System.Drawing.PointF 0, 244
    $lit = New-Object System.Drawing.Drawing2D.LinearGradientBrush $top, $bottom, ([System.Drawing.Color]::FromArgb(255, 255, 251, 232)), ([System.Drawing.Color]::FromArgb(255, 247, 206, 104))
    $g.FillPath($lit, $shape)
    if ($size -ge 32) {
        $glint = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(170, 255, 255, 255))
        foreach ($eye in $eyes) { $g.FillEllipse($glint, $eye[0] - 22, $eye[1] - 25, 26, 9) }
    }

    $inkBrush = New-Object System.Drawing.SolidBrush $ink
    $pen = New-Object System.Drawing.Pen $ink, $line
    $pen.StartCap = 'Round'; $pen.EndCap = 'Round'
    $fine = New-Object System.Drawing.Pen $ink, ([Math]::Max(6.0, 200.0 / $size))
    $fine.StartCap = 'Round'; $fine.EndCap = 'Round'

    # Each scale starts in the body's own colour where it joins the head and deepens to the darker gold at
    # its tip, so it grows out of the head rather than sitting on it. No line divides the two.
    if ($scales.Count) {
        $deep = [System.Drawing.Color]::FromArgb(255, 214, 146, 34)
        foreach ($s in $scales) {
            $r = $s[2]
            $dx = $head[0] - $s[0]; $dy = $head[1] - $s[1]
            $length = [Math]::Sqrt($dx * $dx + $dy * $dy); $dx /= $length; $dy /= $length   # which way is inwards
            $inner = New-Object System.Drawing.PointF ([single]($s[0] + $dx * ($r + 1))), ([single]($s[1] + $dy * ($r + 1)))
            $tip = New-Object System.Drawing.PointF ([single]($s[0] - $dx * ($r + 1))), ([single]($s[1] - $dy * ($r + 1)))
            # The body's colour at that height: it runs from near white at 50 down to gold at 244.
            $share = [Math]::Min(1.0, [Math]::Max(0.0, ($inner.Y - 50) / 194.0))
            $here = [System.Drawing.Color]::FromArgb(255, [int](255 - 8 * $share), [int](251 - 45 * $share), [int](232 - 128 * $share))
            $fade = New-Object System.Drawing.Drawing2D.LinearGradientBrush $inner, $tip, $here, $deep
            $g.FillEllipse($fade, [single]($s[0] - $r), [single]($s[1] - $r), 2 * $r, 2 * $r)
        }
    }
    foreach ($eye in $eyes) {
        $x = $eye[0]; $y = $eye[1] + 4
        # The eye is one colour, with no white to it: a level oval, as a toad's pupil is.
        $wx = [Math]::Max(22.0, 330.0 / $size); $wy = $wx * 0.72
        $px = $wx * 0.86; $py = [Math]::Max($wy * 0.62, 130.0 / $size)
        $g.FillEllipse($inkBrush, $x - $px, $y - $py, 2 * $px, 2 * $py)
        if ($size -ge 24) {
            # A heavy lid: the top of the eye covered again in the body's colour, with a line under it.
            $lid = $y - $wy * 0.34
            $inner = $g.Save()
            $g.SetClip((New-Object System.Drawing.RectangleF ($x - $wx - 2), ($y - $wy - 2), (2 * $wx + 4), ($lid - ($y - $wy) + 2)))
            $g.FillEllipse($lit, $x - $wx - 1, $y - $wy - 1, 2 * $wx + 2, 2 * $wy + 2)
            $g.Restore($inner)
            $half = [Math]::Sqrt([Math]::Max(0.0, 1 - [Math]::Pow(($lid - $y) / $wy, 2))) * $wx + 3
            $g.DrawLine($fine, [single]($x - $half), [single]$lid, [single]($x + $half), [single]$lid)
        }
    }

    # The mouth: a straight line, a white lip with an outline all round it and a parting along its middle,
    # and a bracket at each end, ( and ),
    # which puffs the cheeks out. At the smallest sizes there is only room for the line. Nostrils above.
    $mouth = 190
    if ($size -ge 32) {
        $outer = New-Object System.Drawing.Pen $ink, ($line * 2.5)
        $outer.StartCap = 'Round'; $outer.EndCap = 'Round'
        $lip = New-Object System.Drawing.Pen $white, ($line * 1.3)
        $lip.StartCap = 'Round'; $lip.EndCap = 'Round'
        $g.DrawLine($outer, 94, $mouth, 162, $mouth)
        $g.DrawLine($lip, 94, $mouth, 162, $mouth)
        if ($size -ge 48) {
            # The lips parted: a fine line along the middle, stopping just short of each end.
            $parting = New-Object System.Drawing.Pen $ink, ([Math]::Max(2.6, 110.0 / $size))
            $parting.StartCap = 'Round'; $parting.EndCap = 'Round'
            $g.DrawLine($parting, 97, $mouth, 159, $mouth)
        }
        $g.DrawArc($fine, 70, $mouth - 15, 18, 30, 105, 150)     # (
        $g.DrawArc($fine, 168, $mouth - 15, 18, 30, -75, 150)    # )
        foreach ($x in 116, 140) { $g.FillEllipse($inkBrush, $x - 4.5, 152 - 4.5, 9, 9) }
    } else {
        $g.DrawLine($pen, 92, $mouth, 164, $mouth)
    }
    $g.Dispose()
    return $bitmap
}

# A finished icon cut into mosaic tiles (a tessera is one such tile): $columns x $rows of them, with clear
# grout between, each tile a touch lighter or darker than its neighbours and, where there are pixels
# enough to show it, lit along its top edge and shaded along its bottom one. Done pixel by pixel on the
# finished picture, so the grout is a whole number of pixels at every size and never a blur.
# $gap: the grout's width on the 256 sheet. $fill: its colour; without one it is left clear.
function Add-Mosaic([System.Drawing.Bitmap] $source, [int] $columns, [int] $rows, [double] $gap = 9, $fill = $null) {
    $size = $source.Width
    $grout = [Math]::Max(1, [int][Math]::Round($size * $gap / 256.0))
    $radius = if ($size -ge 40) { [int][Math]::Round($size * 12 / 256.0) } else { 0 }
    $edge = if ($size -ge 48) { [Math]::Max(1, [int][Math]::Round($size * 7 / 256.0)) } else { 0 }
    $shades = 1.05, 0.96, 1.0, 0.95, 1.04, 0.98, 1.03, 0.97, 1.01
    $bitmap = New-Object System.Drawing.Bitmap $size, $size, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bitmap)
    $g.Clear([System.Drawing.Color]::Transparent)
    $g.SmoothingMode = 'AntiAlias'
    $whole = New-Object System.Drawing.Rectangle 0, 0, $size, $size
    $paint = {
        param([double] $shade)
        $matrix = New-Object System.Drawing.Imaging.ColorMatrix
        $matrix.Matrix00 = $shade; $matrix.Matrix11 = $shade; $matrix.Matrix22 = $shade
        $with = New-Object System.Drawing.Imaging.ImageAttributes
        $with.SetColorMatrix($matrix)
        $g.DrawImage($source, $whole, 0, 0, $size, $size, [System.Drawing.GraphicsUnit]::Pixel, $with)
        $with.Dispose()
    }
    if ($fill) { # the icon's own shape in the grout's colour, for the tiles to lie on
        $matrix = New-Object System.Drawing.Imaging.ColorMatrix
        $matrix.Matrix00 = 0; $matrix.Matrix11 = 0; $matrix.Matrix22 = 0
        $matrix.Matrix40 = $fill.R / 255.0; $matrix.Matrix41 = $fill.G / 255.0; $matrix.Matrix42 = $fill.B / 255.0
        $with = New-Object System.Drawing.Imaging.ImageAttributes
        $with.SetColorMatrix($matrix)
        $g.DrawImage($source, $whole, 0, 0, $size, $size, [System.Drawing.GraphicsUnit]::Pixel, $with)
        $with.Dispose()
    }
    $index = 0
    for ($row = 0; $row -lt $rows; $row++) {
        for ($column = 0; $column -lt $columns; $column++) {
            # Tile edges fall on whole pixels; the grout is taken half from each side of the line between two.
            $left = [int][Math]::Round($size * $column / $columns) + $(if ($column -gt 0) { [int][Math]::Ceiling($grout / 2.0) } else { 0 })
            $right = [int][Math]::Round($size * ($column + 1) / $columns) - $(if ($column -lt $columns - 1) { [int][Math]::Floor($grout / 2.0) } else { 0 })
            $top = [int][Math]::Round($size * $row / $rows) + $(if ($row -gt 0) { [int][Math]::Ceiling($grout / 2.0) } else { 0 })
            $bottom = [int][Math]::Round($size * ($row + 1) / $rows) - $(if ($row -lt $rows - 1) { [int][Math]::Floor($grout / 2.0) } else { 0 })
            $tile = New-Object System.Drawing.Drawing2D.GraphicsPath
            if ($radius -gt 0) {
                $d = 2 * $radius
                $tile.AddArc($left, $top, $d, $d, 180, 90); $tile.AddArc($right - $d, $top, $d, $d, 270, 90)
                $tile.AddArc($right - $d, $bottom - $d, $d, $d, 0, 90); $tile.AddArc($left, $bottom - $d, $d, $d, 90, 90)
                $tile.CloseFigure()
            } else {
                $tile.AddRectangle((New-Object System.Drawing.Rectangle $left, $top, ($right - $left), ($bottom - $top)))
            }
            $shade = $shades[$index % $shades.Count]
            $g.SetClip($tile)
            & $paint $shade
            if ($edge -gt 0) {
                $g.SetClip((New-Object System.Drawing.Rectangle $left, $top, ($right - $left), $edge), 'Intersect')
                $g.CompositingMode = 'SourceCopy'
                & $paint ($shade * 1.10)
                $g.SetClip($tile)
                $g.SetClip((New-Object System.Drawing.Rectangle $left, ($bottom - $edge), ($right - $left), $edge), 'Intersect')
                & $paint ($shade * 0.90)
                $g.CompositingMode = 'SourceOver'
            }
            $g.ResetClip()
            $tile.Dispose()
            $index++
        }
    }
    $g.Dispose()
    $source.Dispose()
    return $bitmap
}

# The tile behind as a mosaic, and the figure whole in front of it: the grout runs behind the frog.
function Draw-OnMosaic([string] $draw, [int] $size, [int] $columns, [int] $rows, [double] $gap, $fill) {
    $bitmap = Add-Mosaic (& $draw $size 'tile') $columns $rows $gap $fill
    $figure = & $draw $size 'figure'
    $g = [System.Drawing.Graphics]::FromImage($bitmap)
    $g.DrawImage($figure, 0, 0, $size, $size)
    $g.Dispose()
    $figure.Dispose()
    return $bitmap
}

function Get-Png([System.Drawing.Bitmap] $bitmap) {
    $stream = New-Object IO.MemoryStream
    $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
    return ,$stream.ToArray()
}

# An .ico: a directory of entries, then each picture as a PNG.
function Write-Ico([string] $path, [scriptblock] $draw) {
    $images = foreach ($size in 256, 64, 48, 40, 32, 24, 20, 16) { $b = & $draw $size; [pscustomobject]@{ Size = $size; Png = (Get-Png $b) }; $b.Dispose() }
    $out = New-Object IO.MemoryStream
    $w = New-Object IO.BinaryWriter $out
    $w.Write([uint16]0); $w.Write([uint16]1); $w.Write([uint16]$images.Count)
    $offset = 6 + 16 * $images.Count
    foreach ($image in $images) {
        $w.Write([byte]($image.Size % 256)); $w.Write([byte]($image.Size % 256))   # 0 stands for 256
        $w.Write([byte]0); $w.Write([byte]0); $w.Write([uint16]1); $w.Write([uint16]32)
        $w.Write([uint32]$image.Png.Length); $w.Write([uint32]$offset)
        $offset += $image.Png.Length
    }
    foreach ($image in $images) { $w.Write([byte[]]$image.Png) }
    $w.Flush()
    [IO.File]::WriteAllBytes($path, $out.ToArray())
    Write-Host "  $path" -ForegroundColor Green
}

$source = Join-Path (Split-Path -Parent $PSScriptRoot) 'Src\Tessera'
# The icons as chosen: the figure whole, in front of a tile laid as a mosaic of four, two by two, with
# narrow grout in a brighter shade of the tile: bright blue behind the frog, bright gold behind the toad.
$brightBlue = [System.Drawing.Color]::FromArgb(255, 150, 215, 255)
$brightGold = [System.Drawing.Color]::FromArgb(255, 255, 238, 150)
Write-Ico (Join-Path $source 'Tessera.ico') { param($size) Draw-OnMosaic 'Draw-Frog' $size 2 2 4.95 $brightBlue }
Write-Ico (Join-Path $source 'Tessera-Admin.ico') { param($size) Draw-OnMosaic 'Draw-Toad' $size 2 2 4.95 $brightGold }

if ($Preview) {
    New-Item -ItemType Directory -Force $Preview | Out-Null
    # icon.png: the two icons as they are. mosaic-6.png and mosaic-8.png: each as a mosaic of six or eight
    # tiles, wider than tall and taller than wide. On every sheet an icon is shown large, then its small
    # sizes as they are and blown up so their pixels can be judged.
    $sheets = [ordered]@{
        'icon.png'     = { param($size) Draw-OnMosaic 'Draw-Frog' $size 2 2 4.95 $brightBlue }, { param($size) Draw-OnMosaic 'Draw-Toad' $size 2 2 4.95 $brightGold }
        'plain.png'    = { param($size) Draw-Frog $size }, { param($size) Draw-Toad $size }
        'mosaic-6.png' = { param($size) Add-Mosaic (Draw-Frog $size) 3 2 }, { param($size) Add-Mosaic (Draw-Toad $size) 3 2 },
                         { param($size) Add-Mosaic (Draw-Frog $size) 2 3 }, { param($size) Add-Mosaic (Draw-Toad $size) 2 3 }
        'mosaic-8.png' = { param($size) Add-Mosaic (Draw-Frog $size) 4 2 }, { param($size) Add-Mosaic (Draw-Toad $size) 4 2 },
                         { param($size) Add-Mosaic (Draw-Frog $size) 2 4 }, { param($size) Add-Mosaic (Draw-Toad $size) 2 4 }
    }
    # Tried and not chosen: the whole icon cut into nine or four tiles, figure and all.
    $sheets['mosaic-9.png'] = { param($size) Add-Mosaic (Draw-Frog $size) 3 3 4.5 $brightBlue }, { param($size) Add-Mosaic (Draw-Toad $size) 3 3 4.5 $brightGold }
    $sheets['mosaic-4.png'] = { param($size) Add-Mosaic (Draw-Frog $size) 2 2 4.5 $brightBlue }, { param($size) Add-Mosaic (Draw-Toad $size) 2 2 4.5 $brightGold }
    $sheets['mosaic-behind.png'] = { param($size) Draw-OnMosaic 'Draw-Frog' $size 2 2 4.5 $brightBlue }, { param($size) Draw-OnMosaic 'Draw-Toad' $size 2 2 4.5 $brightGold },
                                   { param($size) Draw-OnMosaic 'Draw-Frog' $size 3 3 4.5 $brightBlue }, { param($size) Draw-OnMosaic 'Draw-Toad' $size 3 3 4.5 $brightGold }
    foreach ($name in $sheets.Keys) {
        $draws = @($sheets[$name])
        $across = if ($draws.Count -gt 2) { 2 } else { 1 }
        $sheet = New-Object System.Drawing.Bitmap (720 * $across), (276 * [int][Math]::Ceiling($draws.Count / $across) - 20)
        $g = [System.Drawing.Graphics]::FromImage($sheet)
        $g.Clear([System.Drawing.Color]::FromArgb(255, 32, 34, 40))
        for ($i = 0; $i -lt $draws.Count; $i++) {
            $draw = $draws[$i]
            $left = 720 * ($i % $across); $top = 276 * [int][Math]::Floor($i / $across)
            $g.InterpolationMode = 'HighQualityBicubic'; $g.PixelOffsetMode = 'HighQuality'
            $big = & $draw 256; $g.DrawImage($big, $left, $top); $big.Dispose()
            $g.InterpolationMode = 'NearestNeighbor'; $g.PixelOffsetMode = 'Half'
            $x = $left + 280
            foreach ($size in 48, 32, 16) {
                $small = & $draw $size
                $g.DrawImage($small, $x, $top + 16, $size, $size)
                $g.DrawImage($small, $x, $top + 96, $size * 3, $size * 3)
                $small.Dispose()
                $x += [Math]::Max($size * 3, 64) + 24
            }
        }
        $g.Dispose()
        $sheet.Save((Join-Path $Preview $name), [System.Drawing.Imaging.ImageFormat]::Png)
        $sheet.Dispose()
    }
}
