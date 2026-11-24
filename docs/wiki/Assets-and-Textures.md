# Assets and Textures

Lua plugins can ship PNG/JPEG assets and render them on the pedal card. Images work in:

- **Folder plugins** (`Plugins/my_fuzz.lua` + `my_fuzz_assets/` or `assets/`)
- **`.pctp` packages** (ZIP with `assets/` or `<name>_assets/` inside)
- **`.pp` plugin packs** (packs containing `.pctp` or plugin folders)
- **Editor test/preview** (images kept in memory before export)

## Folder/plugin layout

```
Plugins/
  my_fuzz.ptc
  my_fuzz.lua
  my_fuzz_assets/      # or assets/
    face.png
    knob.png
    led.png
    footswitch.png
```

## `.pctp` layout

```
plugin.ptc
my_fuzz.lua
assets/
  face.png
  knob.png
  led.png
  footswitch.png
```

## Loading images

Recommended modern form:

```lua
local art = loadassets("my_fuzz")
local face = art.loadimage("face.png")      -- also accepts art.imagepast().loadimage(...)
local knob = art.loadimage("knob.png")
```

`loadimage(name)` returns a texture ID (`0` if missing/unreadable/no texture cache). The lookup is tolerant: it tries the module folder, `<module>_assets/`, `assets/`, `images/`, the script stem assets folder, and finally a basename match inside the package/plugin root.

Load images at script load time, **never inside `process()`**.

## Assigning textures

```lua
if face ~= 0 then pedal.basetexture(face) end  -- alias: pedal.texture(face)
if knob ~= 0 then pedal.knobtexture(knob) end
if led  ~= 0 then pedal.ledtexture(led) end
if foot ~= 0 then pedal.footswitchtexture(foot) end
```

Supported methods on pedal/amp objects: `basetexture`/`texture`, `knobtexture`, `ledtexture`, `footswitchtexture`, `labeltexture`, `overlaytexture`.

The current UI renders these textures in the card: base covers the card, knob texture rotates with value, LED/footswitch textures are drawn in the footer/header, label/overlay are drawn when provided. Missing textures fall back to the procedural style.

## Cleanup

Texture references acquired by `loadimage()` are tracked by the Lua pedal and released when the pedal is destroyed. You do not need to free textures manually in Lua.

## See also

- [Lua API Reference](Lua-API-Reference.md) — method signatures.
- [Getting Started](Getting-Started.md) — plugin folder layout.
