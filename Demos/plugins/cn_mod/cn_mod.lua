-- CN Rotary/Chorus (Inspired)
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
