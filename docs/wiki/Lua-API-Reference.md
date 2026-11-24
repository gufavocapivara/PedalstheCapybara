# Lua API Reference

Scripts run in an isolated Lua state. Everything below is available at load time unless noted otherwise.

## Contents

- [Creation functions](#creation-functions)
- [Globals and helper functions](#globals-and-helper-functions)
- [Pedal / amp methods](#pedal--amp-methods)
- [Callbacks you can assign](#callbacks-you-can-assign)
- [Available standard libraries](#available-standard-libraries)
- [Limits and error messages](#limits-and-error-messages)

---

## Creation functions

Call **exactly one** of these at the top level of your script (inside a function that you invoke, like the templates do, is fine). The returned table is the pedal definition: you call methods on it and assign callbacks to it.

| Function | Returns | Description |
| --- | --- | --- |
| `np([name])` | pedal table | Creates a **pedal**. Default name: `"Lua Pedal"`. The pedal appears in the library tab set by `linkbar()` (default `Plugins`) and can be inserted in the pedalboard. |
| `newamp([name])` | amp table | Creates an **amp**. Default name: `"New Amp"`. The script is registered in the Amps library and the amp always processes **after** all pedals. Only one amp is active at a time. |

Calling either function a second time in the same script raises an error (`use np() ou newamp() apenas uma vez por script`).

```lua
local pedal = np("My Fuzz")
-- or
local amp = newamp("My Amp")
```

---

## Globals and helper functions

| Name | Type | Description |
| --- | --- | --- |
| `SLIDER` | integer (`2`) | Widget kind for a continuous knob. |
| `BUTTONSPIN` | integer (`1`) | Widget kind for an ON/OFF button, an integer counter, or a labeled mode selector. |
| `SAMPLE_RATE` | number | Current sample rate. **Inside `prepare()` and `process()` this is the actual rate**, i.e. `2 ×` the device rate when `oversample(true)` is set. Use it for LFOs and filter coefficients instead of hard-coded numbers. Initial value before `prepare()`: `48000`. |
| `math.tanh(x)` | function | Hyperbolic tangent implemented natively in C++. Use it for soft clipping. |
| `clamp(x, lo, hi)` | function | Clamps `x` into `[lo, hi]`. |
| `db2lin(db)` | function | Converts decibels to linear gain: `10 ^ (db / 20)`. |
| `newassetsbar(label)` | string | Declares a library tab name. Pass the result to `linkbar()` to place a pedal in that tab. See [UI Customization](UI-Customization.md). |
| `loadassets(module)` | table | Entry point for image assets. See [Assets and Textures](Assets-and-Textures.md). |

Example:

```lua
local g = db2lin(-6.0)          -- ~0.501
local v = clamp(x * g, -1, 1)
local y = math.tanh(v * 2.0)
```

---

## Pedal / amp methods

Methods are called with **dot syntax** on the table (`pedal.instance(...)`, `amp.color(...)`). All of them are intended to be called **once at load time**, not from the audio callback.

### `instance(kind, name, min, max, default)`

Declares one control. Controls are passed to `process()` in **declaration order**, right after `x, n`.

| Argument | Meaning |
| --- | --- |
| `kind` | `SLIDER` or `BUTTONSPIN`. |
| `name` | Label shown under the knob/button. Also used by `labels()`. |
| `min`, `max` | Range. Defaults: `0` and `1`. |
| `default` | Initial value. Defaults to the midpoint `(min + max) / 2`. |

```lua
pedal.instance(SLIDER, "drive", 0.0, 1.0, 0.5)
pedal.instance(BUTTONSPIN, "mode", 0, 2, 0)
```

`BUTTONSPIN` values are **not smoothed** by the host (they are discrete), which makes them safe for indexing modes. `SLIDER` values are smoothed (~20 ms) to avoid zipper noise.

### `labels(name, {"A", "B", ...})`

Assigns display names to the positions of a `BUTTONSPIN` (or any parameter). The control then cycles through the names instead of showing numbers.

```lua
amp.instance(BUTTONSPIN, "mode", 0, 2, 0)
amp.labels("mode", { "CLEAN", "CRUNCH", "LEAD" })
```

The displayed position is `round(value - min)`, so declare the parameter with `min = 0` and consecutive integer positions (`0, 1, 2, ...`). The value still reaches `process()` as a number. An unknown parameter name raises `parameter '<name>' does not exist`.

### `color(r, g, b)`

Sets the accent color of the card and controls. Components are `0..255` and are clamped to that range.

```lua
pedal.color(220, 60, 40)   -- warm red
```

### `linkbar(name)`

Places a **pedal** in a library tab. `name` is a string; use the value returned by `newassetsbar()` or any string you like. The default tab is `Plugins`. Amps always live in the `Amps` tab.

```lua
local tab = newassetsbar("Drives")
pedal.linkbar(tab)
```

### `oversample(enabled)`

When called with `true`, the host runs your `process()` callback at **2× the device sample rate** and passes **2× the block length**, with 4th-order Butterworth filters on the way up and down. This prevents aliasing ("metallic fizz") from distortion and amp saturation. It costs roughly twice the CPU in your Lua loop.

```lua
amp.oversample(true)
```

> Because `n` and `SAMPLE_RATE` are both doubled, always derive time constants from them. Do not assume `n` equals the host buffer size.

### `size(width, height)`

Sets the custom card size in UI units (clamped to `0..4096`). It is primarily used for amps; `0, 0` (the default) lets the app compute the size from the number of controls.

```lua
amp.size(440, 220)
```

### Texture methods (pedal only)

These accept a texture ID returned by `loadassets(...).imagepast().loadimage(...)`:

| Method | Target |
| --- | --- |
| `basetexture(id)` / `texture(id)` | Pedal background |
| `knobtexture(id)` | Knob face |
| `ledtexture(id)` | Status LED |
| `footswitchtexture(id)` | Footswitch |

See [Assets and Textures](Assets-and-Textures.md) for the full chain and caveats.

---

## Callbacks you can assign

Assign functions to fields of the pedal/amp table:

### `definition.process = function(x, n, p1, ..., pN)`

**Required.** Called once per audio block (not once per sample).

- `x` — a preallocated Lua table holding the input samples at indices **`1..n`** (Lua tables are 1-based).
- `n` — number of samples in this block.
- `p1 ... pN` — current parameter values, in declaration order.
- Return — a **table** with the output samples at `1..n`. Returning `x` after modifying it in place is the normal pattern. Non-numeric or non-finite values are replaced with `0`.

```lua
pedal.process = function(x, n, gain)
    for i = 1, n do
        x[i] = math.tanh(x[i] * gain)
    end
    return x
end
```

### `definition.reset = function()`

Optional. Clears internal state (filter memory, delay lines, envelopes, LFO phase, etc.). The host calls it when the effect is disengaged and in other state-reset situations.

### `definition.prepare = function(sr)`

Optional. Called before audio starts (and after sample-rate changes). `sr` is the rate your callback will actually see, and the global `SAMPLE_RATE` is set to the same value just before the call. Use it to compute filter coefficients or allocate reusable state.

```lua
local coeff = 0
pedal.prepare = function(sr)
    coeff = 1.0 - math.exp(-1.0 / (0.01 * sr))
end
```

---

## Available standard libraries

For safety and real-time determinism, only these Lua libraries are opened:

| Library | Status |
| --- | --- |
| `base` | Available, except `dofile` and `loadfile` (explicitly disabled) |
| `table` | Available |
| `string` | Available |
| `math` | Available, plus `math.tanh` |
| `utf8` | Available |
| `io`, `os`, `debug`, `package`, `require`, `coroutine` | **Not available** |

Scripts must therefore be self-contained: no `require`, no loading other files at runtime, no file or shell access. The built-in `Plugins/pedal_american.lua` is a good example of a self-contained DSP script.

---

## Limits and error messages

| Limit / message | Meaning |
| --- | --- |
| `maximum of 128 parameters` | Too many `instance()` calls (hard limit 128). |
| `the script did not call np() or newamp()` | The script finished without creating a pedal/amp (often a missing `main()` call). |
| `define pedal.process = function(...) ... end` | No `process` callback was assigned on the returned table. |
| `use np() ou newamp() apenas uma vez por script` | More than one creation call in the same script. |
| `parameter '<name>' does not exist` | `labels()` was given a name that was not declared with `instance()`. |
| Pedalboard full | At most **15** pedals can be active at once; the editor reports `Pedalboard is full (maximum 15 pedals).` |

Load-time problems are shown in the plugin status / editor message. Runtime errors inside `process()` mark the pedal as broken (it is silenced) and are displayed by the editor as `Test pedal runtime error: ...`.

See [Troubleshooting](Troubleshooting.md) for fixes.
