# Delay (supertask 5): measured facts

Provenance: every fact here is **measured from ReBirth exports** (clean-room).
Experiment selection was informed by third-party binary analysis; no constant
from that analysis is copied here (`docs/PROVENANCE.md`). Campaign for the first
round: `fixtures-2026-09-10/delay-taps/`, `renders/delay-taps-2026-09-10/`,
`tools/rbsgen/gen_delay_taps.py`, and a scratch analysis script that was not
kept. This is Role A output (measured facts); the DSP fit is a later, walled step.

## P2 delay-tap probe, 2026-09-10

One 909 rimshot per bar routed to the delay (echo send 100), everything else off,
comp/dist/PCF verified off, ratioed against `dl_ref` (send 0). Within a single
export the delay is deterministic, so tap offsets are exact.

### Delay time
`length = Steps * base`. At 120 bpm the base is **7350 samples on the triplet
grid** (DELY byte 2 = 0) and **5512.5 samples on the straight grid** (byte 2
nonzero). 7350 = a quarter / 3 (eighth-note triplet); 5512.5 = a quarter / 4
(sixteenth). Length scales as 1/bpm: Steps 4 triplet grid measured 39200 / 29400
/ 24500 samples at 90 / 120 / 144 bpm. Tap offsets are integer samples and no
fractional interpolation was seen.

Measured Steps sweep, triplet grid, 120 bpm:

| Steps | 1 | 2 | 3 | 4 | 6 | 8 |
|---|---|---|---|---|---|---|
| samples | 7350 | 14700 | 22050 | 29400 | 44100 | 58800 |

Measured Steps sweep, straight grid (byte 2 = 1), 120 bpm (base 5512.5 = a
sixteenth):

| Steps | 1 | 2 | 4 | 8 | 16 |
|---|---|---|---|---|---|
| samples | 5512 | 11025 | 22050 | 44100 | 88200 |

The per-step sample count is computed with integer truncation, not kept exact:
straight Steps 1 at 120 bpm is floor(5512.5) = 5512, and straight Steps 4 at
123 bpm is floor(21512.19) = 21512 (not the rounded 21513.6). Straight Steps 4
across tempo: 26460 at 100 bpm, 22050 at 120, 21512 at 123. (The straight-grid
full-bar point, Steps 16 at 120 bpm, is 88200 exactly; the probe's 88204 was the
echo landing on the next bar's rimshot, a bar-grid confound the threshold onset
detector picked up, confirmed by cross-correlation.) An exact integer
sample-count arithmetic `floor(Steps*Fs*60/(bpm*D))`, D = 3 triplet / 4 straight,
reproduces every one of these points; fit that to the measured samples.

**DELY byte 2 sense is inverted from intuition:** 0 selects the triplet grid
(divide the beat by 3), nonzero selects the straight grid (divide by 4). The
fixture generator's `triplet` field name follows the byte, so `triplet=0` is the
longer (triplet) grid. Established by measurement, not assumed.

### Feedback
**feedback = byte / 127**, linear, reaching exactly 1.0 at byte 127. Measured
echo2/echo1 ratios: byte 32 -> 0.26, 64 -> 0.50, 96 -> 0.76, 127 -> 1.00, versus
byte/127 = 0.252 / 0.504 / 0.756 / 1.0. At 127 the train does not decay. (Our
current model is `0.95 * byte/127` capped at 0.95, so it is about 5 percent low
and never reaches unity.)

### Pan is a static return pan, NOT ping-pong
echo1 and echo2 sit on the **same** side (no alternation), so there is no
ping-pong. The pan holds the near side at full level and attenuates the far side,
the same near-unity / far-attenuated shape as the mixer pan:

| pan byte | 0 | 32 | 64 | 96 | 127 |
|---|---|---|---|---|---|
| echo1 (L, R) | (0.058, 0) | (0.058, 0.015) | (0.058, 0.058) | (0.015, 0.058) | (0, 0.058) |

