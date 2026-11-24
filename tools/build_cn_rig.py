from pathlib import Path
import shutil, zipfile

root = Path(r"D:\ProjectVSComunity\Pedals the capybara\Pedals the capybara")
assets = root / 'Demos/assets/cn_rig'

plugins = [
    {
        'dir': 'cn_comp',
        'script': 'cn_comp.lua',
        'name': 'CN Comp (Inspired)',
        'desc': 'Compressor inspirado para sustain limpo antes do drive. Controles: Sustain, Level, Attack, Blend. DSP original, não afiliado.',
        'color': '120,150,190',
    },
    {
        'dir': 'cn_muff',
        'script': 'cn_muff.lua',
        'name': 'CN Big Muff Sustain (Inspired)',
        'desc': 'Fuzz/sustain inspirado no caráter Big Muff usado em leads tipo Comfortably Numb. Controles: Sustain, Tone, Level. DSP original, não clone de circuito.',
        'color': '170,90,150',
    },
    {
        'dir': 'cn_mod',
        'script': 'cn_mod.lua',
        'name': 'CN Rotary/Chorus (Inspired)',
        'desc': 'Modulação suave para largura e movimento. Controles: Rate, Depth, Mix, Tone. DSP original.',
        'color': '90,150,190',
    },
    {
        'dir': 'cn_delay',
        'script': 'cn_delay.lua',
        'name': 'CN Lead Delay (Inspired)',
        'desc': 'Delay curto para solo. Controles: Time, Feedback, Mix, Tone. DSP original.',
        'color': '110,150,110',
    },
    {
        'dir': 'cn_amp',
        'script': 'cn_amp.lua',
        'name': 'CN Hiwatt Lead Amp (Inspired)',
        'desc': 'Amp inspirado em clean headroom com drive de lead. Controles: Gain, Bass, Mid, Treble, Presence, Master. DSP original, não clone de circuito.',
        'color': '120,128,150',
        'amp': True,
    },
]

scripts = {}

scripts['cn_comp.lua'] = r'''-- CN Comp (Inspired) - compressor for smooth sustain
local p = np("CN Comp (Inspired)")
p.instance(SLIDER, "sustain", 0.0, 1.0, 0.55)
p.instance(SLIDER, "level",   0.0, 1.0, 0.60)
p.instance(SLIDER, "attack",  0.0, 1.0, 0.35)
p.instance(SLIDER, "blend",   0.0, 1.0, 0.75)
p.color(120,150,190); p.linkbar("Comfortably Numb"); p.size(250,330); p.oversample(false)
local art=loadassets("cn_comp")
local face=art.loadimage("comp_face.png"); local knob=art.loadimage("comp_knob.png")
local led=art.loadimage("comp_led.png"); local foot=art.loadimage("comp_footswitch.png")
if face~=0 then p.basetexture(face) end
if knob~=0 then p.knobtexture(knob) end
if led~=0 then p.ledtexture(led) end
if foot~=0 then p.footswitchtexture(foot) end
local function clamp(x,a,b) if x<a then return a end if x>b then return b end return x end
local function db2lin(db) return 10^(db/20) end
local env=0; local sr=48000
p.prepare=function(rate) sr=rate; env=0 end
p.reset=function() env=0 end
p.process=function(x,n,sustain,level,attack,blend)
  local thr=db2lin(-28 + (1-sustain)*18)
  local ratio=2 + sustain*6
  local atk=math.exp(-1/(sr*(0.001+attack*0.05)))
  local rel=math.exp(-1/(sr*0.12))
  local makeup=db2lin(2 + sustain*10) * db2lin(-12 + level*18)
  for i=1,n do
    local s=x[i]
    local a=math.abs(s)
    if a>env then env=atk*env+(1-atk)*a else env=rel*env+(1-rel)*a end
    local gain=1.0
    if env>thr then
      local over=db2lin(20*math.log10(env/thr+1e-12))
      local comp=db2lin(20*math.log10(env/thr+1e-12)/ratio)
      gain=comp/(over+1e-12)
    end
    local y=s*gain*makeup
    y=s*(1-blend)+y*blend
    x[i]=clamp(y,-1.2,1.2)
  end
  return x
end
'''

