-- Golden Echo: demo delay plugin with images, packed as .pctp/.pp.
local function main()
    local art = loadassets("golden_echo")
    local face = art.loadimage("face.png")
    local knob = art.loadimage("knob.png")
    local led  = art.loadimage("led.png")
    local foot = art.loadimage("footswitch.png")

    local p = np("Golden Echo")
    p.instance(SLIDER, "time", 60, 900, 320)
    p.instance(SLIDER, "feedback", 0, 0.85, 0.35)
    p.instance(SLIDER, "mix", 0, 1, 0.35)
    p.color(232, 190, 96)
    p.linkbar("Time/Space")
    p.size(250, 330)
    p.oversample(true)
    if face ~= 0 then p.basetexture(face) end
    if knob ~= 0 then p.knobtexture(knob) end
    if led  ~= 0 then p.ledtexture(led) end
    if foot ~= 0 then p.footswitchtexture(foot) end

    local buf, pos, sr = {}, 1, 48000
    p.prepare = function(rate)
        sr = rate
        buf = {}
        for i = 1, math.floor(sr * 1.2) do buf[i] = 0 end
        pos = 1
    end
    p.process = function(x, n, timeMs, fb, mix)
        local d = math.max(1, math.floor(sr * timeMs / 1000))
        for i = 1, n do
            local rp = pos - d
            if rp < 1 then rp = rp + #buf end
            local delayed = buf[rp] or 0
            buf[pos] = x[i] + delayed * fb
            x[i] = x[i] * (1 - mix) + delayed * mix
            pos = pos + 1
            if pos > #buf then pos = 1 end
        end
        return x
    end
end

main()
