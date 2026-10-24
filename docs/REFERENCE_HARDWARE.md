# The three Roland machines

Background for choosing model topologies. **This is a prior, not a target.**
ReBirth is the target, and where ReBirth departs from the hardware we follow
ReBirth. See `docs/REBIRTH_CHARACTER.md` for the departures that matter.

## TB-303 Bass Line (1981 to 1984, roughly 10,000 built)

Sold as a bass accompaniment box for practising guitarists, and a commercial
failure. One VCO, switchable sawtooth or square, into a diode-ladder VCF, into a
VCA.

**The filter.** The long-running "18 or 24 dB per octave" argument is settled by
Tim Stinchcombe's 2009 circuit analysis: it is a four-pole diode ladder, but the
last pole is loaded in a way that makes the measured response neither a clean
24 dB per octave nor a clean 18. The resonance path has an unusual peak near
10 Hz. The practical summary is that it is a four-pole design whose audible
slope reads shallower than four poles, and whose resonance interacts with the
passband instead of sitting cleanly on top of it. A stock unit does not
self-oscillate; that takes a modification.

The diode ladder exists because EMS wanted a filter that did not infringe Moog's
transistor-ladder patent. Diodes control current into the filter capacitors
under a control voltage, the same job a transistor ladder does, with a
different large-signal nonlinearity. That nonlinearity is a large part of the
sound: the 303 is not a clean filter and the distortion is not incidental.

**Two envelopes.** The Main Envelope Generator drives the filter cutoff; the
Volume Envelope Generator drives the VCA. The DECAY knob is on the MEG. The VEG
is essentially fixed, with a short attack and a decay that leaks rather than
gating cleanly, so notes never stop instantly. That leak is why a 303 line has
its particular sense of connectedness.

**Accent.** An accented step raises the VCA level and also feeds the accent
voltage into the filter and resonance path, so accents are louder *and*
brighter. The accent circuit charges a capacitor that does not fully discharge
between steps, so a run of consecutive accents grows progressively more
emphatic. This is the "accent sweep" and it is the hardest part of the machine
to model, because it makes accent history-dependent rather than per-note.

**Slide.** Portamento of roughly 60 ms with no envelope retrigger, so a slid
note inherits the previous note's filter envelope state.

## TR-808 Rhythm Composer (1980 to 1983, roughly 12,000 built)

Fully analog. Every voice is its own circuit, and there is no shared voice
architecture.

**Bass drum, snare, toms and congas** are bridged-T resonant networks: a pulse
excites a network tuned to a low frequency and it rings down. The DECAY control
changes the network's feedback, so it rings longer, which also slightly changes
the effective pitch. The bass drum TONE control blends in a highpassed pulse for
the click. The snare uses two bridged-T oscillators, near 180 Hz and 330 Hz,
plus a noise generator through a bandpass; SNAPPY sets the noise level and TUNE
moves the oscillators.

**Hats, cymbal and cowbell** share a bank of six square-wave Schmitt-trigger
oscillators at fixed inharmonic frequencies. Four are hardwired, two are trimmed
by internal pots. The metallic voices mix all six and shape them with different
filters and envelopes; the cowbell uses only the two adjustable ones. This is
why the 808's metals all sound related, and why they cannot be tuned
independently.

**Clap** is a burst train of three short noise bursts roughly 10 ms apart,
followed by a longer bandpassed noise tail.

**Accent** is a global bus. An accented step raises the trigger level for every
voice on that step by the same amount, which is exactly what the ReBirth manual
describes on page 149.

**The closed hat chokes the open hat** through a shared discharge path.

## TR-909 Rhythm Composer (1983 to 1985, roughly 10,000 built)

Hybrid, and that hybrid is the whole character. Bass drum, snare, toms, rimshot
and clap are analog. Hi-hats, crash and ride are 6-bit PCM samples in ROM,
clocked out by a counter into a simple DAC, then a VCA for the envelope and a
lowpass to remove the sample clock. The low bit depth and low sample rate are
audible and are part of the sound rather than a defect to correct.

The bass drum is a bridged-T with a pitch envelope plus a separate click
generator, with ATTACK, DECAY and TUNE. The snare is tone oscillators plus
noise, with TUNE and SNAPPY. The closed hat chokes the open hat as on the 808.

## What this buys us

The 303 section is directly useful: it tells us the filter is four poles with a
compromised last pole, that resonance and passband interact, that the VCA leaks,
and that accent is history-dependent. All four are modelling decisions.

The 808 and 909 sections are mostly *not* directly useful, because ReBirth does
not synthesise its drums. It plays recordings. The circuit descriptions explain
why the recordings sound as they do and why certain knobs behave oddly, but the
model to match is a sampler, not a bridged-T network. That is the single largest
correction this research pass produced.
