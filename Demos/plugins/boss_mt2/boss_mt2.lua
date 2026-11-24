-- BOSS MT-2 Metal Zone (inspired model)
-- Researched controls from boss.info/us/products/mt-2/ and Wikipedia:
-- LEVEL, DIST, HIGH, LOW, MID, MID FREQ. Dual-stage gain + 3-band EQ with semi-parametric mid.
-- Original DSP model inspired by documented controls; not schematic-accurate.

local p = np("BOSS MT-2 Metal Zone")
p.instance(SLIDER, "level",    0.0, 1.0, 0.50)
p.instance(SLIDER, "dist",     0.0, 1.0, 0.70)
p.instance(SLIDER, "high",     0.0, 1.0, 0.55)
p.instance(SLIDER, "low",      0.0, 1.0, 0.55)
p.instance(SLIDER, "mid",      0.0, 1.0, 0.45)
p.instance(SLIDER, "mid_freq", 0.0, 1.0, 0.45) -- 200Hz..5kHz
p.color(72, 74, 78)
p.linkbar("BOSS Distortion")
p.size(280, 360)
p.oversample(true)

local art = loadassets("boss_mt2")
local face = art.loadimage("face.png"); local knob = art.loadimage("knob.png")
local led  = art.loadimage("led.png");  local foot = art.loadimage("footswitch.png")
if face ~= 0 then p.basetexture(face) end
if knob ~= 0 then p.knobtexture(knob) end
if led  ~= 0 then p.ledtexture(led) end
if foot ~= 0 then p.footswitchtexture(foot) end

local function clamp(x,a,b) if x<a then return a end if x>b then return b end return x end
local function db2lin(db) return 10^(db/20) end

local hp, lowS, midBP, highLP, postLP, dc = 0,0,0,0,0,0
local sr = 48000
p.prepare=function(rate) sr=rate; hp,lowS,midBP,highLP,postLP,dc=0,0,0,0,0,0 end
p.reset=function() hp,lowS,midBP,highLP,postLP,dc=0,0,0,0,0,0 end

p.process=function(x,n,level,dist,high,low,mid,midFreq)
  local d=dist*dist
  local g1=10 + d*140
  local g2=6 + d*90
  local out=db2lin(-28 + level*30)

  local hpC=60/sr
  local lowC=120/sr
  local highC=3500/sr
  local midHz=200 * (25^(midFreq)) -- 200..5000
  local midC=clamp(midHz/sr,0.001,0.45)
  local midQ = 0.85 - mid*0.15
  local lowGain=db2lin((low-0.5)*30)
  local highGain=db2lin((high-0.5)*30)
  local midGain=db2lin((mid-0.5)*30)
  local postC=clamp(8000/sr,0.01,0.45)
  local dcC=20/sr

  for i=1,n do
    local s=x[i]
    hp = hp + hpC*(s-hp); s=s-hp

    -- tight pre-shape before high gain
    local preLow = lowS + lowC*(s-lowS)
    local pre = s - preLow*0.25

    local y = math.tanh(pre*g1)
    y = math.tanh(y*g2 + y*y*y*0.25*d)

    -- EQ: low shelf-ish, high shelf-ish, semi-parametric mid
    lowS = lowS + lowC*(y-lowS)
    local lowBand = lowS
    highLP = highLP + highC*(y-highLP)
    local highBand = y-highLP

    -- simple resonant band-pass for mid
    midBP = midBP + midC*((y - midBP) - midBP*midQ)
    local midBand = midBP

    y = y + lowBand*(lowGain-1)*0.8 + highBand*(highGain-1)*0.8 + midBand*(midGain-1)*1.2

    postLP = postLP + postC*(y-postLP)
    y = postLP*0.4 + y*0.6

    dc = dc + dcC*(y-dc)
    y = y-dc
    x[i]=clamp(y*out, -1.0, 1.0)
  end
  return x
end
