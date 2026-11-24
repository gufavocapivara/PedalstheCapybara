# Gera as imagens dos plugins de demonstracao (fundo, knob, LED, footswitch e label).
# Uso: powershell -ExecutionPolicy Bypass -File tools/make_demo_assets.ps1
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

function New-Graphics([System.Drawing.Bitmap]$bmp) {
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
    return $g
}

function New-Canvas([int]$w, [int]$h) {
    return [System.Drawing.Bitmap]::new($w, $h, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
}

function Save-Png([System.Drawing.Bitmap]$bmp, [string]$path) {
    $dir = Split-Path -Parent $path
    if (!(Test-Path $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
}

function Add-VerticalGradient($g, [int]$w, [int]$h, [System.Drawing.Color]$a, [System.Drawing.Color]$b) {
    $rect = [System.Drawing.Rectangle]::new(0, 0, $w, $h)
    $brush = [System.Drawing.Drawing2D.LinearGradientBrush]::new($rect, $a, $b,
        [System.Drawing.Drawing2D.LinearGradientMode]::Vertical)
    $g.FillRectangle($brush, $rect)
    $brush.Dispose()
}

function Add-Screws($g, [int]$w, [int]$h, [System.Drawing.Color]$rim) {
    $pts = @(@(24, 24), @(($w - 24), 24), @(24, ($h - 24)), @(($w - 24), ($h - 24)))
    foreach ($p in $pts) {
        $x = [float]$p[0]; $y = [float]$p[1]
        $g.FillEllipse([System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(220, 10, 9, 8)), $x - 10, $y - 10, 20, 20)
        $g.DrawEllipse([System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(160, $rim.R, $rim.G, $rim.B), 1.5), $x - 9, $y - 9, 18, 18)
        $g.DrawLine([System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(130, 200, 200, 200), 1.2), $x - 5, $y, $x + 5, $y)
        $g.DrawLine([System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(90, 0, 0, 0), 1.2), $x, $y - 5, $x, $y + 5)
    }
}

function Add-Frame($g, [int]$w, [int]$h, [System.Drawing.Color]$c) {
    $pen = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(110, $c.R, $c.G, $c.B), 2)
    $g.DrawRectangle($pen, 13, 13, $w - 27, $h - 27)
    $pen.Dispose()
}

function New-Face([string]$path, [System.Drawing.Color]$top, [System.Drawing.Color]$bottom,
                  [System.Drawing.Color]$accent, [string]$motif) {
    $w = 480; $h = 640
    $bmp = New-Canvas $w $h
    $g = New-Graphics $bmp
    Add-VerticalGradient $g $w $h $top $bottom
    $pen = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(26, $accent.R, $accent.G, $accent.B), 1)

    if ($motif -eq 'drive') {
        for ($i = -8; $i -lt 24; $i++) {
            $x = $i * 40
            $g.DrawLine($pen, $x, 0, $x + 260, $h)
        }
        # silhueta de capivara
        $body = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(48, $accent.R, $accent.G, $accent.B))
        $g.FillEllipse($body, 145, 320, 190, 150)
        $g.FillEllipse($body, 188, 235, 105, 105)
        $g.FillEllipse($body, 204, 212, 30, 30)
        $g.FillEllipse($body, 258, 214, 30, 30)
        $body.Dispose()
    } elseif ($motif -eq 'echo') {
        for ($i = 1; $i -le 7; $i++) {
            $r = 38 * $i
            $g.DrawEllipse($pen, 240 - $r, 330 - $r * 0.62, $r * 2, $r * 1.24)
        }
        for ($i = 0; $i -lt 5; $i++) {
            $x = 60 + $i * 90
            $g.DrawLine($pen, $x, 0, $x - 70, $h)
        }
    } elseif ($motif -eq 'velvet') {
        for ($y = 80; $y -lt $h; $y += 34) {
            $pts = @()
            for ($x = 0; $x -le $w; $x += 12) {
                $pts += [System.Drawing.PointF]::new([float]$x, [float]($y + [Math]::Sin($x / 42.0) * 11))
            }
            $g.DrawLines($pen, [System.Drawing.PointF[]]$pts)
        }
    } elseif ($motif -eq 'tape') {
        # tape: dois carretéis e uma fita
        $g.DrawEllipse($pen, 78, 190, 120, 120)
        $g.DrawEllipse($pen, 282, 190, 120, 120)
        $g.DrawEllipse($pen, 112, 224, 52, 52)
        $g.DrawEllipse($pen, 316, 224, 52, 52)
        $g.DrawLine($pen, 138, 250, 316, 250)
        $g.DrawLine($pen, 138, 270, 316, 270)
        for ($i = 0; $i -lt 6; $i++) {
            $y = 390 + $i * 28
            $g.DrawLine($pen, 60, $y, 420, $y + 8)
        }
    } elseif ($motif -eq 'md2') {
        for ($i = -6; $i -lt 20; $i++) { $x=$i*44; $g.DrawLine($pen, $x, 0, $x+120, $h) }
        for ($y = 140; $y -lt 520; $y += 26) { $g.DrawLine($pen, 40, $y, 440, $y) }
        $b=[System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(30,$accent.R,$accent.G,$accent.B))
        $g.FillPolygon($b, [System.Drawing.PointF[]]@([System.Drawing.PointF]::new(240,220),[System.Drawing.PointF]::new(330,390),[System.Drawing.PointF]::new(150,390)))
        $b.Dispose()
    } elseif ($motif -eq 'ds1') {
        for ($i=0; $i -lt 14; $i++) { $r=28+$i*24; $g.DrawRectangle($pen, 240-$r, 320-$r, $r*2, $r*2) }
    } elseif ($motif -eq 'sd1') {
        for ($i=0; $i -lt 18; $i++) { $y=80+$i*30; $g.DrawLine($pen, 30, $y, 450, $y) }
        for ($i=0; $i -lt 10; $i++) { $x=40+$i*45; $g.DrawLine($pen, $x, 80, $x, 560) }
    } elseif ($motif -eq 'bd2') {
        for ($i=0; $i -lt 9; $i++) { $r=34+$i*34; $g.DrawEllipse($pen, 240-$r, 330-$r, $r*2, $r*2) }
        $g.DrawLine($pen, 240, 60, 240, 600); $g.DrawLine($pen, 40, 330, 440, 330)
    } elseif ($motif -eq 'mt2') {
        for ($i=-10; $i -lt 30; $i++) { $x=$i*28; $g.DrawLine($pen,$x,0,$x-180,$h); $g.DrawLine($pen,$x,0,$x+180,$h) }
        $b=[System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(28,$accent.R,$accent.G,$accent.B))
        $g.FillRectangle($b, 80, 250, 320, 170); $b.Dispose()
    } else {
        for ($i=0; $i -lt 12; $i++) { $y=90+$i*42; $g.DrawLine($pen, 40, $y, 440, $y) }
    }
    $pen.Dispose()

    Add-Frame $g $w $h $accent
    Add-Screws $g $w $h $accent

    # brilho suave no topo
    $gloss = [System.Drawing.Drawing2D.LinearGradientBrush]::new(
        [System.Drawing.Rectangle]::new(0, 0, $w, 150),
        [System.Drawing.Color]::FromArgb(30, 255, 255, 255),
        [System.Drawing.Color]::FromArgb(0, 255, 255, 255),
        [System.Drawing.Drawing2D.LinearGradientMode]::Vertical)
    $g.FillRectangle($gloss, 0, 0, $w, 150)
    $gloss.Dispose()

    $g.Dispose()
    Save-Png $bmp $path
}

