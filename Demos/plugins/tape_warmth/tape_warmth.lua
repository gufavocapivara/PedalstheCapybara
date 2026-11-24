-- Tape Warmth: demo amp-style plugin with images. It lives in the same chain.
local function main()
    local art = loadassets("tape_warmth")
    local face = art.loadimage("face.png")
    local knob = art.loadimage("knob.png")
    local led  = art.loadimage("led.png")
    local foot = art.loadimage("footswitch.png")

    local a = newamp("Tape Warmth")
    a.instance(SLIDER, "drive", 0, 1, 0.45)
    a.instance(SLIDER, "tone", 0, 1, 0.55)
    a.instance(SLIDER, "level", 0, 1, 0.7)
    a.color(226, 142, 74)
    a.size(280, 330)
    a.oversample(true)
    if face ~= 0 then a.basetexture(face) end
    if knob ~= 0 then a.knobtexture(knob) end
    if led  ~= 0 then a.ledtexture(led) end
    if foot ~= 0 then a.footswitchtexture(foot) end

    local lp = 0
    a.process = function(x, n, drive, tone, level)
        local g = 1.0 + drive * 9.0
        local t = 0.2 + tone * 0.7
        for i = 1, n do
            local y = math.tanh(x[i] * g)
            lp = lp + (y - lp) * t
            x[i] = lp * level
        end
        return x
    end
end

main()
