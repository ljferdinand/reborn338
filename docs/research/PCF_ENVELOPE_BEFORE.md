# PCF envelope-to-cutoff: BEFORE measurements (baseline capture)

Captured 2026-09-13 on branch `pcf-envelope-cleanroom` BEFORE the envelope->cutoff
law change, so the "unchanged from before" acceptance can be checked against real
numbers rather than reasoning. Resonator ON (REBORN_PCF_RESO default), the state
this branch starts in. Analyzer `tools/measure/pcf_trajectory.py`, one method,
against my own `psw_ref` render (12 s renders of the pcf-sweep fixtures). Peak
cutoff (Hz) at 17/40/80/150/300 ms after a trigger.

ReBirth reference (renders/pcf-sweep-2026-09-11), same analyzer:

| fixture | 17 | 40 | 80 | 150 | 300 |
|---|---|---|---|---|---|
| psw_a040 | 1507 | 1981 | 1550 | 1852 | 2196 |  (prominence negative: peak floors out, unresolvable)
| psw_a070 | 2412 | 2110 | 1895 | 1507 | 2196 |
| psw_a090 | 6934 | 6029 | 4953 | 3488 | 1895 |
| psw_a110 | 14341 | 13049 | 9733 | 6675 | 3316 |
| psw_a127 | 14341 | 13049 | 9733 | 6675 | 3316 |  (== a110: Amt saturated)
| psw_f010 | 6288 | 5383 | 4522 | 2799 | 1593 |
| psw_f050 | 14600 | 13092 | 11154 | 8269 | 4651 |
| psw_f070 | 15116 | 13910 | 12231 | 9905 | 6245 |

Ours BEFORE (additive + hard clamp, kEnvDepth 0.95), same analyzer:

| fixture | 17 | 40 | 80 | 150 | 300 |
|---|---|---|---|---|---|
| psw_a040 | 1550 | 1593 | 1723 | 1637 | 1723 |  (floored, as ReBirth; unresolvable band)
| psw_a070 | 2670 | 2498 | 2067 | 1593 | 1723 |
| psw_a090 | 8096 | 7149 | 5857 | 3962 | 2110 |
| psw_a110 | 15719 | 15719 | 15719 | 10724 | 4867 |  PLATEAU through 80 ms
| psw_a127 | 15719 | 15719 | 15719 | 15762 | 9001 |  PLATEAU through 150 ms
| psw_f010 | 7580 | 6718 | 4953 | 3445 | 1550 |
| psw_f050 | 15719 | 15719 | 15719 | 15762 | 14815 | PLATEAU almost the whole tail
| psw_f070 | 15719 | 15719 | 15719 | 15762 | 15633 | PLATEAU the whole tail

