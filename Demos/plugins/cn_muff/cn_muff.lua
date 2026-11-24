-- CN Big Muff Sustain (Inspired) - original DSP, not circuit clone
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
