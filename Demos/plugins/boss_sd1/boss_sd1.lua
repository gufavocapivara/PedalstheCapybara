-- BOSS SD-1 Super OverDrive (inspired model)
-- Researched controls from boss.info/us/products/sd-1/ and Wikipedia:
-- LEVEL, TONE, DRIVE; asymmetric overdrive, since 1981.
-- Not a schematic-accurate clone.

local p = np("BOSS SD-1 Super OverDrive")
p.instance(SLIDER, "level", 0.0, 1.0, 0.60)
p.instance(SLIDER, "tone",  0.0, 1.0, 0.55)
p.instance(SLIDER, "drive", 0.0, 1.0, 0.40)
p.color(235, 196, 52)
p.linkbar("BOSS Overdrive")
p.size(250, 330)
p.oversample(true)

local art = loadassets("boss_sd1")
local face = art.loadimage("face.png"); local knob = art.loadimage("knob.png")
local led  = art.loadimage("led.png");  local foot = art.loadimage("footswitch.png")
if face ~= 0 then p.basetexture(face) end
if knob ~= 0 then p.knobtexture(knob) end
if led  ~= 0 then p.ledtexture(led) end
if foot ~= 0 then p.footswitchtexture(foot) end

local function clamp(x,a,b) if x<a then return a end if x>b then return b end return x end
local hp, lp, toneLP = 0,0,0
local sr = 48000
p.prepare = function(rate) sr=rate; hp,lp,toneLP=0,0,0 end
p.reset = function() hp,lp,toneLP=0,0,0 end

p.process = function(x,n,level,tone,drive)
  local d = drive*drive
  local g = 2.0 + d*40.0
  local asym = 0.08 + d*0.18
  local out = 10^((-18 + level*22)/20)
  local hpC = 120/sr
  local lpC = clamp(900 + tone*7000, 800, 10000)/sr
  for i=1,n do
    local s=x[i]
    hp = hp + hpC*(s-hp); s = s-hp
    -- asymmetric soft clip
    local y = (s + asym) * g
    if y >= 0 then y = math.tanh(y) else y = math.tanh(y*1.35) * 0.85 end
    toneLP = toneLP + lpC*(y-toneLP)
    y = toneLP
    lp = lp + clamp(7000/sr,0.01,0.45)*(y-lp)
    y = lp*0.25 + y*0.75
    x[i] = clamp(y*out, -1.1, 1.1)
  end
  return x
end