scripts['cn_muff.lua'] = r'''-- CN Big Muff Sustain (Inspired) - original DSP, not circuit clone
local p = np("CN Big Muff Sustain (Inspired)")
p.instance(SLIDER, "sustain", 0.0, 1.0, 0.72)
p.instance(SLIDER, "tone",    0.0, 1.0, 0.58)
p.instance(SLIDER, "level",   0.0, 1.0, 0.55)
p.color(170,90,150); p.linkbar("Comfortably Numb"); p.size(250,330); p.oversample(true)
local art=loadassets("cn_muff")
local face=art.loadimage("muff_face.png"); local knob=art.loadimage("muff_knob.png")
local led=art.loadimage("muff_led.png"); local foot=art.loadimage("muff_footswitch.png")
if face~=0 then p.basetexture(face) end
if knob~=0 then p.knobtexture(knob) end
if led~=0 then p.ledtexture(led) end
if foot~=0 then p.footswitchtexture(foot) end
local function clamp(x,a,b) if x<a then return a end if x>b then return b end return x end
local function db2lin(db) return 10^(db/20) end
local hp, lp1, lp2, toneLP, toneHP, dc = 0,0,0,0,0,0
local sr=48000
p.prepare=function(rate) sr=rate; hp,lp1,lp2,toneLP,toneHP,dc=0,0,0,0,0,0 end
p.reset=function() hp,lp1,lp2,toneLP,toneHP,dc=0,0,0,0,0,0 end
p.process=function(x,n,sustain,tone,level)
  local d=sustain*sustain
  local g1=8 + d*120
  local g2=5 + d*80
  local out=db2lin(-24 + level*28)
  local hpC=40/sr
  local preLP=clamp(6000/sr,0.01,0.45)
  local toneC=clamp((900 + tone*5000)/sr,0.01,0.45)
  local dcC=20/sr
  for i=1,n do
    local s=x[i]
    hp = hp + hpC*(s-hp); s=s-hp
    lp1 = lp1 + preLP*(s-lp1); s = lp1*0.3 + s*0.7
    -- sustain stages
    local y = math.tanh(s*g1)
    y = math.tanh(y*g2 + y*y*y*0.2*d)
    -- muff-like tone: blend lowpass/highpass-ish contour
    toneLP = toneLP + toneC*(y-toneLP)
    toneHP = toneHP + (180/sr)*(y-toneHP)
    local hi = y-toneHP
    y = toneLP*(0.9-tone*0.4) + hi*(0.25 + tone*0.9)
    lp2 = lp2 + clamp(7000/sr,0.01,0.45)*(y-lp2)
    y = lp2*0.4 + y*0.6
    dc = dc + dcC*(y-dc); y=y-dc
    x[i]=clamp(y*out,-1.0,1.0)
  end
  return x
end
'''

scripts['cn_mod.lua'] = r'''-- CN Rotary/Chorus (Inspired)
local p = np("CN Rotary/Chorus (Inspired)")
p.instance(SLIDER, "rate",  0.0, 1.0, 0.28)
p.instance(SLIDER, "depth", 0.0, 1.0, 0.35)
p.instance(SLIDER, "mix",   0.0, 1.0, 0.30)
p.instance(SLIDER, "tone",  0.0, 1.0, 0.60)
p.color(90,150,190); p.linkbar("Comfortably Numb"); p.size(250,330); p.oversample(false)
local art=loadassets("cn_mod")
local face=art.loadimage("mod_face.png"); local knob=art.loadimage("mod_knob.png")
local led=art.loadimage("mod_led.png"); local foot=art.loadimage("mod_footswitch.png")
if face~=0 then p.basetexture(face) end
if knob~=0 then p.knobtexture(knob) end
if led~=0 then p.ledtexture(led) end
if foot~=0 then p.footswitchtexture(foot) end
local function clamp(x,a,b) if x<a then return a end if x>b then return b end return x end
local sr=48000; local buf, bmax, pos, ph, lp = {},1,1,0,0
p.prepare=function(rate)
  sr=rate; bmax=math.max(2, math.floor(sr*0.05)); buf={}
  for i=1,bmax do buf[i]=0 end; pos=1; ph=0; lp=0
end
p.reset=function() for i=1,bmax do buf[i]=0 end; pos=1; ph=0; lp=0 end
p.process=function(x,n,rate,depth,mix,tone)
  local hz=0.1 + rate*4.0
  local dSamp=(0.002 + depth*0.010)*sr
  local toneC=clamp((1500 + tone*6000)/sr,0.01,0.45)
  for i=1,n do
    local s=x[i]
    buf[pos]=s
    ph = ph + hz/sr; if ph>=1 then ph=ph-1 end
    local lfo=(math.sin(ph*math.pi*2)+1)*0.5
    local d=dSamp*(0.2+0.8*lfo)
    local rp=pos-d; while rp<1 do rp=rp+bmax end
    local i0=math.floor(rp); local frac=rp-i0; local i1=i0+1; if i1>bmax then i1=1 end
    local wet=buf[i0]*(1-frac)+buf[i1]*frac
    lp = lp + toneC*(wet-lp); wet=lp
    x[i]=clamp(s*(1-mix)+wet*mix,-1.2,1.2)
    pos=pos+1; if pos>bmax then pos=1 end
  end
  return x
end
'''

