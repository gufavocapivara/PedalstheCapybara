-- BOSS BD-2 Blues Driver (inspired model)
-- Researched controls from boss.info/us/products/bd-2/: LEVEL, TONE, GAIN.
-- BOSS describes touch-responsive gain, amp-like circuit, clean boost to overdrive.
-- Not a schematic-accurate clone.

local p = np("BOSS BD-2 Blues Driver")
p.instance(SLIDER, "level", 0.0, 1.0, 0.62)
p.instance(SLIDER, "tone",  0.0, 1.0, 0.55)
p.instance(SLIDER, "gain",  0.0, 1.0, 0.38)
p.color(74, 126, 196)
p.linkbar("BOSS Overdrive")
p.size(250, 330)
p.oversample(true)

local art = loadassets("boss_bd2")
local face = art.loadimage("face.png"); local knob = art.loadimage("knob.png")
local led  = art.loadimage("led.png");  local foot = art.loadimage("footswitch.png")
if face ~= 0 then p.basetexture(face) end
if knob ~= 0 then p.knobtexture(knob) end
if led  ~= 0 then p.ledtexture(led) end
if foot ~= 0 then p.footswitchtexture(foot) end

local function clamp(x,a,b) if x<a then return a end if x>b then return b end return x end
local hp, low, hi, lp = 0,0,0,0
local sr=48000
p.prepare=function(rate) sr=rate; hp,low,hi,lp=0,0,0,0 end
p.reset=function() hp,low,hi,lp=0,0,0,0 end

p.process=function(x,n,level,tone,gain)
  local d=gain*gain
  local g1=1.5 + d*18.0
  local g2=1.2 + d*8.0
  local out=10^((-16 + level*20)/20)
  local hpC=70/sr
  local lpC=clamp(2200 + tone*7500, 1500, 12000)/sr
  for i=1,n do
    local s=x[i]
    hp = hp + hpC*(s-hp); s=s-hp
    -- amp-like: softer first stage, more breakup second
    local y = math.tanh(s*g1)
    y = math.tanh(y*g2 + y*0.25*d)
    -- keep dynamics a bit
    y = y*(0.85 + 0.15*(1-d))
    low = low + (180/sr)*(y-low)
    hi = y-low
    y = low*0.95 + hi*(0.8 + tone*0.6)
    lp = lp + lpC*(y-lp)
    x[i]=clamp(lp*out,-1.1,1.1)
  end
  return x
end
