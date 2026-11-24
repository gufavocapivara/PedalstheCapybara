# Troubleshooting

## The plugin does not appear in the app

| Check | Fix |
| --- | --- |
| Is there a `.ptc` file? | Only `.ptc` manifests are scanned. A lone `.lua` file is ignored. |
| Is `SCRIPT=` correct? | It must be the `.lua` file name relative to `Plugins/`, e.g. `SCRIPT=my_fuzz.lua`. |
| Are the keys correct? | `NAME`, `DESCRIPTION`, `SCRIPT` — keys are case-insensitive, one per line, `KEY=value`. |
| Wrong folder? | Both files must be directly in `Plugins/` next to the executable. |
| Added files while the app was running? | Use the plugin reload action or restart the app. |

## Load errors

| Message | Cause / fix |
| --- | --- |
| `the script did not call np() or newamp()` | The script finished without creating a pedal. Usually you defined `main()` but forgot the final `main()` call. |
| `define pedal.process = function(...) ... end` | You created the pedal but never assigned `process` on the returned table. Check for typos like `process = ...` on a different table. |
| `use np() ou newamp() apenas uma vez por script` | Two `np()`/`newamp()` calls in the same script. One script = one effect. |
| `maximum of 128 parameters` | Too many `instance()` calls; the hard limit is 128. |
| `parameter '<name>' does not exist` | `labels("mode", ...)` was called before `instance(..., "mode", ...)` or with a misspelled name. |
| A Lua syntax error / `unexpected symbol` | Check the editor message; it includes the line number. Remember Lua uses `--` for comments and `nil`, `true`, `false` in lowercase. |
| Compiling an amp fails | Use `Compile Amp` and `newamp("Name")`; `Compile and Create .ptc` requires `np("Name")`. |

Note that load-time errors are stored in the plugin status. The editor shows them immediately when you press **Test**, **Test as Amp**, or a compile button.

## Runtime errors and "broken" pedals

If `process()` throws (for example, indexing `nil`, dividing by zero in a bad expression, or calling a function that does not exist), the host records the message, marks the pedal as **broken**, and silences it. In the editor this appears as:

```
Test pedal runtime error: <message>
```

Common causes:

- reading past the block: `x[n + 1]` — only `x[1..n]` is valid;
- forgetting that `instance()` arguments arrive in declaration order — an extra/missing parameter shifts every value;
- using a library that is not available (`require`, `io`, `os`, `dofile`, `loadfile`);
- calling a method with colon syntax (`pedal:instance(...)`) — use dot syntax (`pedal.instance(...)`).

## The pedal is loaded but silent

1. **Power** — click the power button on the card (or make sure the test pedal was added enabled).
2. **Return value** — `process()` must return the sample table. Modifying `x` and returning `nil` discards your changes.
3. **NaN / Inf** — non-finite outputs are replaced with `0`. Check divisions by zero and `math.sqrt` of negative numbers.
4. **Parameter ranges** — check the actual value passed to `process()`; a `level` of `0` is a common typo.
5. **Amp** — amps are a separate library. An amp must be selected/active to be in the chain; it always runs after all pedals.
6. **Audio devices** — verify the input/output device and that signal is reaching the app (meters in the UI).

## Clicks, crackles, or unstable sound

| Symptom | Fix |
| --- | --- |
| Click when toggling | Normal bypass has an 8 ms crossfade; clicks usually mean state was not cleared. Clear filter memory, delay buffers and envelopes in `reset()`. |
| Zipper noise when turning a knob | Use `SLIDER` for continuous values. `BUTTONSPIN` is intentionally not smoothed. |
| Random crackles | Avoid allocating in `process()`: no `{}`, no string concatenation, no `table.insert`. Reuse the provided `x` table and preallocated buffers. |
| CPU meter high / dropouts | Reduce parameter recomputation per sample, remove `oversample(true)` if not needed, and keep loops short. |
| "Metallic fizz" on distortion | Call `oversample(true)`, filter before the clipper, and avoid extreme gain. |

## Timing and oversampling surprises

With `oversample(true)` the callback sees **twice** the sample rate and **twice** the block length. If your LFO or filter coefficients use a hard-coded sample size or a fixed rate, they will behave differently.

```lua
-- correct: works at any rate, with or without oversampling
phase = phase + rate / SAMPLE_RATE
```

If you need to know the original device rate, remember that `SAMPLE_RATE / 2` is the device rate while oversampling is active.

## Textures do not show up

- Images must be **PNG** or **JPEG** and live in `<script_name>_assets/` next to the `.lua` file.
- `loadimage()` returns `0` when the file is missing — check for `0` before assigning.
- The `module` argument of `loadassets()` is not used for path resolution; the `_assets` folder name is what matters.
- Do not load images inside `process()` (file I/O on the audio thread).
- The current stock UI draws knobs and cards procedurally, so texture IDs may not visibly change the rendering. Use `color()`, `labels()`, and `size()` for reliable visual customization.

See [Assets and Textures](Assets-and-Textures.md) for details.

## Still stuck?

Compare your script with the working templates in the editor and with `Plugins/pedal_american.lua`. Reduce the script to a single gain stage, confirm it makes sound, then add features back one at a time.