function New-Knob([string]$path) {
    $w = 220; $h = 220
    $bmp = New-Canvas $w $h
    $g = New-Graphics $bmp

    $g.FillEllipse([System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(90, 0, 0, 0)), 14, 18, 192, 192)
    $g.FillEllipse([System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 22, 24, 28)), 8, 8, 204, 204)
    $g.DrawEllipse([System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(255, 66, 70, 78), 3), 9, 9, 202, 202)

    # ticks
    $tick = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(150, 180, 176, 164), 2)
    for ($i = 0; $i -lt 9; $i++) {
        $ang = (-225 + $i * 56.25) * [Math]::PI / 180.0
        $x1 = 110 + [Math]::Cos($ang) * 90
        $y1 = 110 + [Math]::Sin($ang) * 90
        $x2 = 110 + [Math]::Cos($ang) * 100
        $y2 = 110 + [Math]::Sin($ang) * 100
        $g.DrawLine($tick, [float]$x1, [float]$y1, [float]$x2, [float]$y2)
    }
    $tick.Dispose()

    $pathEll = [System.Drawing.Drawing2D.GraphicsPath]::new()
    $pathEll.AddEllipse(30, 30, 160, 160)
    $pg = [System.Drawing.Drawing2D.PathGradientBrush]::new($pathEll)
    $pg.CenterColor = [System.Drawing.Color]::FromArgb(255, 245, 240, 226)
    $pg.SurroundColors = [System.Drawing.Color[]]@([System.Drawing.Color]::FromArgb(255, 168, 158, 138))
    $pg.CenterPoint = [System.Drawing.PointF]::new(88, 82)
    $g.FillPath($pg, $pathEll)
    $pg.Dispose()
    $g.DrawPath([System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(255, 120, 112, 96), 2), $pathEll)
    $pathEll.Dispose()

    # ponteiro no topo (o app gira a textura conforme o valor)
    $amber = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 224, 146, 40))
    $g.FillRectangle($amber, 104, 48, 12, 52)
    $g.FillEllipse($amber, 99, 96, 22, 22)
    $g.FillEllipse([System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(90, 20, 20, 20)), 102, 100, 16, 16)
    $amber.Dispose()

    $g.Dispose()
    Save-Png $bmp $path
}

