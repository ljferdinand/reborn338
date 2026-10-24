# Environment variables

Reborn's code reads a number of `REBORN_*` environment variables. One is meant
for users: `REBORN_BASS_ENGINE` chooses the 303 voice in `reborn_render`. The rest
are measurement probes left in the code from the parity work. Each one overrides
a constant, or switches in an alternative that was measured and rejected, so that
a candidate change could be rendered and measured against ReBirth without
rebuilding. With none of them set, Reborn plays its shipped, measured values,
which are the defaults listed here.

Two cautions. Setting a probe changes how Reborn sounds, and measurements made
that way are not comparable with anything recorded in the docs. And most of the
TPT voice's probes are read each time the code that uses them runs, many of them
once per audio block, which is not safe in a real-time host
(`docs/research/REVIEW_2026-09.md`, item RT1): `docs/HANDOVER.md` lists reading
them once, or removing the concluded ones, as open work. The comment beside each
read in the code says what the probe tested and what was found.

The list below was generated from the code at the release. A default is the
value the code falls back to when the variable is unset; where it is blank the
variable is a switch, described in words.

## For users

| Variable | Default | Read at | Notes |
|---|---|---|---|
| `REBORN_BASS_ENGINE` |  | `tools/render/main.cpp:142` | `tpt` (default) or `open303`: the voice in both 303 slots |

## The 303 TPT voice: amplifier, gate and slide

| Variable | Default | Read at | Notes |
|---|---|---|---|
| `REBORN_GATE_FRAC` | `0.500` | `src/devices/bass_open303.cpp:164` | the fraction of a step a non-tied 303 note is held, shared by both voices |
| `REBORN_AMP_DECAY` | `0.0` | `src/devices/bass_synth.cpp:128` |  |
| `REBORN_AMP_SUS` | `0.08` | `src/devices/bass_synth.cpp:129` |  |
| `REBORN_AMP_ATK` | `0.0012` | `src/devices/bass_synth.cpp:781` |  |
| `REBORN_AMP_REL` | `0.02192` | `src/devices/bass_synth.cpp:789` |  |
| `REBORN_GLIDE` | `0.018` | `src/devices/bass_synth.cpp:806` |  |
| `REBORN_GATE_PIVOT` | `0.0` | `src/devices/bass_synth.cpp:1228` |  |

## The 303 TPT voice: envelope and env mod

| Variable | Default | Read at | Notes |
|---|---|---|---|
| `REBORN_EM_OCT` | `4.45` | `src/devices/bass_synth.cpp:419` |  |
| `REBORN_ENV_ATK` | `0.002` | `src/devices/bass_synth.cpp:595` |  |
| `REBORN_ENV_OVER` | `1.10` | `src/devices/bass_synth.cpp:596` |  |
| `REBORN_ENV_SHAPE` | `1.0` | `src/devices/bass_synth.cpp:597` |  |
| `REBORN_ENV_DEC_MUL` | `1.0` | `src/devices/bass_synth.cpp:598` |  |
| `REBORN_DEC_A` | `0.070` | `src/devices/bass_synth.cpp:663` |  |
| `REBORN_DEC_B` | `0.901` | `src/devices/bass_synth.cpp:664` |  |
| `REBORN_DEC_C` | `0.584` | `src/devices/bass_synth.cpp:665` |  |
| `REBORN_EM_CURVE` | `0.0` | `src/devices/bass_synth.cpp:1397` |  |
| `REBORN_EM_RESO` | `0.0` | `src/devices/bass_synth.cpp:1410` |  |
| `REBORN_EM_FLOOR` | `0.85` | `src/devices/bass_synth.cpp:1460` |  |
| `REBORN_ENV_DOMAIN` |  | `src/devices/bass_synth.cpp:1517` | `hz` applies env mod in hertz rather than octaves (an A/B of a rejected alternative) |
| `REBORN_EM_CENTRE` | `0.45` | `src/devices/bass_synth.cpp:1628` |  |
| `REBORN_K_ENVFLOOR` | `1.0` | `src/devices/bass_synth.cpp:1779` |  |
| `REBORN_K_EMTRADE` | `0.0` | `src/devices/bass_synth.cpp:1785` |  |

## The 303 TPT voice: accent

