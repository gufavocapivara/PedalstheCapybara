-- American Sound JF-14: modelo DSP inspirado no JOYO American Sound.
-- The manufacturer describes the pedal as a Fender '57 Deluxe simulation and
-- documents its six controls; the exact topology and response curves are not public.
-- This is a continuous gray-box model, not a bit-for-bit electrical replica.

local amp = newamp("JOYO American Sound JF-14")
amp.instance(SLIDER, "level", 0.0, 1.0, 0.74)
amp.instance(SLIDER, "low",   0.0, 1.0, 0.50)
amp.instance(SLIDER, "mid",   0.0, 1.0, 0.50)
amp.instance(SLIDER, "high",  0.0, 1.0, 0.50)
amp.instance(SLIDER, "voice", 0.0, 1.0, 0.48)
amp.instance(SLIDER, "drive", 0.0, 1.0, 0.24)
amp.color(214, 112, 42)
amp.size(440, 220)
amp.oversample(true)

local function bq()
    return { b0=1, b1=0, b2=0, a1=0, a2=0, z1=0, z2=0 }
end
local function clear(f) f.z1, f.z2 = 0, 0 end
local function run(f, x)
    local y = f.b0 * x + f.z1
    f.z1 = f.b1 * x - f.a1 * y + f.z2
    f.z2 = f.b2 * x - f.a2 * y
    return y
end
local function hp(f, fs, hz, q)
    hz = math.max(10, math.min(hz, fs * 0.45))
    local w = 2 * math.pi * hz / fs
    local c, s = math.cos(w), math.sin(w)
    local a = s / (2 * (q or 0.70710678))
    local a0 = 1 + a
    f.b0 = (1 + c) * 0.5 / a0
    f.b1 = -(1 + c) / a0
    f.b2 = f.b0
    f.a1 = -2 * c / a0
    f.a2 = (1 - a) / a0
end
local function lp(f, fs, hz, q)
    hz = math.max(10, math.min(hz, fs * 0.45))
    local w = 2 * math.pi * hz / fs
    local c, s = math.cos(w), math.sin(w)
    local a = s / (2 * (q or 0.70710678))
    local a0 = 1 + a
    f.b0 = (1 - c) * 0.5 / a0
    f.b1 = (1 - c) / a0
    f.b2 = f.b0
    f.a1 = -2 * c / a0
    f.a2 = (1 - a) / a0
end
local function peak(f, fs, hz, q, db)
    hz = math.max(10, math.min(hz, fs * 0.45))
    local A = 10 ^ (db / 40)
    local w = 2 * math.pi * hz / fs
    local c, s = math.cos(w), math.sin(w)
    local a = s / (2 * q)
    local a0 = 1 + a / A
    f.b0 = (1 + a * A) / a0
    f.b1 = -2 * c / a0
    f.b2 = (1 - a * A) / a0
    f.a1 = -2 * c / a0
    f.a2 = (1 - a / A) / a0
end
local function shelf(f, fs, hz, db, high)
    hz = math.max(10, math.min(hz, fs * 0.45))
    local A = 10 ^ (db / 40)
    local w = 2 * math.pi * hz / fs
    local c, s = math.cos(w), math.sin(w)
    local alpha = s / math.sqrt(2)
    local beta = 2 * math.sqrt(A) * alpha
    local a0
    if high then
        a0 = (A + 1) - (A - 1) * c + beta
        f.b0 = A * ((A + 1) + (A - 1) * c + beta) / a0
        f.b1 = -2 * A * ((A - 1) + (A + 1) * c) / a0
        f.b2 = A * ((A + 1) + (A - 1) * c - beta) / a0
        f.a1 = 2 * ((A - 1) - (A + 1) * c) / a0
        f.a2 = ((A + 1) - (A - 1) * c - beta) / a0
    else
        a0 = (A + 1) + (A - 1) * c + beta
        f.b0 = A * ((A + 1) - (A - 1) * c + beta) / a0
        f.b1 = 2 * A * ((A - 1) - (A + 1) * c) / a0
        f.b2 = A * ((A + 1) - (A - 1) * c - beta) / a0
        f.a1 = -2 * ((A - 1) + (A + 1) * c) / a0
        f.a2 = ((A + 1) + (A - 1) * c - beta) / a0
    end
end

-- Asymmetric curve normalized for unit differential gain at the curve input.
-- f(x)= [tanh(k(x+b))-tanh(kb)] / [k(1-tanh²(kb))].
local function triode(x, drive, bias)
    local k = 1.15
    local b = bias * k
    local base = math.tanh(b)
    local slope = k * (1 - base * base)
    return (math.tanh(k * (x * drive + bias)) - base) / slope
end

local fs = SAMPLE_RATE or 96000
local inputHP, hum, voiceLow, voiceMid = bq(), bq(), bq(), bq()
local lowEQ, midEQ, highEQ = bq(), bq(), bq()
local dc, cabHP, speakerPeak, cabLP = bq(), bq(), bq(), bq()

local function configure(sr, low, mid, high, voice)
    fs = sr
    -- Continuous Voice: from clean/bright to fuller and more mid-forward.
    -- Voice changes the pre-emphasis and operating point along with Drive.
    hp(inputHP, fs, 55 + voice * 35, 0.70710678)
    peak(hum, fs, 60, 18.0, -5.0) -- gentle 60 Hz mains hum notch
    shelf(voiceLow, fs, 180, (0.5 - voice) * 3.0, false)
    peak(voiceMid, fs, 900, 0.8, (voice - 0.5) * 6.0)
    shelf(lowEQ, fs, 125, (low - 0.5) * 24.0, false)
    peak(midEQ, fs, 1000, 0.75, (mid - 0.5) * 24.0)
    shelf(highEQ, fs, 3200, (high - 0.5) * 24.0, true)
    hp(cabHP, fs, 68, 0.70710678)
    peak(speakerPeak, fs, 115, 1.0, 1.8)
    lp(cabLP, fs, 6800, 0.70710678)
    hp(dc, fs, 18, 0.70710678)
end

configure(fs, 0.5, 0.5, 0.5, 0.48)
amp.prepare = function(sr)
    configure(sr, 0.5, 0.5, 0.5, 0.48)
end
amp.reset = function()
    for _, f in ipairs({ inputHP, hum, voiceLow, voiceMid, lowEQ, midEQ,
                         highEQ, dc, cabHP, speakerPeak, cabLP }) do clear(f) end
end
amp.process = function(x, n, level, low, mid, high, voice, drive)
    configure(fs, low, mid, high, voice)
    local d = drive * drive
    local preGain = 1.0 + 15.0 * d
    local powerGain = 1.0 + 1.8 * drive
    local bias = 0.04 + voice * 0.12
    local levelDb = -24.0 + level * 30.0
    local outGain = 10 ^ (levelDb / 20)

    for i = 1, n do
        local s = run(inputHP, x[i])
        s = run(hum, s)
        s = run(voiceLow, s)
        s = run(voiceMid, s)
        s = triode(s, preGain * (0.85 + 0.30 * voice), bias)
        s = triode(s, powerGain, bias * 0.55)
        s = run(lowEQ, s)
        s = run(midEQ, s)
        s = run(highEQ, s)
        s = math.tanh(s * (1.15 + drive * 0.55)) * 0.88
        s = run(dc, s)
        s = run(cabHP, s)
        s = run(speakerPeak, s)
        s = run(cabLP, s)
        x[i] = s * outGain
    end
    return x
end