scripts['cn_delay.lua'] = r'''-- CN Lead Delay (Inspired)
local p = np("CN Lead Delay (Inspired)")
p.instance(SLIDER, "time",     0.0, 1.0, 0.54) -- ~380ms default
p.instance(SLIDER, "feedback", 0.0, 1.0, 0.28)
p.instance(SLIDER, "mix",      0.0, 1.0, 0.22)
p.instance(SLIDER, "tone",     0.0, 1.0, 0.55)
p.color(110,150,110); p.linkbar("Comfortably Numb"); p.size(250,330); p.oversample(false)
local art=loadassets("cn_delay")
local face=art.loadimage("delay_face.png"); local knob=art.loadimage("delay_knob.png")
local led=art.loadimage("delay_led.png"); local foot=art.loadimage("delay_footswitch.png")
if face~=0 then p.basetexture(face) end
if knob~=0 then p.knobtexture(knob) end
if led~=0 then p.ledtexture(led) end
if foot~=0 then p.footswitchtexture(foot) end
local function clamp(x,a,b) if x<a then return a end if x>b then return b end return x end
local sr=48000; local buf,bmax,pos,lp= {},1,1,0
p.prepare=function(rate)
  sr=rate; bmax=math.max(2, math.floor(sr*1.2)); buf={}
  for i=1,bmax do buf[i]=0 end; pos=1; lp=0
end
p.reset=function() for i=1,bmax do buf[i]=0 end; pos=1; lp=0 end
p.process=function(x,n,time,feedback,mix,tone)
  local dt=math.floor((0.05 + time*0.75)*sr)
  local fb=clamp(feedback*0.75,0,0.85)
  local toneC=clamp((1200 + tone*5000)/sr,0.01,0.45)
  for i=1,n do
    local s=x[i]
    local rp=pos-dt; if rp<1 then rp=rp+bmax end
    local dly=buf[rp] or 0
    lp = lp + toneC*(dly-lp); dly=lp
    buf[pos]=clamp(s + dly*fb, -1.5, 1.5)
    x[i]=clamp(s*(1-mix)+dly*mix,-1.2,1.2)
    pos=pos+1; if pos>bmax then pos=1 end
  end
  return x
end
'''

scripts['cn_amp.lua'] = r'''-- CN Hiwatt Lead Amp (Inspired) - original DSP, not circuit clone
local a = newamp("CN Hiwatt Lead Amp (Inspired)")
a.instance(SLIDER, "gain",     0.0, 1.0, 0.45)
a.instance(SLIDER, "bass",     0.0, 1.0, 0.45)
a.instance(SLIDER, "mid",      0.0, 1.0, 0.60)
a.instance(SLIDER, "treble",   0.0, 1.0, 0.65)
a.instance(SLIDER, "presence", 0.0, 1.0, 0.52)
a.instance(SLIDER, "master",   0.0, 1.0, 0.62)
a.color(120,128,150); a.linkbar("Comfortably Numb"); a.size(320,360); a.oversample(true)
local art=loadassets("cn_amp")
local face=art.loadimage("amp_face.png"); local knob=art.loadimage("amp_knob.png")
local led=art.loadimage("amp_led.png"); local foot=art.loadimage("amp_footswitch.png")
if face~=0 then a.basetexture(face) end
if knob~=0 then a.knobtexture(knob) end
if led~=0 then a.ledtexture(led) end
if foot~=0 then a.footswitchtexture(foot) end
local function clamp(x,lo,hi) if x<lo then return lo end if x>hi then return hi end return x end
local function db2lin(db) return 10^(db/20) end
local sr=48000
local hp,lowS,midBP,highLP,presLP,dc = 0,0,0,0,0,0
a.prepare=function(rate) sr=rate; hp,lowS,midBP,highLP,presLP,dc=0,0,0,0,0,0 end
a.reset=function() hp,lowS,midBP,highLP,presLP,dc=0,0,0,0,0,0 end
a.process=function(x,n,gain,bass,mid,treble,presence,master)
  local g = 1.2 + gain*gain*16.0
  local out = db2lin(-24 + master*28)
  local hpC=60/sr
  local lowC=120/sr
  local midC=900/sr
  local highC=3200/sr
  local presC=5500/sr
  local dcC=20/sr
  local bassG=db2lin((bass-0.5)*12)
  local midG=db2lin((mid-0.5)*14)
  local trebG=db2lin((treble-0.5)*14)
  local presG=db2lin((presence-0.5)*10)
  for i=1,n do
    local s=x[i]
    hp = hp + hpC*(s-hp); s=s-hp
    -- preamp: clean headroom then smooth lead breakup
    local y = math.tanh(s*g)
    y = math.tanh(y*1.4 + y*y*y*0.08*gain)
    -- EQ
    lowS = lowS + lowC*(y-lowS); local lowBand=lowS
    midBP = midBP + midC*((y-midBP)-midBP*0.72); local midBand=midBP
    highLP = highLP + highC*(y-highLP); local highBand=y-highLP
    presLP = presLP + presC*(y-presLP); local presBand=y-presLP
    y = y + lowBand*(bassG-1)*0.7 + midBand*(midG-1)*1.0 + highBand*(trebG-1)*0.7 + presBand*(presG-1)*0.45
    dc = dc + dcC*(y-dc); y=y-dc
    x[i]=clamp(y*out,-1.0,1.0)
  end
  return x
end
'''

