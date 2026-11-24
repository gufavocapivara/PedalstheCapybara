-- CN Lead Delay (Inspired)
local p = np("CN Lead Delay (Inspired)")
p.instance(SLIDER, "time",     0.0, 1.0, 0.54) -- ~380ms default
p.instance(SLIDER, "feedback", 0.0, 1.0, 0.28)
p.instance(SLIDER, "mix",      0.0, 1.0, 0.22)
p.instance(SLIDER, "tone",     0.0, 1.0, 0.55)
p.color(110,150,110); p.linkbar("Comfortably Numb"); p.size(250,330); p.oversample(false)
local art=loadassets("cn_delay")
local face=art.loadimage("delay_face.png"); local knob=art.loadimage("delay_knob.png")
local led=art.loadimage("delay_led.png"); local foot=art.loadimage("delay_footswitch.png")
if face~=0 then p.basetexture(face) end
if knob~=0 then p.knobtexture(knob) end
if led~=0 then p.ledtexture(led) end
if foot~=0 then p.footswitchtexture(foot) end
local function clamp(x,a,b) if x<a then return a end if x>b then return b end return x end
local sr=48000; local buf,bmax,pos,lp= {},1,1,0
p.prepare=function(rate)
  sr=rate; bmax=math.max(2, math.floor(sr*1.2)); buf={}
  for i=1,bmax do buf[i]=0 end; pos=1; lp=0
end
p.reset=function() for i=1,bmax do buf[i]=0 end; pos=1; lp=0 end
p.process=function(x,n,time,feedback,mix,tone)
  local dt=math.floor((0.05 + time*0.75)*sr)
  local fb=clamp(feedback*0.75,0,0.85)
  local toneC=clamp((1200 + tone*5000)/sr,0.01,0.45)
  for i=1,n do
    local s=x[i]
    local rp=pos-dt; if rp<1 then rp=rp+bmax end
    local dly=buf[rp] or 0
    lp = lp + toneC*(dly-lp); dly=lp
    buf[pos]=clamp(s + dly*fb, -1.5, 1.5)
    x[i]=clamp(s*(1-mix)+dly*mix,-1.2,1.2)
    pos=pos+1; if pos>bmax then pos=1 end
  end
  return x
end
