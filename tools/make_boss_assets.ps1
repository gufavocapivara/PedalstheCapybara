$root = Split-Path -Parent $PSScriptRoot
$boss = Join-Path $root 'Demos/assets/boss'
$kits = @(
  @{name='md2'; top=@(72,18,18); bottom=@(18,6,6); accent=@(228,78,60); motif='md2'; label='MEGA DISTORTION'; labelColor=@(245,235,220)},
  @{name='ds1'; top=@(232,122,28); bottom=@(120,52,10); accent=@(255,196,120); motif='ds1'; label='DISTORTION'; labelColor=@(40,24,12)},
  @{name='sd1'; top=@(235,196,52); bottom=@(150,110,18); accent=@(255,244,190); motif='sd1'; label='SUPER OVERDRIVE'; labelColor=@(50,38,10)},
  @{name='bd2'; top=@(56,96,156); bottom=@(18,28,48); accent=@(170,210,255); motif='bd2'; label='BLUES DRIVER'; labelColor=@(235,242,255)},
  @{name='mt2'; top=@(58,60,64); bottom=@(18,18,20); accent=@(220,110,60); motif='mt2'; label='METAL ZONE'; labelColor=@(245,240,235)},
  @{name='comfortably_numb_solo'; top=@(46,54,68); bottom=@(16,18,24); accent=@(170,196,255); motif='echo'; label='COMFORTABLY NUMB'; labelColor=@(236,242,255)}
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
Write-Host 'Boss+CN images written.'
