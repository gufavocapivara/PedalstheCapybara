-- Comfortably Numb Solo (inspired Pink Floyd tone chain)
-- Original preset/model for the app. Not affiliated with Pink Floyd/BOSS.
-- Designed as an amp-style block in the same pedalboard chain.

local a = newamp("Comfortably Numb Solo (Inspired)")
a.instance(SLIDER, "gain",      0.0, 1.0, 0.58)
a.instance(SLIDER, "bass",      0.0, 1.0, 0.45)
a.instance(SLIDER, "mid",       0.0, 1.0, 0.62)
a.instance(SLIDER, "treble",    0.0, 1.0, 0.68)
a.instance(SLIDER, "presence",  0.0, 1.0, 0.55)
a.instance(SLIDER, "delay_mix", 0.0, 1.0, 0.18)
a.instance(SLIDER, "level",     0.0, 1.0, 0.62)
a.color(112, 128, 156)
a.linkbar("Pink Floyd")
a.size(320, 360)
a.oversample(true)

local art = loadassets("comfortably_numb_solo")
local face = art.loadimage("face.png")
local knob = art.loadimage("knob.png")
local led  = art.loadimage("led.png")
local foot = art.loadimage("footswitch.png")
if face ~= 0 then a.basetexture(face) end
if knob ~= 0 then a.knobtexture(knob) end
if led  ~= 0 then a.ledtexture(led) end
if foot ~= 0 then a.footswitchtexture(foot) end

local function clamp(x,lo,hi) if x<lo then return lo end if x>hi then return hi end return x end
local function db2lin(db) return 10^(db/20) end

local sr = 48000
local hp, lowS, midBP, highLP, presLP, dc = 0,0,0,0,0,0
local dly, dpos, dmax = {}, 1, 1

a.prepare = function(rate)
  sr = rate
  hp,lowS,midBP,highLP,presLP,dc = 0,0,0,0,0,0
  dmax = math.max(1, math.floor(sr * 0.7))
  dly = {}
  for i=1,dmax do dly[i] = 0 end
  dpos = 1
end
a.reset = function()
  hp,lowS,midBP,highLP,presLP,dc = 0,0,0,0,0,0
  for i=1,dmax do dly[i]=0 end
  dpos = 1
end

a.process = function(x, n, gain, bass, mid, treble, presence, delayMix, level)
  local g = 1.5 + gain*gain * 18.0
  local out = db2lin(-22 + level*28)
  local hpC = 70/sr
  local lowC = 120/sr
  local midC = 850/sr
  local highC = 3200/sr
  local presC = 5500/sr
  local dcC = 20/sr
  local bassG = db2lin((bass-0.5)*12)
  local midG = db2lin((mid-0.5)*14)
  local trebG = db2lin((treble-0.5)*14)
  local presG = db2lin((presence-0.5)*10)
  local dt = math.floor(sr * 0.38)
  local fb = 0.28

  for i=1,n do
    local s = x[i]
    hp = hp + hpC*(s-hp); s=s-hp

    -- smooth lead drive
    local y = math.tanh(s*g)
    y = math.tanh(y*1.6 + y*y*y*0.15*gain)

    -- amp-ish EQ
    lowS = lowS + lowC*(y-lowS)
    local lowBand = lowS
    midBP = midBP + midC*((y-midBP) - midBP*0.75)
    local midBand = midBP
    highLP = highLP + highC*(y-highLP)
    local highBand = y-highLP
    presLP = presLP + presC*(y-presLP)
    local presBand = y-presLP

    y = y + lowBand*(bassG-1)*0.7 + midBand*(midG-1)*1.0 + highBand*(trebG-1)*0.7 + presBand*(presG-1)*0.5

    -- short lead delay
    local rp = dpos - dt
    if rp < 1 then rp = rp + dmax end
    local delayed = dly[rp] or 0
    dly[dpos] = y + delayed*fb
    y = y*(1-delayMix) + delayed*delayMix
    dpos = dpos + 1
    if dpos > dmax then dpos = 1 end

    dc = dc + dcC*(y-dc)
    y = y-dc
    x[i] = clamp(y*out, -1.0, 1.0)
  end
  return x
end