The plateau at ~15720 Hz on the four clamp fixtures (a110/a127/f050/f070) is the
defect: the additive sum exceeds 1.0 at the trigger and fc pins at the ceiling
until env decays back under the clamp. a040/a070/a090 do not clamp and are the
"must not move" set. a040 floors out on both sides (its true peak descends below
the tracker's 1.5 kHz band), so it carries little information; a070/a090 are the
usable low/mid checks.

---

# AFTER (headroom-multiplicative law), same analyzer, same references

Ours AFTER (cparam = base + 0.988*min(1.259*amt,1)*env*(1-base)), vs my psw_ref:

| fixture | 17 | 40 | 80 | 150 | 300 | vs ReBirth (worst) |
|---|---|---|---|---|---|---|
| psw_a040 | 1550 | 1593 | 1723 | 1637 | 1723 | identical to BEFORE (floored) |
| psw_a070 | 2670 | 2498 | 2067 | 1593 | 1723 | identical to BEFORE |
| psw_a090 | 8096 | 7149 | 5857 | 3962 | 2110 | identical to BEFORE |
| psw_a110 | 15073 | 13221 | 10422 | 6718 | 3273 | +5.1/+1.3/+7.1/+0.6/-1.3% |
| psw_a127 | 15073 | 13221 | 10422 | 6718 | 3273 | == a110 (Amt saturated) |
| psw_f010 | 14944 | 12963 | 9388 | 5857 | 2369 | overshoots (RB ~6.3 kHz); see caveat |
| psw_f050 | 15073 | 13221 | 11025 | 8312 | 4522 | +3.2/+1.0/-1.2/+0.5/-2.8% |
| psw_f070 | 15375 | 14040 | 12274 | 9733 | 6245 | +1.7/+0.9/+0.4/-1.7/0.0% |

Verdict: the four clamp fixtures (a110/a127/f050/f070) descend immediately with
NO plateau and track ReBirth within 15% (worst +7.1% at a110 80 ms); a110 == a127
(Amt saturation). a040/a070/a090 peak-identical to BEFORE (the coefficient at
base 30 differs from the old law by ~4e-5, sub-Hz in cutoff). Resonator surface
byte-identical (Amt 0). ctest green. psw_ref byte-identical before/after.

CAVEAT: psw_f010 (base 10, Amt 110) is outside the base-30/50/70 fit and the
acceptance set; the headroom law overshoots there (14944 vs RB 6288 at 17 ms)
because RB's reach FALLS at very low base rather than rising with headroom. Not
modelled here; see PCF_FINDINGS section 21.

---

# AFTER v2 (DEPTH-CAPPED law), same analyzer, same references

The headroom "AFTER" section above is SUPERSEDED: a dense base sweep
(renders/pcf-envsweep-2026-09-13, PCF_FINDINGS 22) showed the headroom law
over-opened at low base. The shipped law is now the depth-cap
`cparam = base + min(gcap(amt), 0.99*(1-base)) * env` (no clamp), gcap = 0.95*amt
up to Amt 90 then slope 0.773 to ~0.795 at Amt 110.

Base sweep at Amt 110, ours (depth-cap) vs ReBirth, peak cutoff Hz at 17/40/80/150/300 ms:

| base | ours 17 | 40 | 80 | 150 | 300 | worst err vs RB |
|---|---|---|---|---|---|---|
| b005 |  4694 |  3962 |  3187 | 2196 | 1723 | <=3% (300 ms floored both) |
| b010 |  6417 |  5383 |  4264 | 2756 | 1680 | -5.7% (80 ms); RB 6288/5383/4522/2799/1593 |
| b015 |  8269 |  6934 |  5383 | 3488 | 1680 | -4.2%; RB 8183/7235/5469/3488/1723 |
| b020 | 11499 |  9690 |  7494 | 4953 | 2369 | +7.9% (300 ms); RB 11240/9690/7666/4953/2196 |
| b025 | 15418 | 13221 | 10164 | 6761 | 3144 | +7.5% (17 ms); RB 14341/13049/9690/6632/3058 |
| b030 | 15418 | 13221 | 10465 | 6761 | 3273 | +7.5%; RB 14341/13049/9733/6675/3316 |
| b040 | 15418 | 13221 | 10853 | 7537 | 3747 | +7.5% (17), -8.4% (300); RB 14341/13049/10422/7494/4091 |
| b050 | 15332 | 13781 | 11499 | 8312 | 4522 | +5.3%; RB 14600/13092/11154/8269/4651 |
| b064 | 15418 | 13997 | 11843 | 9173 | 5771 | +2.0%; RB 15116/13910/11843/9302/5857 |
| b080 | 15418 | 14556 | 12705 | 10637| 7278 | -2.9%; RB 15848/14600/12791/10508/7494 |

All base 5..80 within ~8.5%, no plateau anywhere. b100/b110 (base > 80) are outside
the acceptance range (b110 reads a flat ~8.8 kHz in RB that neither law reproduces).

Clamp fixtures, ours vs RB:

| fixture | ours 17/40/80/150/300 | worst err |
|---|---|---|
| psw_a110 | 15418/13221/10465/6761/3273 | +7.5% (RB 14341/13049/9733/6675/3316) |
| psw_a127 | 15418/13221/10465/6761/3273 | byte-identical to a110 (headroom limit) |
| psw_f050 | 15332/13781/11499/8312/4522 | +5.3% (RB 14600/13092/11154/8269/4651) |
| psw_f070 | 15418/14083/12102/9776/6245 | +2.0% (RB 15116/13910/12231/9905/6245) |

Low-base Amt sweep (base 15), ours: a090 3445 / a110 8269 / a127 14729 at 17 ms
(grows through 127, no saturation). RB: 2929 / 8183 / 14341; a090 runs +17.6% (see
caveat 1 in FINDINGS 22 - gcap(90) pinned to the shipped depth to keep psw_a090
byte-identical), a110/a127 within ~3%.

Low/mid psw_a040/a070/a090 BYTE-identical to the shipped baseline. Resonator
surface byte-identical (Amt 0). psw_ref (non-PCF) byte-identical. ctest green.
