-- Velvet Chorus: demo modulation plugin with images.
local function main()
    local art = loadassets("velvet_chorus")
    local face = art.loadimage("face.png")
    local knob = art.loadimage("knob.png")
    local led  = art.loadimage("led.png")
    local foot = art.loadimage("footswitch.png")

    local p = np("Velvet Chorus")
    p.instance(SLIDER, "rate", 0.1, 6.0, 1.2)
    p.instance(SLIDER, "depth", 0, 1, 0.45)
    p.instance(SLIDER, "mix", 0, 1, 0.4)
    p.color(176, 126, 232)
    p.linkbar("Modulation")
    p.size(250, 330)
    p.oversample(true)
    if face ~= 0 then p.basetexture(face) end
    if knob ~= 0 then p.knobtexture(knob) end
    if led  ~= 0 then p.ledtexture(led) end
    if foot ~= 0 then p.footswitchtexture(foot) end

    local buf, pos, phase, sr = {}, 1, 0, 48000
    p.prepare = function(rate)
        sr = rate
        buf = {}
        for i = 1, math.floor(sr * 0.05) do buf[i] = 0 end
        pos = 1; phase = 0
    end
    p.process = function(x, n, rate, depth, mix)
        local base = math.floor(sr * 0.012)
        local mod = math.floor(sr * 0.008 * depth)
        for i = 1, n do
            buf[pos] = x[i]
            phase = phase + (2 * math.pi * rate / sr)
            if phase > math.pi * 2 then phase = phase - math.pi * 2 end
            local d = base + math.floor((math.sin(phase) * 0.5 + 0.5) * mod)
            local rp = pos - d
            if rp < 1 then rp = rp + #buf end
            local wet = buf[rp] or 0
            x[i] = x[i] * (1 - mix) + wet * mix
            pos = pos + 1
            if pos > #buf then pos = 1 end
        end
        return x
    end
end

main()
