# Delay: behavioural specification for the implementer

You are implementing the delay to match measured ReBirth behaviour. Work from
THIS document, `docs/research/PLAN_DELAY.md` (the measured facts), the measured audio in
`renders/delay-taps-2026-09-10/`, and the existing `reborn/` source. Those are
your only inputs. Every requirement below is a fact measured from ReBirth's
audio output; fit your own arithmetic to the measured numbers.

## What the current delay gets wrong

`src/effects/delay.cpp` today invents behaviour ReBirth does not have. Measurement
(PLAN_DELAY.md) shows all of the following must go:

- a full ping-pong (first echo one side, feedback crossing channels),
- a one-pole feedback high-cut swept 200..18000 Hz,
- a 35 ms smoothstep crossfade on time changes,
- a feedback law capped at `0.95 * byte/127`.

None of these appear in ReBirth's exported echoes. Replace the DSP with the
measured behaviour below. Leave the decode/routing/engine wiring in place; the
delay is a per-channel send effect fed by the mixer echo send.

## The measured behaviour to reproduce

1. **Mono delay line, integer length, no interpolation.** One mono ring, echo
   read at an integer offset `N` behind the write. Length depends on Steps, the
   grid byte, and tempo. Measured lengths (samples, 44100 Hz):
   - triplet grid (DELY byte 2 = 0), 120 bpm: Steps 1/2/3/4/6/8 -> 7350 / 14700 /
     22050 / 29400 / 44100 / 58800.
   - straight grid (byte 2 = 1), 120 bpm: Steps 1/2/4/8/16 -> 5512 / 11025 /
     22050 / 44100 / 88204.
   - straight Steps 4 across tempo: 26460 (100 bpm), 22050 (120), 21512 (123).
   Fit an **integer sample-count arithmetic** that reproduces every one of these
   exactly, including the truncation that makes Steps 16 read 88204 (not 88200)
   and 123 bpm read 21512 (a smooth `Fs*60/bpm*Steps/N` gives 21513.6, which is
   wrong). The grid byte selects divide-the-beat-by-3 (byte 0, triplet) versus
   by-4 (byte nonzero, straight); note the byte sense is inverted from the name.

2. **Feedback = byte / 127**, linear, reaching exactly 1.0 at byte 127. Measured
   successive-tap ratios: byte 32 -> 0.252, 64 -> 0.504, 96 -> 0.756, 127 -> 1.00.
   At 127 the train does not decay. Do not cap below 1.0.

3. **Wet return level = the exponential mixer fader applied to the echo-send
   byte.** echo1/dry measured 0.000 / 0.009 / 0.035 / 0.267 / 0.317 at send
   0 / 16 / 48 / 96 / 100, a 0.37 dB/byte slope near the top, the same fader law
   as the mixer volume (see PARITY.md). Reuse that law; do not invent a linear
   send.

4. **Return pan = the existing mixer pan law applied to the wet return**, and the
   two channels of the return are the same signal panned, NOT a ping-pong. echo1
   and echo2 sit on the SAME side (verified: `delay_taps.py <dir> dl_pan000
   29400` shows both taps hard left). Measured echo1 (L, R): pan 0 (0.058, 0),
   32 (0.058, 0.015), 64 (0.058, 0.058), 96 (0.015, 0.058), 127 (0, 0.058). The
   far/near ratio matches the mixer pan law (`-45.9*((byte-64)/63)^2` dB). Reuse
   that law. The feedback path does not cross channels.
   **Corrected 2026-09-27:** the far side is NOT silent at the extremes. The "0"
   above is a 3-decimal reading, which cannot resolve -47 dB; a least-squares
   read of the dct_mirror and dr_even exports finds a coherent far-side copy at
   -46.8 dB at pan 127 (BACKLOG 19). Our return, through `panGains`, is silent
   there. A pan-law change, pending sign-off.

5. **No filtering in the echo path.** The echo's spectral centroid equals the dry
   hit (868 dry vs 878 echo, unchanged across repeats). No tone control, no
   high-cut, no lowpass on the repeats.

## Model structure (what reproduces the measurements)

A mono ring buffer. Per sample: `read = (write - N) mod len`; add the delayed
sample times the return gains to the L and R mix (return gain = fader(send) split
by the pan law); write `send_in + ring[read] * feedback` back into the ring. No
interpolation, no filter, no channel crossing, no crossfade. `N`, `feedback`,
`fader`, and the pan split are the four measured laws above. This is the whole
DSP.

## Acceptance tests

Regenerate the fixtures with `python tools/rbsgen/gen_delay_taps.py`, render each
with your delay (`reborn_render --song <fixture> --samples samples-standard-909`),
and compare against `renders/delay-taps-2026-09-10/` with the SAME tool,
`tools/measure/delay_taps.py`:

- **Length:** first-echo offset matches the measured samples above for every
  `dl_s*`, `dss_s*`, `dl_t*`, `dss_t*`, `dl_trip` fixture, exactly (integer).
- **Feedback:** `dl_fb032/064(=dl_s04)/096/127` successive-tap ratio within ~0.02
  of byte/127.
- **Pan:** `dl_pan000/032/064/096/127` echo1 (L, R), sampled at the known N
  (`delay_taps.py <dir> dl_pan000 29400`), matches the measured split within
  ~1 dB, and echo2 is on the same side as echo1 (no ping-pong).
- **Filter:** echo centroid within a few percent of the dry centroid (no
  darkening) on `dl_s04`.
- **Send:** echo1/dry across `dl_ref, dsn_016/048/096, dl_s04` tracks the fader
  law.
- `ctest` stays green, and a non-delay render (any existing fixture) is unchanged.

## Non-goals and constraints

- Not bit-exact (ReBirth exports are not sample-deterministic). Behavioural parity
  to the tolerances above is the target.
- Fit your own integer length arithmetic and reuse the existing fader and pan
  laws. Do not seed any constant from anywhere but the measured numbers here.
- Do not add filters, saturators, ping-pong, or crossfades the measurements do
  not show.
- Work on a branch, behind a flag if the old behaviour must stay runnable for
  comparison, and keep the engine building and the rest of the project intact.

## Deliverable

The code, plus a short report: the integer length formula you fit and its match
to the measured samples, the feedback/pan/send laws used, a parity table of your
render versus `renders/delay-taps-2026-09-10/` on the tests above, and
confirmation that `ctest` passes and non-delay renders are unchanged.