| Variable | Default | Read at | Notes |
|---|---|---|---|
| `REBORN_ACC_LATCH_EXPIRE` |  | `src/devices/bass_synth.cpp:260` | non-zero lets an accent latch expire (a rejected alternative, kept for A/B) |
| `REBORN_ACC_CORNER_SPLIT` |  | `src/devices/bass_synth.cpp:292` | non-zero splits the accent's corner push on ties (a rejected alternative, kept for A/B) |
| `REBORN_ACC_CORNER_TIE` | `0.90` | `src/devices/bass_synth.cpp:297` |  |
| `REBORN_ACC_TIE` | `0.05` | `src/devices/bass_synth.cpp:329` |  |
| `REBORN_ACC_SHORT` | `0.24` | `src/devices/bass_synth.cpp:685` |  |
| `REBORN_ACC_SHORT_CUT` | `0.60` | `src/devices/bass_synth.cpp:723` |  |
| `REBORN_ACC_ATK` | `0.010` | `src/devices/bass_synth.cpp:771` |  |
| `REBORN_ACC_DEC` | `0.044` | `src/devices/bass_synth.cpp:773` |  |
| `REBORN_ACC_SWEEP` | `3.30` | `src/devices/bass_synth.cpp:1375` |  |
| `REBORN_ACC_RESO_SLOPE` | `-0.708` | `src/devices/bass_synth.cpp:1528` |  |
| `REBORN_ACC_SWEEP_CUT` | `0.0` | `src/devices/bass_synth.cpp:1548` |  |
| `REBORN_ACC_RESO` | `0.1` | `src/devices/bass_synth.cpp:1771` |  |
| `REBORN_ACC_LVL` | `3.2` | `src/devices/bass_synth.cpp:2055` |  |
| `REBORN_ACC_SUS` | `0.10` | `src/devices/bass_synth.cpp:2056` |  |
| `REBORN_ACC_FLAT_LATCH` |  | `src/devices/bass_synth.cpp:2071` | non-zero restores the accent's flat latch from before 2026-08-06 |

## The 303 TPT voice: cutoff, resonance and level laws

| Variable | Default | Read at | Notes |
|---|---|---|---|
| `REBORN_CUT_SCALE` | `0.78` | `src/devices/bass_synth.cpp:397` |  |
| `REBORN_CUT_SLOPE` | `2.9` | `src/devices/bass_synth.cpp:429` |  |
| `REBORN_K_A` | `0.0` | `src/devices/bass_synth.cpp:518` |  |
| `REBORN_K_B` | `3.40` | `src/devices/bass_synth.cpp:537` |  |
| `REBORN_K_B_SLOPE` | `0.5` | `src/devices/bass_synth.cpp:558` |  |
| `REBORN_K_B_PIVOT` | `0.50` | `src/devices/bass_synth.cpp:559` |  |
| `REBORN_K_CUT` | `0.0` | `src/devices/bass_synth.cpp:560` |  |
| `REBORN_K_CUT_TOP` | `0.87` | `src/devices/bass_synth.cpp:576` |  |
| `REBORN_HFQ` | `1.0` | `src/devices/bass_synth.cpp:908` |  |
| `REBORN_MAKEUP` | `1.0` | `src/devices/bass_synth.cpp:909` |  |
| `REBORN_LVL_CUT` | `8.4` | `src/devices/bass_synth.cpp:980` |  |
| `REBORN_LVL_RGATE` | `0.10` | `src/devices/bass_synth.cpp:981` |  |
| `REBORN_LVL_PIVOT` | `0.65` | `src/devices/bass_synth.cpp:982` |  |
| `REBORN_LVL_HI` | `0.0` | `src/devices/bass_synth.cpp:991` |  |
| `REBORN_LVL_HI_PIVOT` | `0.0` | `src/devices/bass_synth.cpp:992` |  |
| `REBORN_LVL_HI_ON` | `0.85` | `src/devices/bass_synth.cpp:993` |  |
| `REBORN_LVL_EM` | `1.0` | `src/devices/bass_synth.cpp:1029` |  |
| `REBORN_LVL_TRIM` | `1.46` | `src/devices/bass_synth.cpp:1058` |  |
| `REBORN_LF_HP` | `119.9` | `src/devices/bass_synth.cpp:1074` |  |
| `REBORN_LF_HP_KNEE` | `0.141` | `src/devices/bass_synth.cpp:1075` |  |
| `REBORN_LF_HP_MIN` | `0.0` | `src/devices/bass_synth.cpp:1094` |  |
| `REBORN_KHI_CEIL` | `2.45` | `src/devices/bass_synth.cpp:1100` |  |
| `REBORN_RESO_OCT` | `0.25` | `src/devices/bass_synth.cpp:1686` |  |
| `REBORN_RESO_OCT_CUT` | `0.35` | `src/devices/bass_synth.cpp:1707` |  |
| `REBORN_FC_MAX` | `9000.0` | `src/devices/bass_synth.cpp:1749` |  |
| `REBORN_HFQ_ON` | `4000.0` | `src/devices/bass_synth.cpp:1811` |  |
| `REBORN_HFQ_SPAN` | `2500.0` | `src/devices/bass_synth.cpp:1812` |  |
| `REBORN_SKIRT_ZERO` | `0.0` | `src/devices/bass_synth.cpp:2118` |  |

