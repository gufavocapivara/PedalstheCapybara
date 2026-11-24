# Pedals the Capybara — Lua API Wiki

Welcome! **Pedals the Capybara** is a real-time guitar pedalboard and amp simulator. In addition to the built-in effects, you can write your own **pedals** and **amps** in Lua, ship them as files in the `Plugins/` folder, and load them into the app without recompiling it.

## What you can build

- **Pedals** — DSP effects inserted in any of the 15 pedalboard slots.
- **Amps** — the final stage of the signal chain; only one amp is active at a time.
- Custom **sliders**, **ON/OFF buttons**, and **mode selectors** with labels.
- Custom **colors**, **card sizes**, library tabs, and (optionally) **image assets**.
- Stateful DSP: filters, LFOs, delays, saturators, amp tone stacks — anything you can express in Lua, as long as it obeys the real-time rules.

## Documentation index

| Page | Contents |
| --- | --- |
| [Getting Started](Getting-Started.md) | Folder layout, `.ptc` manifests, installing and reloading plugins |
| [Lua API Reference](Lua-API-Reference.md) | Every global, method, and callback exposed to scripts |
| [Audio Callback and DSP](Audio-Callback-and-DSP.md) | Block processing, state, smoothing, oversampling, real-time rules |
| [UI Customization](UI-Customization.md) | Widgets, labels, colors, card sizes, library tabs |
| [Assets and Textures](Assets-and-Textures.md) | `loadassets()`, image folders, texture IDs |
| [Examples](Examples.md) | Copy-paste pedals and amps, from minimal to stateful |
| [Troubleshooting](Troubleshooting.md) | Load errors, runtime errors, silence, crackles, aliasing |
| [Publishing the Wiki](Publishing-the-Wiki.md) | How to push these Markdown files to your GitHub Wiki |

## Plugin formats

- **Folder plugin**: `Plugins/name.ptc` + `name.lua` + `name_assets/` (compatible with old plugins).
- **`.pctp`**: one plugin as a ZIP package (manifest + script + assets). The app reads it directly.
- **`.pp`**: a plugin pack ZIP containing several plugins. Create it from a folder in **Plugins > Create plugin pack (.pp)...**

Amps are normal cards in the same signal chain as pedals (`isAmp=true`, created with `newamp()`).

## 30-second example

Save this as `Plugins/clean_boost.lua`, add a matching `Plugins/clean_boost.ptc` (see [Getting Started](Getting-Started.md)), and restart or refresh the app:

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

main()
```

## Quick limits

| Item | Limit |
| --- | --- |
| Parameters per pedal/amp | 128 |
| Pedals in the pedalboard | 15 |
| Samples per process call | Up to 512 (up to 1024 with `oversample(true)`) |
| `np()` / `newamp()` calls per script | Exactly one |
| Available Lua libraries | `base`, `table`, `string`, `math`, `utf8` (plus the helpers below) |
| Image formats for assets | PNG and JPEG |

> **Heads up:** the `.lua` files in this wiki are written in English for readability. The existing `Plugins/pedal_american.lua` shipped with the app is a complete, more advanced amp model you can study alongside these pages.
