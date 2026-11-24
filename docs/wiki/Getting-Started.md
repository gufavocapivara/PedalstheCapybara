# Getting Started: install and load a plugin

You can install plugins in three ways:

1. **Folder plugin** (compatible): `Plugins/*.ptc` + `.lua` + optional `<name>_assets/`.
2. **`.pctp` package**: a single ZIP containing manifest, script and images.
3. **`.pp` plugin pack**: a ZIP containing multiple plugins/packages.

## Folder layout

```
Pedals the Capybara/
  PedalsTheCapybara.exe
  Plugins/
    clean_boost.ptc
    clean_boost.lua
    clean_boost_assets/
      face.png
  Packs/
    CapybaraStarter.pp
```

The app scans `Plugins/` and `Packs/` at startup. You can also import files from **File > Import plugin / package...**, drag-and-drop into the window, or use the Plugins window.

## `.ptc` manifest

Plain text, one `KEY=value` per line. Keys are case-insensitive; UTF-8 BOM is tolerated.

```ini
NAME=Clean Boost
DESCRIPTION=Simple transparent boost with gain and level.
AUTHOR=Your Name
VERSION=1.0
SCRIPT=clean_boost.lua
ICON=clean_boost_assets/face.png
```

`SCRIPT` is recommended. If missing, the app tries to find a `.lua` in the same folder; a standalone `.lua` can also be imported and gets an automatic manifest.

## `.pctp` package

A `.pctp` is a ZIP. Recommended layout:

```text
plugin.ptc
my_plugin.lua
assets/
  face.png
  knob.png
```

The app reads `.pctp` directly; no extraction needed. Use the editor's **Export .pctp** button.

## `.pp` plugin pack

A `.pp` is a ZIP with multiple plugins. It can contain `.pctp` files and/or plugin folders. Optional `pack.ini`:

```ini
NAME=My Pack
DESCRIPTION=My favorite plugins
AUTHOR=Me
VERSION=1.0
```

Create it from the app: **Plugins > Create plugin pack (.pp)...** and select a folder containing plugins.

## Two ways to create a plugin

### Option A — the in-app Lua editor (easiest)

Open the **Pedal Editor (Lua)** window. It starts with a pedal template and provides these buttons:

| Button | What it does |
| --- | --- |
| **Test** | Loads the current code as a pedal, adds it enabled to the end of the pedalboard. |
| **Test as Amp** | Loads the current code as an amp and activates it as the last stage. Requires `newamp()`. |
| **Compile and Create .ptc** | Validates the script, writes `Plugins/<slug>.lua` + `Plugins/<slug>.ptc`, and adds the pedal to the library. |
| **Compile Amp** | Same, but requires the script to call `newamp()`; registers it in the Amps library. |
| **Remove Test** | Removes the test pedal from the pedalboard. |
| **Pedal Template** / **Amp Template** | Replaces the editor contents with a starting template. |

Compilation never overwrites an existing plugin: if `my_fuzz.lua` or `my_fuzz.ptc` already exists, it writes `my_fuzz_2.*`, then `_3`, and so on. The file slug is derived from the **Plugin name** field (lowercase, non-alphanumeric characters become `_`).

### Option B — write the files by hand

Use any text editor, save both files in `Plugins/`, then use the app's plugin reload action (or restart the app). At startup the app:

1. scans all `.ptc` files, sorted by manifest name;
2. executes each referenced `.lua` script in a fresh Lua state;
3. registers scripts that called `np()` as pedals, in the library tab given by `linkbar()` (or `Plugins` if none);
4. registers scripts that called `newamp()` in the **Amps** library.

A script that calls neither `np()` nor `newamp()` is rejected with `the script did not call np() or newamp()`.

## Your first script, line by line

```lua
local function main()
    -- 1. Create the pedal exactly once, at load time.
    local pedal = np("Clean Boost")

    -- 2. Declare controls in the order they will be passed to process().
    pedal.instance(SLIDER, "gain",  0.0, 2.0, 1.0)
    pedal.instance(SLIDER, "level", 0.0, 1.0, 0.7)

    -- 3. Assign the DSP callback.
    pedal.process = function(x, n, gain, level)
        for i = 1, n do
            x[i] = x[i] * gain * level
        end
        return x
    end
end

-- 4. Actually run main() at the end of the file.
main()
```

Forgetting step 4 is a common mistake: the script loads without errors, but the app reports that `np()` / `newamp()` was never called.

## Where to go next

- [Lua API Reference](Lua-API-Reference.md) — all globals and methods.
- [Audio Callback and DSP](Audio-Callback-and-DSP.md) — how `process(x, n, ...)` really works.
- [Examples](Examples.md) — more complete scripts.