The attenuation is the **mixer pan law**, reused: at pan 32 the far/near ratio is
0.015/0.058 = -11.7 dB, which matches the mixer's -45.9*((byte-64)/63)^2 (-11.8 dB
at byte 32). So the return needs no delay-specific pan, only the existing pan law
applied to the wet return. (Our current model is a ping-pong with the feedback
crossing channels, which the same-side echoes contradict.)

**Corrected 2026-09-27.** This section said the far side is digitally silent at
the extremes, as the mixer pan does, which the table's "0" seemed to show. The
table has three decimals and cannot resolve -47 dB. Read by least squares, the
dct_mirror and dr_even exports carry a coherent copy on the far side at -46.8 dB
at pan 127, close to the curve's -45.9 at x = 1, where our return is digital
zero. Whether the mixer's own end cut has the same resolution limit is open, and
any change is a pan-law parity change (BACKLOG 19).

### No filtering in the echo path
Echo spectral centroid equals the dry hit: 868 Hz dry, 878 Hz echo1, 878 Hz
echo2. No tone or lowpass on the repeats. (Our current model sweeps a one-pole
feedback high-cut 200..18000 Hz, which this contradicts.)

### Send / return gain
The echo send is the **exponential mixer fader**, not a linear send. echo1/dry vs
send byte (triplet Steps 4): 0 -> 0.000, 16 -> 0.009, 48 -> 0.035, 96 -> 0.267,
100 -> 0.317. From byte 96 to 100 that is +1.5 dB over 4 bytes = 0.37 dB/byte,
the same slope as the mixer volume fader (PARITY.md). So the wet-return level is
`fader(send_byte)` applied to the delay output, using the fader law we already
have; echo1 is independent of feedback, as expected for a send return.

## Gaps / next probes
- Bypass / tail behaviour when the delay is toggled off versus transport stop
  (needs a timed-automation fixture; the wet ring may or may not be cleared).
- Higher-resolution send and pan curves if the fader/pan-law reuse does not hold
  to tolerance during implementation (the five points here already match the
  existing laws).
- Feedback path stereo behaviour at high feedback (does the pan of later echoes
  drift, or stay put as the two measured taps show).

These measured facts feed the delay behavioural spec for the walled implementer
(`docs/specs/SPEC_DELAY.md`).

## Implementation and Role A review (2026-09-10)

Implemented on branch `delay-cleanroom` by the walled implementer (a fresh
context given only `SPEC_DELAY.md`, this doc, the measured audio, and the source;
no access to the binary analysis). It rewrote `src/effects/delay.cpp` to a mono
integer ring with `length = floor(Steps*Fs*60/(bpm*D))` (D = 3 triplet / 4
straight), feedback = byte/127 uncapped, the wet return via the existing mixer
fader on the echo send, and the return pan via the existing mixer pan law; it
removed the ping-pong, the feedback high-cut and the crossfade, and read the
DELY +2 (grid) and +4 (pan) bytes that were previously dropped.

Role A independently re-rendered and measured (not trusting the implementer's
own report), against `renders/delay-taps-2026-09-10`:

- length exact incl. the truncation case (dl_s04 = 29400, dss_t123 = 21512);
- feedback = byte/127 exact (dl_fb096 tap ratio 0.756, dl_fb127 1.000);
- return level 0.317 at send 100; pan echoes both on one side (no ping-pong),
  matching the mixer pan law within ~0.3 dB;
- echo centroid = dry (no darkening);
- a non-delay render is BYTE-IDENTICAL between the new binary and the pre-delay
  binary, so the change is confined to the delay path;
- `ctest` green.

PROVENANCE NOTE: the implementer's length formula was derived by fitting to the
measured samples, by a context that never saw the analysis, so its source is
measurement.

Merged into `pcf-supertask` (the active integration line) 2026-09-10. Still
pending: Luke's `main` sign-off, and the two residual gaps (the DELY automation
track, and bypass/tail behaviour) which remain documented in "Gaps" above.