pack_dir = root / 'Demos/plugins/ComfortablyNumbRig'
if pack_dir.exists():
    shutil.rmtree(pack_dir)
pack_dir.mkdir(parents=True)

for meta in plugins:
    pdir = root / 'Demos/plugins' / meta['dir']
    adir = pdir / 'assets'
    if adir.exists():
        shutil.rmtree(adir)
    adir.mkdir(parents=True, exist_ok=True)
    # copy assets by prefix
    prefix = meta['dir'].split('_',1)[1]  # comp/muff/mod/delay/amp
    mapping = {
        f'{prefix}_face.png': 'face.png',
        f'{prefix}_knob.png': 'knob.png',
        f'{prefix}_led.png': 'led.png',
        f'{prefix}_footswitch.png': 'footswitch.png',
    }
    for src_name, dst_name in mapping.items():
        src = assets / src_name
        if src.exists():
            shutil.copy2(src, adir / dst_name)
    # write script
    (pdir / meta['script']).write_text(scripts[meta['script']], encoding='utf-8')
    ptc = (
        f"NAME={meta['name']}\n"
        f"DESCRIPTION={meta['desc']}\n"
        "AUTHOR=Pedals the Capybara\n"
        "VERSION=1.0\n"
        f"SCRIPT={meta['script']}\n"
        "ICON=assets/face.png\n"
    )
    (pdir / 'plugin.ptc').write_text(ptc, encoding='utf-8')
    pctp = root / 'Demos' / f"{meta['dir']}.pctp"
    if pctp.exists(): pctp.unlink()
    with zipfile.ZipFile(pctp, 'w', zipfile.ZIP_DEFLATED) as z:
        z.writestr('plugin.ptc', ptc)
        z.writestr(meta['script'], scripts[meta['script']])
        for f in ['face.png','knob.png','led.png','footswitch.png']:
            fp = adir/f
            if fp.exists(): z.write(fp, f'assets/{f}')
    shutil.copy2(pctp, pack_dir / pctp.name)

(pack_dir / 'pack.ini').write_text(
    "NAME=Comfortably Numb Rig (Inspired)\n"
    "DESCRIPTION=Rig inspirado para solo de Comfortably Numb: comp, sustain/fuzz, modulação, delay e amp. DSP original; não afiliado a Pink Floyd/BOSS/EHX/Hiwatt.\n"
    "AUTHOR=Pedals the Capybara\n"
    "VERSION=1.0\n",
    encoding='utf-8'
)

pp = root / 'Packs/ComfortablyNumbRig.pp'
if pp.exists(): pp.unlink()
with zipfile.ZipFile(pp, 'w', zipfile.ZIP_DEFLATED) as z:
    for path in sorted(pack_dir.rglob('*')):
        if path.is_file():
            z.write(path, path.relative_to(pack_dir).as_posix())

# also update old single pack for convenience
old_pp = root / 'Packs/ComfortablyNumbSolo.pp'
if old_pp.exists(): old_pp.unlink()
shutil.copy2(pp, old_pp)
print('ok', pp)
