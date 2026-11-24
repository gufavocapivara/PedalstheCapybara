Welcome to the **PedalstheCapybara** wiki! This page explains how to write, install, and customize your own guitar pedals and amps with the built-in **Lua API**. No recompilation is needed: a plugin is just two files in the `Plugins/` folder.

## Contents

- [1. Install a plugin](#1-install-a-plugin)
- [2. Your first script](#2-your-first-script)
- [3. Creating a pedal or an amp](#3-creating-a-pedal-or-an-amp)
- [4. Controls](#4-controls)
- [5. Callbacks](#5-callbacks)
- [6. How `process()` works](#6-how-processworks)
- [7. Helpers and globals](#7-helpers-and-globals)
- [8. Appearance and behavior](#8-appearance-and-behavior)
- [9. Image assets](#9-image-assets)
- [10. Limits and errors](#10-limits-and-errors)
- [11. Next steps](#11-next-steps)

---

## 1. Install a plugin

```
Pedals the Capybara/
├── Plugins/
│   ├── clean_boost.lua      ← the Lua script
│   ├── clean_boost.ptc      ← the manifest
│   └── clean_boost_assets/  ← optional images
│       └── face.png
```

The `.ptc` file is plain text with one `KEY=value` per line (keys are case-insensitive):

```ini
NAME=Clean Boost
DESCRIPTION=Simple transparent boost with Gain and Level.
SCRIPT=clean_boost.lua
```

`SCRIPT=` is the `.lua` file name relative to `Plugins/`. The app scans `.ptc` files at startup (or when you reload plugins). You can also write scripts in the in-app **Pedal Editor (Lua)** and use **Test**, **Test as Amp**, **Compile and Create .ptc**, or **Compile Amp**.

## 2. Your first script

```lua
local function main()
    local pedal = np("Clean Boost")

    pedal.instance(SLIDER, "gain",  0.0, 2.0, 1.0)
    pedal.instance(SLIDER, "level", 0.0, 1.0, 0.7)
    pedal.color(60, 140, 220)

    pedal.process = function(x, n, gain, level)
        for i = 1, n do
            x[i] = x[i] * gain * level
        end
        return x
    end
end

main()   -- do not forget this call
```

Add it to the pedalboard from the Library tab (**Plugins** by default), turn on its power button, and play.

## 3. Creating a pedal or an amp

Call **exactly one** of these at load time; the returned table is your effect definition:

| Function | Result |
| --- | --- |
| `np([name])` | A **pedal**, insertable in any of the 15 pedalboard slots. Default name: `"Lua Pedal"`. |
| `newamp([name])` | An **amp**, always processed after all pedals. Only one amp is active. Default name: `"New Amp"`. |

A script that calls neither is rejected, and calling both (or one twice) is an error. Wrap your code in a function like above and **invoke it at the end of the file** — otherwise `np()` is never reached.

## 4. Controls

Declare controls on the definition table with dot syntax:

```lua
pedal.instance(SLIDER, "drive", 0.0, 1.0, 0.5)
pedal.instance(BUTTONSPIN, "mode", 0, 2, 0)
pedal.labels("mode", { "CLEAN", "CRUNCH", "LEAD" })
```

| Function | Description |
| --- | --- |
| `instance(kind, name, min, max, default)` | Declares a parameter. `SLIDER` (`2`) = continuous knob; `BUTTONSPIN` (`1`) = ON/OFF button, integer counter, or mode selector. Defaults: `min=0`, `max=1`, `default=(min+max)/2`. |
| `labels(name, {"A", "B", ...})` | Names for the positions of a `BUTTONSPIN`. Use `min=0` and positions `0, 1, 2, ...`; the callback still receives a number. |

Parameters are passed to `process()` **in declaration order**, right after `x, n`. `SLIDER` values are smoothed by the host (~20 ms) to avoid zipper noise. `BUTTONSPIN` values are discrete, so they are safe for selecting modes.

## 5. Callbacks

Assign these as fields of the table returned by `np()` / `newamp()`:

| Callback | Signature | Purpose |
| --- | --- | --- |
| `process` | `function(x, n, p1, ..., pN)` | **Required.** DSP for one audio block; must return the sample table. |
| `reset` | `function()` | Optional. Clear filter memory, delay buffers, envelopes, LFO phase. |
| `prepare` | `function(sr)` | Optional. Called before audio starts; compute coefficients for the actual sample rate. |

```lua
local z = 0.0
pedal.reset = function() z = 0.0 end

pedal.prepare = function(sr)
    -- sr is the rate your callback will see
end

pedal.process = function(x, n, cutoff)
    local coef = 1.0 - math.exp(-2.0 * math.pi * cutoff / SAMPLE_RATE)
    for i = 1, n do
        z = z + (x[i] - z) * coef
        x[i] = z
    end
    return x
end
```

## 6. How `process()` works

The callback runs **once per audio block**, not once per sample — this keeps Lua overhead low.

- `x` is a preallocated table with the input samples at indices **`1..n`** (Lua is 1-based).
- `n` is the block length (host buffer, up to 512) — **up to 1024 when oversampling**.
- Modify `x` in place and **return `x`**. Returning nothing discards your changes.
- Non-numeric or non-finite (`NaN`/`Inf`) output values become `0`.
- Only `x[1..n]` is valid; do not read or write beyond `n`.

Keep state (filters, phase, buffers) in local variables/upvalues, not in globals rewritten every block. Allocate buffers once at load time — **never allocate tables or strings inside `process()`**, because it runs on the audio thread and allocations can cause crackles.

Time-based effects must use the provided rate/size:

```lua
phase = phase + rate / SAMPLE_RATE   -- correct at any sample rate
```

## 7. Helpers and globals

| Name | Meaning |
| --- | --- |
| `SAMPLE_RATE` | Current rate. Inside `prepare()`/`process()` it is the real rate, **doubled** when `oversample(true)` is set. Initial value: `48000`. |
| `math.tanh(x)` | Native hyperbolic tangent — use it for soft clipping. |
| `clamp(x, lo, hi)` | Clamps a value into a range. |
| `db2lin(db)` | Decibels to linear gain: `10 ^ (db / 20)`. |
| `newassetsbar(label)` | Returns a library tab name for use with `linkbar()`. |

Only the `base` (minus `dofile`/`loadfile`), `table`, `string`, `math`, and `utf8` libraries are available. No `require`, `io`, `os`, or `debug` — scripts must be self-contained.

## 8. Appearance and behavior

```lua
local tab = newassetsbar("Drives")
pedal.linkbar(tab)          -- Library tab (default: "Plugins")
pedal.color(220, 60, 40)    -- accent color, RGB 0..255
pedal.oversample(true)      -- 2x internal rate, less aliasing
amp.size(440, 260)          -- custom amp card size
```

| Method | Effect |
| --- | --- |
| `linkbar(name)` | Places a pedal in a Library tab. Amps always appear in **Amps**. |
| `color(r, g, b)` | Card and control accent color; values are clamped to `0..255`. |
| `oversample(true)` | Runs `process()` at **2×** rate with `n` and `SAMPLE_RATE` doubled and anti-aliasing filters around it. Recommended for distortion and amps; costs ~2× CPU. |
| `size(w, h)` | Custom card size (`0..4096`); mainly used for amps. `0, 0` = automatic. |

Knobs support drag, mouse wheel, double-click to reset to the default, and `Shift` for fine adjustment. Button-spins cycle left-click/right-click or with the wheel. Bypass uses an 8 ms crossfade.

## 9. Image assets

For `Plugins/my_fuzz.lua`, put images in `Plugins/my_fuzz_assets/` (PNG or JPEG only):

```lua
local base = loadassets("my_fuzz").imagepast().loadimage("face.png")
if base ~= 0 then pedal.basetexture(base) end
```

Pedal texture methods: `basetexture(id)` (alias `texture(id)`), `knobtexture(id)`, `ledtexture(id)`, `footswitchtexture(id)`. Load images at script load time, not in `process()`. The `module` string passed to `loadassets()` is informational; the `_assets` folder name is what matters. The stock UI currently draws controls procedurally, so textures may not be visible in every build — use `color()`, `labels()`, and `size()` for reliable visual changes.

## 10. Limits and errors

| Limit / message | Meaning |
| --- | --- |
| 128 parameters | Hard limit per pedal/amp. |
| 15 pedals | Maximum active in the pedalboard. |
| `the script did not call np() or newamp()` | Missing creation call, often a forgotten `main()`. |
| `define pedal.process = function(...) ... end` | No `process` callback assigned. |
| `use np() ou newamp() apenas uma vez por script` | More than one creation call in one script. |
| `maximum of 128 parameters` | Too many `instance()` calls. |
| `parameter '<name>' does not exist` | `labels()` used before/with a wrong parameter name. |
| `Test pedal runtime error: ...` | A runtime error marked the test pedal as broken; check the message. |

If a pedal is silent: check the power button, make sure `process()` returns the table, and verify no `NaN`/`Inf` is produced. For clicks, clear state in `reset()`; for crackles, remove allocations from `process()`; for metallic fizz, enable `oversample(true)`.

## 11. Next steps

- Study `Plugins/pedal_american.lua` — a complete amp model with biquad filters, asymmetric clipping, `prepare()` and `reset()`.
- Use the **Pedal Template** / **Amp Template** buttons in the in-app Lua editor.
- If you publish the multi-page documentation, add pages for *Getting Started*, *Lua API Reference*, *Audio Callback and DSP*, *UI Customization*, *Assets and Textures*, *Examples*, and *Troubleshooting*, plus a `_Sidebar.md` for navigation.