## The 303 TPT voice: oscillator, drive and filter structure

| Variable | Default | Read at | Notes |
|---|---|---|---|
| `REBORN_A_HP` | `0.0` | `src/devices/bass_synth.cpp:815` |  |
| `REBORN_OSC_HP` | `0.0` | `src/devices/bass_synth.cpp:832` |  |
| `REBORN_OSC_HS_GAIN` | `0.0` | `src/devices/bass_synth.cpp:844` |  |
| `REBORN_OSC_HS_FC` | `2000.0` | `src/devices/bass_synth.cpp:845` |  |
| `REBORN_B_GAIN` | `0.0` | `src/devices/bass_synth.cpp:848` |  |
| `REBORN_B_FC` | `4000.0` | `src/devices/bass_synth.cpp:849` |  |
| `REBORN_B_PIVOT` | `0.40` | `src/devices/bass_synth.cpp:850` |  |
| `REBORN_DRIVE` | `1.0` | `src/devices/bass_synth.cpp:853` |  |
| `REBORN_DRIVE_BIAS` | `0.9` | `src/devices/bass_synth.cpp:856` |  |
| `REBORN_SQR_DUTY` | `0.52286` | `src/devices/bass_synth.cpp:881` |  |
| `REBORN_SQR_GAIN` | `0.5493` | `src/devices/bass_synth.cpp:887` |  |
| `REBORN_DC_HP` | `5.0` | `src/devices/bass_synth.cpp:894` |  |
| `REBORN_TPT_OS` | `4` | `src/devices/bass_synth.cpp:905` |  |
| `REBORN_FB_HP` | `0.0` | `src/devices/bass_synth.cpp:1098` |  |
| `REBORN_FB_DRIVE` | `1.0` | `src/devices/bass_synth.cpp:1099` |  |
| `REBORN_OSC_SINE` | `0.0` | `src/devices/bass_synth.cpp:1101` |  |
| `REBORN_RAW_TAP` | `0.0` | `src/devices/bass_synth.cpp:1102` |  |
| `REBORN_U_SAT` | `1.0` | `src/devices/bass_synth.cpp:1106` |  |
| `REBORN_FB_SEP` | `1.0` | `src/devices/bass_synth.cpp:1124` |  |
| `REBORN_STAGE_NL` | `0.0` | `src/devices/bass_synth.cpp:1129` |  |
| `REBORN_POST_DRIVE` | `0.0` | `src/devices/bass_synth.cpp:1133` |  |
| `REBORN_TAP` | `0` | `src/devices/bass_synth.cpp:1211` |  |
| `REBORN_TAP_GAIN` | `0.0` | `src/devices/bass_synth.cpp:1212` |  |
| `REBORN_TAP_PIVOT` | `0.30` | `src/devices/bass_synth.cpp:1213` |  |
| `REBORN_VEL` | `0.0` | `src/devices/bass_synth.cpp:1222` |  |
| `REBORN_VEL_REF` | `0.30` | `src/devices/bass_synth.cpp:1223` |  |
| `REBORN_VEL_TC` | `5.0` | `src/devices/bass_synth.cpp:1224` |  |

## The 303 TPT voice: experimental filter cores (off by default)

`REBORN_FILTER=diode` and `REBORN_FILTER=rb` select two filter cores that were built and measured but never became the default; the comments at their reads and `docs/research/EXPERIMENTS.md` record why. The `RB_` probes tune the `rb` core, whose resonance skew and coefficients are adapted from Open303 under its MIT licence, and the `D_` probes tune the diode core. None has any effect unless its core is selected.

