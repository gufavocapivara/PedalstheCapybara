-- BOSS MD-2 Mega Distortion (inspired model)
-- Researched controls/features from boss.info/us/products/md-2/:
-- LEVEL, TONE/BOTTOM, DIST, GAIN BOOST.
-- BOSS describes: dual-stage distortion circuit with added gain boost,
-- bottom-heavy distortion for modern metal/hard rock.
-- Original DSP model inspired by documented controls; not schematic-accurate.

local p = np("BOSS MD-2 Mega Distortion")
p.instance(SLIDER, "level",       0.0, 1.0, 0.55)
p.instance(SLIDER, "tone_bottom", 0.0, 1.0, 0.45) -- 0=bass heavy, 1=brighter
p.instance(SLIDER, "dist",        0.0, 1.0, 0.65)
p.instance(SLIDER, "gain_boost",  0.0, 1.0, 0.45)
p.color(196, 56, 42)
p.linkbar("BOSS Distortion")
p.size(260, 340)
p.oversample(true)

local art = loadassets("boss_md2")
local face = art.loadimage("face.png")
local knob = art.loadimage("knob.png")
local led  = art.loadimage("led.png")
local foot = art.loadimage("footswitch.png")
if face ~= 0 then p.basetexture(face) end
if knob ~= 0 then p.knobtexture(knob) end
if led  ~= 0 then p.ledtexture(led) end
if foot ~= 0 then p.footswitchtexture(foot) end

local function clamp(x,a,b) if x<a then return a end if x>b then return b end return x end
local function db2lin(db) return 10^(db/20) end

local hp1, low1, lp1, lp2, dc = 0,0,0,0,0
local sr = 48000

p.prepare = function(rate) sr = rate; hp1,low1,lp1,lp2,dc=0,0,0,0,0 end
p.reset = function() hp1,low1,lp1,lp2,dc=0,0,0,0,0 end

p.process = function(x, n, level, toneBottom, dist, gainBoost)
  local d = dist * dist
  local boost = 1.0 + gainBoost * gainBoost * 35.0
  local g1 = (6.0 + d * 90.0) * boost
  local g2 = 3.0 + d * 35.0 + gainBoost * 12.0
  local out = db2lin(-20 + level * 26)

  local bottom = 1.0 - toneBottom
  local hpC = clamp(35 + toneBottom * 90, 20, 250) / sr
  local lowC = clamp(90 + bottom * 260, 60, 500) / sr
  local toneC = clamp(1600 + toneBottom * 5500, 900, 10000) / sr
  local fizzC = clamp(5000 + toneBottom * 2500, 3000, 9000) / sr
  local dcC = 20 / sr

  for i=1,n do
    local s = x[i]
    hp1 = hp1 + hpC * (s - hp1)
    s = s - hp1

    -- keep bottom weight before distortion
    low1 = low1 + lowC * (s - low1)
    local pre = s + low1 * (0.35 + bottom * 0.65)

    -- dual-stage distortion with gain boost
    local y = math.tanh(pre * g1)
    y = math.tanh(y * g2 + y*y*y * (0.12 + d * 0.25))

    -- tone/bottom post shaping
    lp1 = lp1 + toneC * (y - lp1)
    y = lp1 + (y - lp1) * (0.15 + toneBottom * 0.35)
    y = y + low1 * bottom * 0.12

    -- fizz control
    lp2 = lp2 + fizzC * (y - lp2)
    y = lp2 * 0.55 + y * 0.45

    dc = dc + dcC * (y - dc)
    y = y - dc

    x[i] = clamp(y * out, -1.0, 1.0)
  end
  return x
end
