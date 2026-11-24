-- Capybara Drive: example plugin with real images.
-- The PNGs live in Plugins/capybara_drive_assets/ (folder plugin) or inside
-- assets/ when this plugin is packed as a .pctp.
local function main()
    local art = loadassets("capybara_drive")
    local face  = art.loadimage("face.png")
    local knob  = art.loadimage("knob.png")
    local led   = art.loadimage("led.png")
    local foot  = art.loadimage("footswitch.png")
    local label = art.loadimage("label.png")

    local p = np("Capybara Drive")
    p.instance(SLIDER, "drive", 0, 1, 0.45)
    p.instance(SLIDER, "tone", 0, 1, 0.5)
    p.instance(SLIDER, "level", 0, 1, 0.55)
    p.color(233, 161, 59)
    p.linkbar("Drives")
    p.size(240, 330)
    p.oversample(true)

    if face  ~= 0 then p.basetexture(face) end
    if knob  ~= 0 then p.knobtexture(knob) end
    if led   ~= 0 then p.ledtexture(led) end
    if foot  ~= 0 then p.footswitchtexture(foot) end
    if label ~= 0 then p.labeltexture(label) end

    local lp = 0
    p.process = function(x, n, drive, tone, level)
        local g = 1.0 + drive * 18.0
        local c = 0.35 + tone * 0.6
        for i = 1, n do
            local y = math.tanh(x[i] * g)
            lp = lp + (y - lp) * c
            x[i] = lp * level
        end
        return x
    end
end

main()
