# Audio Callback and DSP

This page explains how and when your Lua code runs, and how to write DSP that stays glitch-free.

## The signal chain

```
input → DC blocker → pedal 1 → ... → pedal N → amp → output
```

- Up to **15 pedals** run in the order shown in the pedalboard.
- The **amp is always the last stage**. A script created with `newamp()` is registered in the Amps library; only one amp is active.
- Bypassing a pedal uses an **8 ms crossfade**, so toggling power does not click.

## Block processing (the only exposed mode)

Your `process` callback is called **once per audio block**, not once per sample. This keeps Lua overhead low.

```lua
definition.process = function(x, n, p1, p2, ...)
    -- x[1] .. x[n] are the input samples (Lua is 1-based)
    -- p1, p2, ... are the current parameter values, in declaration order
    -- modify x in place and return it
    return x
end
```

| Detail | Value |
| --- | --- |
| Block size `n` | Host buffer size (typically 128, 256, or 512), up to 512 frames |
| With `oversample(true)` | `n` is **2 ×** the host buffer (up to 1024) and `SAMPLE_RATE` is doubled |
| Sample values | Floats; typical usable range is about `-1..1` |
| Return value | A table with numbers at indices `1..n`; return `x` after in-place edits |
| Bad output | `nil`, non-numeric, `NaN`, and `Inf` entries are replaced with `0` |
| If `process` is missing | The script is rejected with `define pedal.process = function(...) ... end` and is not loaded |

> Do not read `x[n+1]` or write beyond `n`: the table is shared/reused and may be larger than the current block, so stale samples can leak into your output.

## State: where to keep it

Keep DSP state in locals/upvalues of your script, not in the `x` table and not in globals you rewrite every block.

```lua
local function main()
    local pedal = np("One-Pole LP")
    pedal.instance(SLIDER, "cutoff", 100.0, 8000.0, 2000.0)

    local z = 0.0
    local a = 0.0

    pedal.prepare = function(sr)
        -- coefficient recomputed only when the sample rate changes
        local rc = 1.0 / (2.0 * math.pi * 2000.0)
        a = 1.0 - math.exp(-1.0 / (rc * sr))
    end

    pedal.reset = function() z = 0.0 end

    pedal.process = function(x, n, cutoff)
        -- cutoff is smoothed by the host, so recomputing per block is acceptable
        local rc = 1.0 / (2.0 * math.pi * math.max(20.0, cutoff))
        local coef = 1.0 - math.exp(-1.0 / (rc * SAMPLE_RATE))
        for i = 1, n do
            z = z + (x[i] - z) * coef
            x[i] = z
        end
        return x
    end
end
main()
```

`prepare(sr)` is called before audio starts; `SAMPLE_RATE` is set to `sr` right before the call. `reset()` is called when the effect is disengaged — clear filter memory, delay buffers, and envelopes there to avoid clicks or stale repeats when it is switched back on.

## Parameter smoothing

`SLIDER` values are smoothed by the host (about a 20 ms time constant) before being passed to `process()`, so sudden knob moves do not produce zipper noise. `BUTTONSPIN` values are passed **discretely**, which is what you want for mode indices:

```lua
amp.process = function(x, n, mode, gain)
    local g
    if mode < 0.5 then g = 1.0 + gain * 2.0
    elseif mode < 1.5 then g = 1.0 + gain * 8.0
    else g = 1.0 + gain * 20.0 end
    for i = 1, n do x[i] = math.tanh(x[i] * g) end
    return x
end
```

Because mode values are discrete, comparisons like `mode == 1` are also safe, but range checks tolerate any intermediate representation.

## Time-based effects and `SAMPLE_RATE`

Always derive phases and delays from `SAMPLE_RATE` and `n`, never from a fixed sample count. This keeps LFOs at the right speed when oversampling is enabled or the device uses a different rate:

```lua
local phase = 0.0
pedal.process = function(x, n, rate, depth)
    for i = 1, n do
        phase = phase + rate / SAMPLE_RATE
        if phase >= 1.0 then phase = phase - 1.0 end
        x[i] = x[i] * (1.0 - depth * (0.5 + 0.5 * math.sin(2 * math.pi * phase)))
    end
    return x
end
```

## Oversampling

Call `oversample(true)` for distortion, fuzz, and amp models. The host then:

1. upsamples by 2 with zero-stuffing and 4th-order Butterworth filtering;
2. calls your `process()` at 2× rate with `n` doubled and `SAMPLE_RATE` doubled;
3. filters and decimates the result back to the device rate.

This moves clipping harmonics above the audible band during processing so they cannot fold back as aliasing. Remember:

- your loop runs **twice** as often — keep it lean;
- filter cutoffs specified in Hz should use the (doubled) `SAMPLE_RATE`;
- time in seconds is unaffected: `phase += rate / SAMPLE_RATE` still gives the correct LFO rate.

## Real-time rules (important)

The callback runs on the **audio thread**. The C++ engine pre-allocates the sample table and runs a full Lua GC once after loading, but anything your script allocates inside `process()` still happens on the audio thread and can cause crackles.

**Do inside `process()`:**

- arithmetic on numbers;
- reading/writing `x[i]` and local/upvalue state;
- calling `math.tanh`, `clamp`, `db2lin`, `math.sin`, etc.

**Avoid inside `process()`:**

- creating tables (`{}`), strings, or string concatenation;
- `print` and any I/O (most of it is not available anyway);
- `table.insert`, `ipairs` over newly built tables, or anything that triggers allocations;
- long loops or expensive per-sample coefficient recomputation — move constants to `prepare()` or compute once per block.

If you need a buffer (for a delay, for example), allocate and fill it once at load time:

```lua
local buf, w = {}, 1
for i = 1, 8192 do buf[i] = 0.0 end   -- at load time, not in process()

pedal.process = function(x, n, time_ms, mix)
    local size = #buf
    for i = 1, n do
        local d = math.floor(time_ms * 0.001 * SAMPLE_RATE)
        if d < 1 then d = 1 elseif d > size - 2 then d = size - 2 end
        local r = w - d
        if r < 1 then r = r + size end
        local wet = buf[r]
        buf[w] = x[i] + wet * 0.35
        w = w % size + 1
        x[i] = x[i] + wet * mix
    end
    return x
end
```

## See also

- [Lua API Reference](Lua-API-Reference.md) — exact signatures.
- [Troubleshooting](Troubleshooting.md) — silence, clicks, aliasing.
- `Plugins/pedal_american.lua` — a full amp model with biquads, asymmetric clipping, `prepare()`, and `reset()`.
