-- BOSS DS-1 Distortion (inspired model)
-- Researched controls/features from boss.info/us/products/ds-1/ and Wikipedia:
-- LEVEL, TONE, DIST; hard-edged attack, classic distortion, introduced 1978.
-- Not a schematic-accurate clone.

local p = np("BOSS DS-1 Distortion")
p.instance(SLIDER, "level", 0.0, 1.0, 0.55)
p.instance(SLIDER, "tone",  0.0, 1.0, 0.50)
p.instance(SLIDER, "dist",  0.0, 1.0, 0.55)
p.color(232, 122, 28)
p.linkbar("BOSS Distortion")
p.size(250, 330)
p.oversample(true)

local art = loadassets("boss_ds1")
local face = art.loadimage("face.png"); local knob = art.loadimage("knob.png")
local led  = art.loadimage("led.png");  local foot = art.loadimage("footswitch.png")
if face ~= 0 then p.basetexture(face) end
if knob ~= 0 then p.knobtexture(knob) end
if led  ~= 0 then p.ledtexture(led) end
if foot ~= 0 then p.footswitchtexture(foot) end

local function clamp(x,a,b) if x<a then return a end if x>b then return b end return x end
local preHP, preLP, postLP, postHP = 0,0,0,0
local sr = 48000

p.prepare = function(rate) sr = rate; preHP,preLP,postLP,postHP=0,0,0,0 end
p.reset = function() preHP,preLP,postLP,postHP=0,0,0,0 end

p.process = function(x, n, level, tone, dist)
  local g = 3 + dist*dist*120
  local out = 10^((-20 + level*24)/20)
  local hpC = 90/sr
  local lpC = clamp(2000 + tone*9000, 1200, 14000)/sr
  local postHpC = 60/sr
  for i=1,n do
    local s = x[i]
    preHP = preHP + hpC*(s-preHP); s = s-preHP
    preLP = preLP + clamp(8000/sr,0.01,0.45)*(s-preLP); s = preLP*0.3 + s*0.7
    -- hard-ish clipping character
    local y = s*g
    y = clamp(y, -1.5, 1.5)
    y = math.tanh(y * 1.2)
    postLP = postLP + lpC*(y-postLP)
    y = postLP
    postHP = postHP + postHpC*(y-postHP)
    y = y - postHP*0.15
    x[i] = clamp(y*out, -1.1, 1.1)
  end
  return x
end
