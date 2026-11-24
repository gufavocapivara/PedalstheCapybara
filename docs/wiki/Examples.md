# Examples

Each example below is a **complete `.lua` file**. Save it in `Plugins/` and add a matching `.ptc` manifest as shown in [Getting Started](Getting-Started.md).

## 1. Clean boost (minimal)

```lua
-- Plugins/clean_boost.lua
local function main()
    local pedal = np("Clean Boost")

    pedal.instance(SLIDER, "gain",  0.0, 2.0, 1.0)
    pedal.instance(SLIDER, "level", 0.0, 1.0, 0.7)
    pedal.color(60, 140, 220)

    pedal.process = function(x, n, gain, level)
        local g = gain * level
        for i = 1, n do
            x[i] = x[i] * g
        end
        return x
    end
end

main()
```

```ini
NAME=Clean Boost
DESCRIPTION=Transparent boost with Gain and Level.
SCRIPT=clean_boost.lua
```

## 2. Fuzz with a mode selector and oversampling

```lua
-- Plugins/my_fuzz.lua
local function main()
    local tab = newassetsbar("Drives")
    local pedal = np("Lua Fuzz")

    pedal.instance(BUTTONSPIN, "mode", 0, 2, 1)
    pedal.labels("mode", { "MILD", "FUZZ", "GATE" })
    pedal.instance(SLIDER, "drive", 0.0, 1.0, 0.6)
    pedal.instance(SLIDER, "level", 0.0, 1.0, 0.6)

    pedal.color(220, 60, 40)
    pedal.linkbar(tab)
    pedal.oversample(true)   -- run at 2x rate: less aliasing from hard clipping

    local dc = 0.0
    pedal.reset = function() dc = 0.0 end

    pedal.process = function(x, n, mode, drive, level)
        local g = 1.0 + drive * 40.0
        local out = 0.6 + level * 0.6
        for i = 1, n do
            local s = math.tanh(x[i] * g)
            if mode > 1.5 then
                s = s < 0.05 and 0.0 or s   -- crude gate mode
            elseif mode < 0.5 then
                s = s * 0.6                  -- mild mode
            end
            dc = dc + (s - dc) * 0.0005       -- DC blocker / smoothing
            x[i] = (s - dc) * out
        end
        return x
    end
end

main()
```

## 3. Tremolo (time-based, uses `SAMPLE_RATE`)

```lua
-- Plugins/my_trem.lua
local function main()
    local pedal = np("Lua Trem")

    pedal.instance(SLIDER, "rate",  0.1, 12.0, 5.0)
    pedal.instance(SLIDER, "depth", 0.0,  1.0, 0.5)
    pedal.color(90, 180, 140)

    local phase = 0.0
    pedal.reset = function() phase = 0.0 end

    pedal.process = function(x, n, rate, depth)
        for i = 1, n do
            phase = phase + rate / SAMPLE_RATE
            if phase >= 1.0 then phase = phase - 1.0 end
            local lfo = 0.5 + 0.5 * math.sin(2 * math.pi * phase)
            x[i] = x[i] * (1.0 - depth * lfo)
        end
        return x
    end
end

main()
```

Because the phase advances by `rate / SAMPLE_RATE`, the LFO keeps the correct speed when `oversample(true)` is enabled (both `n` and `SAMPLE_RATE` are doubled).

## 4. Delay with a preallocated buffer

```lua
-- Plugins/my_delay.lua
local function main()
    local pedal = np("Lua Delay")

    pedal.instance(SLIDER, "time",     50.0, 1000.0, 380.0)  -- milliseconds
    pedal.instance(SLIDER, "feedback",  0.0,    0.9,  0.35)
    pedal.instance(SLIDER, "mix",       0.0,    1.0,  0.30)
    pedal.color(80, 110, 200)

    local size = 8192
    local buf, w = {}, 1
    for i = 1, size do buf[i] = 0.0 end   -- allocate once, at load time

    pedal.reset = function()
        for i = 1, size do buf[i] = 0.0 end
        w = 1
    end

    pedal.process = function(x, n, timeMs, feedback, mix)
        local d = math.floor(timeMs * 0.001 * SAMPLE_RATE)
        if d < 1 then d = 1 elseif d > size - 2 then d = size - 2 end
        for i = 1, n do
            local r = w - d
            if r < 1 then r = r + size end
            local wet = buf[r]
            buf[w] = x[i] + wet * feedback
            w = w % size + 1
            x[i] = x[i] + wet * mix
        end
        return x
    end
end

main()
```

## 5. A simple amp

```lua
-- Plugins/my_amp.lua
local function main()
    local amp = newamp("My Lua Amp")

    amp.instance(BUTTONSPIN, "mode", 0, 2, 1)
    amp.labels("mode", { "CLEAN", "CRUNCH", "LEAD" })
    amp.instance(SLIDER, "gain",   0.0, 1.0, 0.5)
    amp.instance(SLIDER, "bass",   0.0, 1.0, 0.5)
    amp.instance(SLIDER, "middle", 0.0, 1.0, 0.5)
    amp.instance(SLIDER, "treble", 0.0, 1.0, 0.5)
    amp.instance(SLIDER, "volume", 0.0, 1.0, 0.6)

    amp.color(20, 20, 24)
    amp.size(440, 260)
    amp.oversample(true)

    local lowpass = 0.0
    amp.reset = function() lowpass = 0.0 end

    amp.process = function(x, n, mode, gain, bass, middle, treble, volume)
        local stageGain
        if mode < 0.5 then      stageGain = 1.0 + gain * 2.0
        elseif mode < 1.5 then  stageGain = 1.0 + gain * 9.0
        else                    stageGain = 1.0 + gain * 22.0 end

        local mk = 1.0 / (0.8 + 0.5 * stageGain)
        local bright = 2000.0 + treble * 5000.0
        local coef = 1.0 - math.exp(-2.0 * math.pi * bright / SAMPLE_RATE)
        local outLevel = db2lin(-18.0 + volume * 24.0)

        for i = 1, n do
            local s = math.tanh(x[i] * stageGain)
            lowpass = lowpass + (s - lowpass) * coef
            local tone = lowpass * (0.7 + treble * 0.6) + s * (bass - 0.5) * 0.4
            tone = tone * (0.7 + middle * 0.6)
            x[i] = tone * mk * outLevel
        end
        return x
    end
end

main()
```

Register it with `Compile Amp` in the editor, or with a `.ptc` manifest; it appears in the **Amps** tab and is processed after every pedal.

## Study the shipped model

`Plugins/pedal_american.lua` in the project is a complete gray-box amp model with:

- biquad filter helpers (`hp`, `lp`, `peak`, `shelf`);
- asymmetric triode-style clipping;
- continuous `voice` control;
- `prepare()` for coefficient setup and `reset()` for filter state.

It is a good reference for writing your own amp scripts.

## See also

- [Lua API Reference](Lua-API-Reference.md)
- [Audio Callback and DSP](Audio-Callback-and-DSP.md)
- [Troubleshooting](Troubleshooting.md)
