# UI Customization

Every control you declare with `instance()` becomes a widget on the pedal/amp card. This page covers widget types, labels, colors, sizes, and library tabs.

## Widget types

| Constant | Value | Widget | Behavior |
| --- | --- | --- | --- |
| `SLIDER` | `2` | Knob | Continuous value. Drag vertically, use the mouse wheel, double-click to return to the default, hold `Shift` for fine adjustment. |
| `BUTTONSPIN` | `1` | Button | ON/OFF if the range is ≤ 1.5, an integer `+/-` counter if the range is larger, or a mode selector when `labels()` is used. Left-click advances, right-click goes back, mouse wheel adjusts. |

```lua
pedal.instance(SLIDER, "drive", 0.0, 1.0, 0.4)
pedal.instance(BUTTONSPIN, "bypass-ish", 0, 1, 0)      -- ON/OFF
pedal.instance(BUTTONSPIN, "voicing", 0, 3, 0)         -- integer counter 0..3
```

A `BUTTONSPIN` parameter is marked **discrete**, so the host does not smooth it. This prevents fractional values from selecting a wrong mode in your `process()` function.

## Labeled mode selectors

Use `labels(name, list)` to turn a `BUTTONSPIN` into a cycling selector:

```lua
amp.instance(BUTTONSPIN, "mode", 0, 3, 0)
amp.labels("mode", { "CLEAN", "CRUNCH", "OD1", "OD2" })
```

- The button displays the name of the current position and cycles through the list.
- The displayed index is `round(value - min)`, so use `min = 0` and positions `0, 1, 2, ...`.
- The callback still receives the number (`0..3` above), not the string.
- `labels()` also changes how values are formatted on knobs, but labeled knobs are drawn as buttons by the UI.

## Colors

`color(r, g, b)` sets the accent used for the card header, border, knobs, and active buttons. Values are `0..255`; out-of-range values are clamped.

```lua
pedal.color(220, 60, 40)     -- warm red fuzz
amp.color(20, 20, 24)        -- nearly black amp head
```

Without `color()`, a neutral gray accent is used.

## Card size

`size(width, height)` sets the amp card dimensions in UI units:

```lua
amp.size(440, 220)
```

- The default `0, 0` lets the app compute a size from the number of parameters.
- Pedal cards use a fixed width (three knob columns); `size()` is intended for amps.
- Both dimensions are clamped to `0..4096` and a minimum usable size is enforced by the UI.

## Library tabs

Use `newassetsbar()` together with `linkbar()` to group your pedal in the Library panel:

```lua
local tab = newassetsbar("Drives")
local pedal = np("Lua Fuzz")
pedal.linkbar(tab)
```

- `newassetsbar(label)` simply returns the label string.
- `linkbar(name)` accepts any string; it becomes the pedal's category/tab.
- Without `linkbar()`, Lua pedals appear in the **Plugins** tab.
- Amps are always registered in the **Amps** tab; `linkbar()` does not change that.

Manually installed plugins get their library name from the script, while the `.ptc` manifest controls the plugin browser entry. See [Getting Started](Getting-Started.md).

## Images on cards

The texture API (`basetexture`, `knobtexture`, `ledtexture`, `footswitchtexture`) is documented in [Assets and Textures](Assets-and-Textures.md). Note that the stock card renderer currently draws controls procedurally, so setting texture IDs may not change the appearance in every build. Colors, labels, and sizes always apply.

## Related pages

- [Lua API Reference](Lua-API-Reference.md)
- [Examples](Examples.md)
