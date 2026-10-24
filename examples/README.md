# Example songs

These four files are the repository's only songs. `make_examples.py` writes all
of them from Reborn's own song generator (`tools/rbsgen/rbsgen.py`), so no byte
in them comes from ReBirth or from a song that shipped with it. Run it again to
regenerate them; the output is deterministic.

| File | What it plays |
|---|---|
| `acid_line.rbs` | a 303 line with slides and accents over an 808 beat, the 303 sent to the delay, 8 bars at 128 BPM |
| `beat_909.rbs` | a 909 beat through the pattern-controlled filter, 8 bars at 132 BPM |
| `bass_303.rbs` | one square-wave 303 through the distortion, 8 bars at 120 BPM |
| `acid_line_open303.rbsx` | `acid_line.rbs` as a Reborn session whose first 303 slot uses the Open303 voice |

Render one without the app:

```sh
./build/Release/reborn_render --song examples/acid_line.rbs --seconds 16 --out acid_line.wav
```

On Linux and macOS the tools are in `build/` rather than `build/Release/`. The
`.rbs` files also open in the app and in ReBirth itself. The `.rbsx` session is
Reborn's own format (`docs/FORMAT_RBSX.md`) and opens only in the app, which
reads the voice choice from it; `reborn_render` reads `.rbs` only.