| Variable | Default | Read at | Notes |
|---|---|---|---|
| `REBORN_FILTER` |  | `src/devices/bass_synth.cpp:1136` | `diode` or `rb` selects an experimental filter core in place of the shipped one; unset is the shipped filter |
| `REBORN_RB_A` | `430.0` | `src/devices/bass_synth.cpp:1156` |  |
| `REBORN_RB_SLOPE` | `7.6300` | `src/devices/bass_synth.cpp:1157` |  |
| `REBORN_RB_CURVE` | `0.0` | `src/devices/bass_synth.cpp:1163` |  |
| `REBORN_RB_KBOOST` | `0.0` | `src/devices/bass_synth.cpp:1166` |  |
| `REBORN_RB_OS` | `2.0` | `src/devices/bass_synth.cpp:1167` |  |
| `REBORN_RB_KGAIN` | `30.0` | `src/devices/bass_synth.cpp:1172` |  |
| `REBORN_RB_RESP` | `1.3` | `src/devices/bass_synth.cpp:1179` |  |
| `REBORN_RB_KMAX` | `40.0` | `src/devices/bass_synth.cpp:1180` |  |
| `REBORN_RB_HP` | `120.0` | `src/devices/bass_synth.cpp:1181` |  |
| `REBORN_RB_B0S` | `0.7` | `src/devices/bass_synth.cpp:1182` |  |
| `REBORN_RB_CLIP` | `0.0` | `src/devices/bass_synth.cpp:1183` |  |
| `REBORN_RB_NAIVE` | `1.0` | `src/devices/bass_synth.cpp:1184` |  |
| `REBORN_RB_ACC` | `0.0` | `src/devices/bass_synth.cpp:1185` |  |
| `REBORN_RB_IN` | `0.06` | `src/devices/bass_synth.cpp:1189` |  |
| `REBORN_RB_OUT` | `22.0` | `src/devices/bass_synth.cpp:1190` |  |
| `REBORN_D_VT` | `0.30` | `src/devices/bass_synth.cpp:1194` |  |
| `REBORN_D_K` | `1.0` | `src/devices/bass_synth.cpp:1195` |  |
| `REBORN_D_P1` | `2.0` | `src/devices/bass_synth.cpp:1196` |  |
| `REBORN_D_OS` | `4.0` | `src/devices/bass_synth.cpp:1197` |  |
| `REBORN_D_FBHP` | `0.0` | `src/devices/bass_synth.cpp:1198` |  |
| `REBORN_D_G` | `1.0` | `src/devices/bass_synth.cpp:1199` |  |
| `REBORN_D_ITER` | `4.0` | `src/devices/bass_synth.cpp:1200` |  |

## The PCF

| Variable | Default | Read at | Notes |
|---|---|---|---|
| `REBORN_PCF_RESO` |  | `src/effects/pcf.cpp:459` | `0` or `old` selects the earlier continuous resonator; unset is the shipped banded resonator |
| `REBORN_PCF_ENVLAW` |  | `src/effects/pcf.cpp:465` | `0` or `old` selects the earlier additive envelope law; unset is the shipped depth-capped law |

## The engine

| Variable | Default | Read at | Notes |
|---|---|---|---|
| `REBORN_SLIDE_NEEDS_VOICE` |  | `src/engine/engine.cpp:933` | non-zero applies a measured-and-rejected slide rule (`include/reborn/step_events.h`, BassRules) |
| `REBORN_REST_ACCENT` |  | `src/engine/engine.cpp:934` | non-zero applies a measured-and-rejected accent-on-rest rule (BassRules) |
| `REBORN_TRACE_BASS` | `0` | `src/engine/engine.cpp:948` | `1`, `2` or `3` prints the 303 step decisions for bass 1, bass 2 or both to stderr |

## The tests

These let the test suite exercise a real mod you own without putting it in the repository. Keep any key files outside the repository too.

| Variable | Default | Read at | Notes |
|---|---|---|---|
| `REBORN_TEST_MOD` |  | `tests/test_main.cpp:3115` | path to a mod you own; the test suite then also checks it reads (skipped when unset) |
| `REBORN_TEST_MOD_KEYS` |  | `tests/test_main.cpp:3128` | path to exact keys for that mod, for the test's key comparison |
| `REBORN_TEST_MOD_WRONG` |  | `tests/test_main.cpp:3155` | path to wrong keys, which the test expects the reader to refuse |

## Build options

These are CMake options and compile definitions, not environment variables:
`REBORN_BUILD_APP` (off by default) builds the app and plugins;
`REBORN_BUILD_CLAP` (on) builds the CLAP plugins and CLAP hosting;
`REBORN_BUILD_OPEN303_PLUGIN` (on) builds the Reborn Open303 plugin; and
`REBORN_HOST_CLAP` is the compile definition the second one sets.
`docs/BUILDING.md` describes them.

## The Python tools

The tools in `tools/` read two variables. `REBORN_WORKSPACE` is the directory
that holds material kept outside the repository: generated fixtures
(`fixtures-<date>/`) and ReBirth's exports (`renders/`). It defaults to the
directory that contains the checkout. `REBORN_SONGS_DIR` is your own ReBirth
install's `Demo Songs` folder, which `tools/render/byte_neutral.py` and the
whole-song tools read. A tool that needs `REBORN_SONGS_DIR` and finds it unset
stops with a message; one that finds no fixtures or exports under the workspace
says where it looked. The tools also pass any `REBORN_*` probe through to the renders they
start, which is how a probe is measured.
