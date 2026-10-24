import os, sys, glob, wave
import numpy as np
WS=os.path.abspath("..")
RB=os.path.join(WS,"renders","accentrate_rb"); OU=os.path.join(WS,"renders","accentrate_ours")
SR=44100.0; STEP=0.125          # 120 BPM sixteenths
def rd(p):
    with wave.open(p,"rb") as w:
        n,ch,sr=w.getnframes(),w.getnchannels(),w.getframerate()
        x=np.frombuffer(w.readframes(n),"<i2").astype(np.float64)/32768.0
    return x.reshape(-1,ch).mean(axis=1), sr
def bright(x, sr, t0, t1):
    """dB ratio of 1-5 kHz to 100-400 Hz over a window. Level-independent, so
    it reads the FILTER and not the amplitude envelope."""
    a,b=int(t0*sr),int(t1*sr)
    seg=x[a:b]
    if len(seg)<256: return None
    n=1<<int(np.ceil(np.log2(len(seg))))
    P=np.abs(np.fft.rfft(seg*np.hanning(len(seg)),n))**2
    f=np.fft.rfftfreq(n,1.0/sr)
    hi=(f>=1000)&(f<=5000); lo=(f>=100)&(f<=400)
    return 10*np.log10((P[hi].sum()+1e-20)/(P[lo].sum()+1e-20))
def probe(path, step_idx):
    """Brightness over the 60 ms following the given 0-based step, averaged
    over the 4 bars so one render gives four looks at the same event."""
    x,sr=rd(path); vals=[]
    for bar in range(4):
        t0=bar*2.0+step_idx*STEP
        v=bright(x,sr,t0,t0+0.060)
        if v is not None: vals.append(v)
    return float(np.mean(vals)) if vals else None

CASES=[
 ("CASE A: slide out of SILENCE into an accented note", 3,
  [("r303_A_slide_from_silence","accented"),
   ("r303_A_control_noaccent","no accent"),
   ("r303_A_slide_from_silence_dup","accented (dup)")]),
 ("CASE B: accent on a PLAIN rest over a ringing note", 1,
  [("r303_B_accent_on_plain_rest","accented rest"),
   ("r303_B_control_plain_rest","plain rest"),
   ("r303_B_accent_on_plain_rest_dup","accented rest (dup)")]),
 ("CONTROLS that ACC_TIE already reproduces", 3,
  [("r303_A_control_sounding","accent on slid step, voice sounding")]),
 ("CONTROL: accent on a rest that IS tied into", 1,
  [("r303_B_control_tied_rest","tied rest, accented")]),
 ("ENDPOINTS: a real retrigger", 1,
  [("r303_ref_retrigger_accent","retrigger + accent"),
   ("r303_ref_retrigger_plain","retrigger, no accent"),
   ("r303_ref_retrigger_accent_dup","retrigger + accent (dup)")]),
]
print(f"\nBrightness (1-5 kHz over 100-400 Hz, dB) in the 60 ms after the event.")
print(f"Mean of 4 bars. Higher = brighter = the accent opened the filter more.\n")
for title, idx, files in CASES:
    print(f"{title}   [step {idx+1}]")
    print(f"  {'fixture':<40}{'ReBirth':>10}{'ours':>10}{'ours-RB':>10}")
    for fn, lbl in files:
        r=probe(os.path.join(RB,fn+".wav"), idx); o=probe(os.path.join(OU,fn+".wav"), idx)
        if r is None or o is None: print(f"  {lbl:<40}   MISSING"); continue
        print(f"  {lbl:<40}{r:>10.2f}{o:>10.2f}{o-r:>+10.2f}")
    print()