function New-Led([string]$path) {
    $w = 96; $h = 96
    $bmp = New-Canvas $w $h
    $g = New-Graphics $bmp
    for ($i = 9; $i -ge 1; $i--) {
        $a = [int](8 + (9 - $i) * 5)
        $r = 12 + $i * 3
        $g.FillEllipse([System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb($a, 255, 110, 60)),
            48 - $r, 48 - $r, $r * 2, $r * 2)
    }
    $ellipse = [System.Drawing.Drawing2D.GraphicsPath]::new()
    $ellipse.AddEllipse(18, 18, 60, 60)
    $pg = [System.Drawing.Drawing2D.PathGradientBrush]::new($ellipse)
    $pg.CenterColor = [System.Drawing.Color]::FromArgb(255, 255, 236, 190)
    $pg.SurroundColors = [System.Drawing.Color[]]@([System.Drawing.Color]::FromArgb(255, 214, 58, 34))
    $g.FillPath($pg, $ellipse)
    $pg.Dispose()
    $g.DrawPath([System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(220, 120, 24, 16), 3), $ellipse)
    $ellipse.Dispose()
    $g.FillEllipse([System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(220, 255, 255, 240)), 34, 30, 18, 14)
    $g.Dispose()
    Save-Png $bmp $path
}

function New-Footswitch([string]$path) {
    $w = 160; $h = 160
    $bmp = New-Canvas $w $h
    $g = New-Graphics $bmp
    $g.FillEllipse([System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(100, 0, 0, 0)), 12, 16, 136, 136)

    $outer = [System.Drawing.Drawing2D.GraphicsPath]::new()
    $outer.AddEllipse(8, 8, 144, 144)
    $pg = [System.Drawing.Drawing2D.PathGradientBrush]::new($outer)
    $pg.CenterColor = [System.Drawing.Color]::FromArgb(255, 214, 220, 228)
    $pg.SurroundColors = [System.Drawing.Color[]]@([System.Drawing.Color]::FromArgb(255, 92, 99, 110))
    $g.FillPath($pg, $outer)
    $pg.Dispose()
    $g.DrawPath([System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(255, 42, 46, 52), 3), $outer)
    $outer.Dispose()

    $inner = [System.Drawing.Drawing2D.GraphicsPath]::new()
    $inner.AddEllipse(26, 26, 108, 108)
    $pg2 = [System.Drawing.Drawing2D.PathGradientBrush]::new($inner)
    $pg2.CenterColor = [System.Drawing.Color]::FromArgb(255, 244, 246, 250)
    $pg2.SurroundColors = [System.Drawing.Color[]]@([System.Drawing.Color]::FromArgb(255, 128, 136, 148))
    $g.FillPath($pg2, $inner)
    $pg2.Dispose()
    $g.DrawPath([System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(255, 70, 76, 86), 2), $inner)
    $inner.Dispose()

    $g.FillEllipse([System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 48, 53, 61)), 58, 58, 44, 44)
    $g.FillEllipse([System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 22, 24, 28)), 66, 66, 28, 28)
    $g.FillEllipse([System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(140, 255, 255, 255)), 40, 34, 40, 18)
    $g.Dispose()
    Save-Png $bmp $path
}

function New-Label([string]$path, [string]$text, [System.Drawing.Color]$color) {
    $w = 600; $h = 100
    $bmp = New-Canvas $w $h
    $g = New-Graphics $bmp
    $font = [System.Drawing.Font]::new('Segoe UI', 44, [System.Drawing.FontStyle]::Bold,
        [System.Drawing.GraphicsUnit]::Pixel)
    $fmt = [System.Drawing.StringFormat]::new()
    $fmt.Alignment = [System.Drawing.StringAlignment]::Center
    $fmt.LineAlignment = [System.Drawing.StringAlignment]::Center
    $brush = [System.Drawing.SolidBrush]::new($color)
    $g.DrawString($text, $font, $brush, [System.Drawing.RectangleF]::new(0, -2, $w, 84), $fmt)
    $g.FillRectangle($brush, 150, 84, 300, 4)
    $brush.Dispose()
    $fmt.Dispose()
    $font.Dispose()
    $g.Dispose()
    Save-Png $bmp $path
}

