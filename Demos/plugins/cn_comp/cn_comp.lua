-- CN Comp (Inspired) - compressor for smooth sustain
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
