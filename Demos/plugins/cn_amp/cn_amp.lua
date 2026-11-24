-- CN Hiwatt Lead Amp (Inspired) - original DSP, not circuit clone
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