function New-Kit([string]$folder, [System.Drawing.Color]$top, [System.Drawing.Color]$bottom,
                 [System.Drawing.Color]$accent, [string]$motif, [string]$label, [System.Drawing.Color]$labelColor) {
    New-Face (Join-Path $folder 'face.png') $top $bottom $accent $motif
    New-Label (Join-Path $folder 'label.png') $label $labelColor
}

$root = Split-Path -Parent $PSScriptRoot
$plug = Join-Path $root 'Plugins/capybara_drive_assets'
New-Kit $plug ([System.Drawing.Color]::FromArgb(58, 40, 27)) ([System.Drawing.Color]::FromArgb(18, 14, 11)) `
    ([System.Drawing.Color]::FromArgb(233, 161, 59)) 'drive' 'CAPYBARA DRIVE' ([System.Drawing.Color]::FromArgb(245, 235, 216))
New-Knob (Join-Path $plug 'knob.png')
New-Led (Join-Path $plug 'led.png')
New-Footswitch (Join-Path $plug 'footswitch.png')

$echo = Join-Path $root 'Demos/assets/golden_echo'
New-Kit $echo ([System.Drawing.Color]::FromArgb(26, 43, 54)) ([System.Drawing.Color]::FromArgb(9, 14, 17)) `
    ([System.Drawing.Color]::FromArgb(232, 190, 96)) 'echo' 'GOLDEN ECHO' ([System.Drawing.Color]::FromArgb(248, 231, 188))
New-Knob (Join-Path $echo 'knob.png')
New-Led (Join-Path $echo 'led.png')
New-Footswitch (Join-Path $echo 'footswitch.png')

$velvet = Join-Path $root 'Demos/assets/velvet_chorus'
New-Kit $velvet ([System.Drawing.Color]::FromArgb(45, 30, 56)) ([System.Drawing.Color]::FromArgb(15, 10, 18)) `
    ([System.Drawing.Color]::FromArgb(176, 126, 232)) 'velvet' 'VELVET CHORUS' ([System.Drawing.Color]::FromArgb(238, 224, 250))
New-Knob (Join-Path $velvet 'knob.png')
New-Led (Join-Path $velvet 'led.png')
New-Footswitch (Join-Path $velvet 'footswitch.png')

$tape = Join-Path $root 'Demos/assets/tape_warmth'
New-Kit $tape ([System.Drawing.Color]::FromArgb(57, 41, 29)) ([System.Drawing.Color]::FromArgb(18, 12, 9)) `
    ([System.Drawing.Color]::FromArgb(226, 142, 74)) 'tape' 'TAPE WARMTH' ([System.Drawing.Color]::FromArgb(246, 226, 205))
New-Knob (Join-Path $tape 'knob.png')
New-Led (Join-Path $tape 'led.png')
New-Footswitch (Join-Path $tape 'footswitch.png')

Write-Host 'Demo images written.'
$root = Split-Path -Parent $PSScriptRoot
$boss = Join-Path $root 'Demos/assets/boss'
$kits = @(
  @{name='md2'; top=@(72,18,18); bottom=@(18,6,6); accent=@(228,78,60); motif='md2'; label='MEGA DISTORTION'; labelColor=@(245,235,220)},
  @{name='ds1'; top=@(232,122,28); bottom=@(120,52,10); accent=@(255,196,120); motif='ds1'; label='DISTORTION'; labelColor=@(40,24,12)},
  @{name='sd1'; top=@(235,196,52); bottom=@(150,110,18); accent=@(255,244,190); motif='sd1'; label='SUPER OVERDRIVE'; labelColor=@(50,38,10)},
  @{name='bd2'; top=@(56,96,156); bottom=@(18,28,48); accent=@(170,210,255); motif='bd2'; label='BLUES DRIVER'; labelColor=@(235,242,255)},
  @{name='mt2'; top=@(58,60,64); bottom=@(18,18,20); accent=@(220,110,60); motif='mt2'; label='METAL ZONE'; labelColor=@(245,240,235)}
)
foreach($k in $kits){
  $folder = Join-Path $boss $k.name
  $top=[System.Drawing.Color]::FromArgb($k.top[0],$k.top[1],$k.top[2])
  $bottom=[System.Drawing.Color]::FromArgb($k.bottom[0],$k.bottom[1],$k.bottom[2])
  $accent=[System.Drawing.Color]::FromArgb($k.accent[0],$k.accent[1],$k.accent[2])
  $lc=[System.Drawing.Color]::FromArgb($k.labelColor[0],$k.labelColor[1],$k.labelColor[2])
  New-Kit $folder $top $bottom $accent $k.motif $k.label $lc
  New-Knob (Join-Path $folder 'knob.png')
  New-Led (Join-Path $folder 'led.png')
  New-Footswitch (Join-Path $folder 'footswitch.png')
}
Write-Host 'Boss images written.'
