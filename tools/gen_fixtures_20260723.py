#!/usr/bin/env python3
"""
gen_fixtures_20260723.py: generate the 2026-07-23 controlled-test fixture set
(303 discriminators + 808/909 per-voice isolation songs) via rbsgen.

Run from the repo root:
    python tools/gen_fixtures_20260723.py [outdir]

Layout: outdir/batchA (highest value first), batchB, batchC. Every file is a
scratch-mode .rbs (clean-room, opens in ReBirth): one device audible, static
knobs, master compressor OFF baked in. Export protocol per batch README.
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "rbsgen"))
import rbsgen
S = rbsgen.step

outdir = sys.argv[1] if len(sys.argv) > 1 else "fixtures-2026-07-23"

def w303(batch, name, knobs, pat, tempo=120.0, bars=4):
    path = os.path.join(outdir, batch, name + ".rbs")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    rbsgen.write_song(path, knobs, pat, tempo=tempo, n_bars=bars, title=name)
    print("wrote", path)

def wdrum(batch, name, device, dk, hits, accents=(), tempo=120.0, bars=4):
    path = os.path.join(outdir, batch, name + ".rbs")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    data = rbsgen.build_song({}, [], tempo=tempo, n_bars=bars, title=name,
                             device=device, drum_knobs=dk, drum_hits=hits,
                             drum_accents=accents)
    open(path, "wb").write(data)
    print("wrote", path)

held_a  = [S(9, slide=(i > 0)) for i in range(16)]        # held A (110 Hz)
rep_a   = [S(9) for _ in range(16)]                       # repeated A
FOUR    = dict(bd=[0, 4, 8, 12])                          # four-on-floor
TWO     = [0, 8]                                          # long-tail spacing

# =========================== BATCH A ======================================
# 303 discriminators
for t in (0, 127):
    w303("batchA", "t303_tune%03d" % t,
         dict(tune=t, cutoff=90, reso=20, envmod=0, decay=64), held_a)
w303("batchA", "t303_rawosc_saw",
     dict(tune=64, cutoff=127, reso=0, envmod=0, decay=64, waveform=0), held_a)
w303("batchA", "t303_rawosc_sq",
     dict(tune=64, cutoff=127, reso=0, envmod=0, decay=64, waveform=1), held_a)
for d in (32, 127):
    w303("batchA", "t303_decay%03d" % d,
         dict(tune=64, cutoff=90, reso=60, envmod=70, decay=d), rep_a)
# 808 core: BD + SD
wdrum("batchA", "d808_bd_ref",      "808", dict(), FOUR)
wdrum("batchA", "d808_bd_tone000",  "808", dict(bd_tone=0),   FOUR)
wdrum("batchA", "d808_bd_tone127",  "808", dict(bd_tone=127), FOUR)
wdrum("batchA", "d808_bd_decay000", "808", dict(bd_decay=0),   FOUR)
wdrum("batchA", "d808_bd_decay127", "808", dict(bd_decay=127), FOUR)
wdrum("batchA", "d808_sd_ref",       "808", dict(), dict(sd=[0, 4, 8, 12]))
wdrum("batchA", "d808_sd_snappy000", "808", dict(sd_snappy=0),   dict(sd=[0, 4, 8, 12]))
wdrum("batchA", "d808_sd_snappy127", "808", dict(sd_snappy=127), dict(sd=[0, 4, 8, 12]))

# =========================== BATCH B ======================================
wdrum("batchB", "d808_sd_tone000", "808", dict(sd_tone=0),   dict(sd=[0, 4, 8, 12]))
wdrum("batchB", "d808_sd_tone127", "808", dict(sd_tone=127), dict(sd=[0, 4, 8, 12]))
for t in (0, 64, 127):
    wdrum("batchB", "d808_lt_tune%03d" % t, "808", dict(lt_tune=t), dict(lt=TWO))
wdrum("batchB", "d808_mt_ref", "808", dict(), dict(mt=TWO))
wdrum("batchB", "d808_ht_ref", "808", dict(), dict(ht=TWO))
wdrum("batchB", "d808_cy_ref",      "808", dict(), dict(cy=TWO))
wdrum("batchB", "d808_cy_tune000",  "808", dict(cy_tune=0),   dict(cy=TWO))
wdrum("batchB", "d808_cy_tune127",  "808", dict(cy_tune=127), dict(cy=TWO))
wdrum("batchB", "d808_cy_decay127", "808", dict(cy_decay=127), dict(cy=TWO))
wdrum("batchB", "d808_oh_decay000", "808", dict(oh_decay=0),   dict(oh=[0, 4, 8, 12]))
wdrum("batchB", "d808_oh_decay127", "808", dict(oh_decay=127), dict(oh=TWO))
wdrum("batchB", "d808_ch_ref", "808", dict(), dict(ch=[0, 2, 4, 6, 8, 10, 12, 14]))
wdrum("batchB", "d808_choke",  "808", dict(oh_decay=127),
      dict(oh=[0, 8], ch=[4, 12]))                        # OH ring cut by CH
wdrum("batchB", "d808_cb_ref", "808", dict(), dict(cb=[0, 4, 8, 12]))
wdrum("batchB", "d808_rs_ref", "808", dict(), dict(rs=[0, 4, 8, 12]))
wdrum("batchB", "d808_cp_ref", "808", dict(), dict(cp=[0, 4, 8, 12]))
wdrum("batchB", "d808_accent", "808", dict(accent=127), FOUR, accents=(0, 8))

# =========================== BATCH C (909) ================================
wdrum("batchC", "d909_bd_ref",      "909", dict(), FOUR)
wdrum("batchC", "d909_bd_tune000",  "909", dict(bd_tune=0),   FOUR)
wdrum("batchC", "d909_bd_tune127",  "909", dict(bd_tune=127), FOUR)
wdrum("batchC", "d909_bd_decay127", "909", dict(bd_decay=127), FOUR)
wdrum("batchC", "d909_bd_attack127", "909", dict(bd_attack=127), FOUR)
wdrum("batchC", "d909_sd_ref",       "909", dict(), dict(sd=[0, 4, 8, 12]))
wdrum("batchC", "d909_sd_tune127",   "909", dict(sd_tune=127), dict(sd=[0, 4, 8, 12]))
wdrum("batchC", "d909_sd_snappy000", "909", dict(sd_snappy=0), dict(sd=[0, 4, 8, 12]))
wdrum("batchC", "d909_sd_snappy127", "909", dict(sd_snappy=127), dict(sd=[0, 4, 8, 12]))
wdrum("batchC", "d909_lt_tune000",  "909", dict(lt_tune=0),   dict(lt=TWO))
wdrum("batchC", "d909_lt_tune127",  "909", dict(lt_tune=127), dict(lt=TWO))
wdrum("batchC", "d909_lt_decay127", "909", dict(lt_decay=127), dict(lt=TWO))
wdrum("batchC", "d909_ch_decay127", "909", dict(ch_decay=127), dict(ch=[0, 2, 4, 6, 8, 10, 12, 14]))
wdrum("batchC", "d909_oh_decay127", "909", dict(oh_decay=127), dict(oh=TWO))
wdrum("batchC", "d909_cr_ref",     "909", dict(), dict(cr=TWO))
wdrum("batchC", "d909_cr_tune127", "909", dict(cr_tune=127), dict(cr=TWO))
wdrum("batchC", "d909_rd_ref", "909", dict(), dict(rd=TWO))
wdrum("batchC", "d909_accent", "909", dict(accent=127), FOUR, accents=(0, 8))

print("done.")
