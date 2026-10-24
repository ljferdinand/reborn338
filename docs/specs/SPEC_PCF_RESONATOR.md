# PCF resonator: behavioural specification for the implementer

You are re-deriving the PCF's resonator to match measured ReBirth behaviour. Work
from THIS document, `docs/research/PCF_FINDINGS.md` (sections 15-18 especially), the
measured audio under `renders/pcf-surface-2026-09-11/`, `renders/pcf-hires-2026-09-11/`
and `renders/pcf-sweep-2026-09-11/`, the analyzers `tools/measure/pcf_surface.py`
and `tools/measure/delay_taps.py`-style tools, and the existing `reborn/` source
(`src/effects/pcf.cpp`). Those are your only inputs. Every number is measured from
ReBirth's audio output; fit your OWN coefficients to the measured curves.

## What is already correct and MUST NOT regress

The shipped PCF matches ReBirth on a lot; the re-derivation is targeted, not a
teardown. Keep these:

- Static LP magnitude at **low/mid cutoff (Freq <= ~90) and low/mid Q (<= band 4)**
  already matches to ~0.3 dB. Do not move it.
- The cutoff law incl. the high-Freq top-bend at low Q (PCF_FINDINGS 14), the
  decay law incl. the top-band hold (15), the bandpass tap, and Amt 0.95 are all
  shipped and correct.
- Non-PCF renders must stay byte-identical.

## The defect to fix (measured)

At HIGH cutoff and HIGH Q our resonator is 10-15 dB UNDER-resonant, and its corner
runs high. ReBirth's resonant-peak prominence GROWS with cutoff; ours is flat.
Measured surface (static, Amt 0, broadband; `pcf_surface.py` on
`renders/pcf-surface-2026-09-11`), resonant-peak PROMINENCE in dB, ReBirth target:

| Q band byte | Freq 96 | 108 | 116 | 122 |
|---|---|---|---|---|
| 24 (band 1) | 1.1 | 1.6 | 1.9 | 2.4 |
| 56 (band 3) | 8.4 | 9.8 | 10.3 | 11.1 |
| 88 (band 5) | 17.9 | 19.7 | 20.3 | 21.5 |
| 120 (band 7) | 27.8 | 29.8 | 30.6 | 32.2 |

Ours today is roughly flat per band (~1 / ~5 / ~11 / ~17 dB), i.e. it does not
grow with cutoff and is far short at high Q. CORNER (resonant peak Hz), ReBirth
target, is close through Freq 108 but ours runs high at 116-122 (e.g. band 7 Freq
122: ReBirth 12473, ours 15918). Full ReBirth corner surface from the same tool.

Independent confirmation to hit: `renders/pcf-hires-2026-09-11` (Q112 prominence
27.8 -> 32.2 across Freq 96..122) and the swept chirp `renders/pcf-sweep-2026-09-11/psw_a110`
(ReBirth's resonant peak descends prominently 14341 -> 11843 -> 9733 -> 6460 ->
3316 Hz over 17-300 ms; ours' tracked peak sits ~4-6 kHz and faint).

## Model structure (justified by measurement)

- **2-pole** resonator: the rolloff is ~12 dB/oct (PCF_FINDINGS 15). Keep the order.
- **Resonance is QUANTISED to 8 bands of 16 bytes** (PCF_FINDINGS 15): the transfer
  is identical for all bytes within a band, with steps at 16/32/.../112. Implement
  8 discrete resonance settings selected by the Q byte's band, not a continuous
  `1/Q`. Fit each band's coefficients to that band's measured transfer.
- The resonator must produce a resonant-peak prominence that **grows with cutoff**
  to match the surface above (our current SVF loses sharpness toward high fc,
  likely the `tan(pi*fc/sr)` prewarping; ReBirth holds a sharp peak up high).
  Whether you achieve this by a cutoff-dependent damping in the existing SVF, a
  different 2-pole structure, or fitted per-(band, cutoff) coefficients, is your
  choice, driven by the measured surface. Also correct the high-Freq corner so the
  peak lands where ReBirth's does (fold any Q-dependent corner shift into the fit).

## Acceptance tests

Regenerate/render the surface, hires and sweep fixtures with your resonator and
compare with the SAME tools against the ReBirth renders:

- **Prominence surface** (`pcf_surface.py`, Freq 96-122 x the four Q bands): within
  ~2 dB of the ReBirth table above, and it must GROW with cutoff like ReBirth's.
- **Corner**: within ~5 percent of ReBirth across the surface.
- **Static low/mid preserved**: Freq 64 and Q <= band 4 unchanged from the current
  shipped behaviour (measure before/after; the low/mid must not move).
- **Swept chirp** (`psw_a110`, `psw_a090`): the tracked resonant peak now descends
  prominently from the top like ReBirth (use a peak-frequency + prominence
  trajectory), closing the "flangy chirp".
- `ctest` green; a non-PCF render (any drum/303 or a PCF-off fixture) byte-identical.

## Non-goals / constraints

- Not bit-exact (ReBirth exports are not sample-deterministic). Behavioural parity
  to the tolerances above.
- Fit your own coefficients to the measured curves; seed nothing from elsewhere.
- Do not regress the shipped low/mid static, the cutoff law, the decay, the BP tap
  or Amt. Keep the envelope/clock/preset code as is; this is a resonator + Q-banding
  change.
- Work on a branch, behind a flag if useful for A/B, engine building throughout.

## Deliverable

The code, plus a report: the resonator structure and per-band coefficients you fit,
a prominence+corner surface table of yours vs ReBirth with pass/fail, the swept-
chirp trajectory comparison, confirmation the low/mid static and non-PCF renders
are unchanged, and the `ctest` result.
