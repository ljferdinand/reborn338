// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand

#pragma once
//
// The 303 sequencer-to-voice boundary, as an explicit value.
//
// WHY THIS IS ITS OWN THING. Deciding what one 303 step does to the voice is
// four lines of branching, and it has been wrong three times. Each one shipped,
// each one was caught by Luke's ear rather than by any metric, and each one was
// an interaction between two flags rather than a single control being wrong:
//
//   a rest that is TIED INTO was treated as a note-off   a 32 dB hole
//   the accent row on a rest was not read                the accent was lost
//   the note byte on a rest was discarded                THE WRONG NOTE
//
// That is exactly the class a one-control-at-a-time fixture sweep cannot see,
// BY CONSTRUCTION. Separating the DECISION from the CALLS makes the decision
// testable without rendering any audio, so those three become unit tests that
// fail loudly instead of ear checks that have to be noticed. The regression
// locks live in tests/test_main.cpp under test_bass_step_*.
//
// It is also the boundary a hosted instrument would sit behind: a BassEvent is
// the thing you would translate into MIDI. That translation is lossy in at
// least three places and docs/PLUGINS.md records which, because a hosted synth
// sequenced this way will NOT reproduce 303 slide behaviour and that must never
// be filed as a parity bug.
//
#include <algorithm>
#include <cstddef>
#include "reborn/pattern.h"
#include "reborn/processor.h"

namespace reborn {

// What one 303 step does to the voice.
//
// Decided from that step's own flags and the PREVIOUS step's slide flag alone,
// which is the whole of the state the sequencer carries between steps.
struct BassEvent {
    enum class Kind {
        Strike,    // gate a new note, possibly gliding in from the one before
        Hold,      // carry a sounding note through a step that has no note of
                   // its own, because the previous step tied into it
        Release,   // gate off
    };

    Kind kind = Kind::Release;

    // Absolute semitone this step NAMES. Always meaningful, including on a
    // Release: ReBirth's oscillator pitch follows every step's note byte
    // whether or not that step gates, and the rest flag controls the gate and
    // nothing else. Measured 2026-08-01 on fixtures-2026-08-01/restpitch.
    int note = 0;

    // Read on Hold as well as on Strike. ReBirth fires the accent on a rest and
    // it lands on whatever is sounding through it. Left false on a Release,
    // which is faithful to what the voice is told: a plain rest that is not
    // tied into has nothing sounding for an accent to land on. Whether ReBirth
    // does something there has never been measured, so nothing is invented.
    bool accent = false;

    // Glide into this note rather than re-plucking it. Strike only, and it
    // comes from the PREVIOUS step's slide flag: the 303 slide flag sits on the
    // note you slide FROM.
    bool slideIn = false;

    // This step ties onward, whatever kind it is. NOT gated on there being a
    // note here: a rest can tie onward, and forcing this false on a rest was
    // the second half of the tied-rest bug, because it broke the chain at the
    // first rest and the note after re-plucked instead of gliding.
    bool tie = false;

    // Does this step leave a note running for the NEXT one to tie to? Strike and
    // Hold do, Release does not. Threaded between steps so a slide can tell
    // whether it has anything to glide FROM. See BassRules::slideNeedsVoice.
    bool sounding = false;

    // Fire the accent envelope without gating anything. Only ever set on a
    // Release, and only under BassRules::accentOnPlainRest.
    bool accentOnly = false;
};

// Behaviours that are being changed, held as data rather than as #ifdefs so the
// engine can read them from REBORN_* once and the decision stays pure and
// testable. Both default to the SHIPPED behaviour, so an unconfigured build is
// unchanged.
struct BassRules {
    // A slide only suppresses the retrigger if the previous step actually left
    // a note running. MEASURED 2026-08-02 on fixtures-2026-08-02/accentrate:
    // ReBirth's accent lift on a note slid into from a RELEASED rest is
    // +5.89 dB, and on a genuine retrigger it is +5.95 dB. They agree to 0.06
    // dB, so ReBirth treats a slide out of silence as exactly a fresh pluck.
    // We suppress it and measure +0.17 dB. Clodhopper's step 10 is this shape.
    bool slideNeedsVoice = false;

    // An accent on a PLAIN rest, with a note still ringing, fires the accent
    // envelope. Measured on the same batch: ReBirth lifts +10.61 dB where our
    // Release path drops the flag and lifts +0.00. Clodhopper's step 12.
    //
    // DELIBERATELY DOES NOT RE-ARM THE GATE. The statistic that established
    // this is a band RATIO, which is level-independent by construction and
    // therefore cannot see whether ReBirth also re-gates. Firing the filter
    // envelope is what was measured; anything about the VCA here would be
    // invention.
    bool accentOnPlainRest = false;
};

// Decide one step. Pure: no engine state, no audio, no side effects.
inline BassEvent decideBassStep(const BassStep& st, int absoluteNote,
                                bool prevSlide, bool prevSounding = true,
                                const BassRules& rules = BassRules{}) {
    BassEvent ev;
    ev.note = absoluteNote;
    ev.tie  = st.slide;
    // A slide with nothing to glide from is not a slide. Off by default.
    const bool glide = prevSlide && (prevSounding || !rules.slideNeedsVoice);
    if (st.gate) {
        ev.kind     = BassEvent::Kind::Strike;
        ev.accent   = st.accent;
        ev.slideIn  = glide;
        ev.sounding = true;
    } else if (prevSlide) {
        // A REST THAT IS TIED INTO IS NOT A NOTE-OFF. Measured 2026-07-31 on
        // Clodhopper's 303a, whose steps 2, 5 and 7 are rests and whose step 2
        // is a rest that is both tied into AND tying onward. Level through the
        // step, mean of five bars, dB below the file peak:
        //
        //   step 5, plain rest   ReBirth  -7.8  -9.5 -26.3   at 0 / 50 / 90%
        //                        ours    -10.4 -27.2 -38.5
        //   step 2, tied rest    ReBirth  -8.3  -1.9  -5.6   it SUSTAINS
        //                        ours    -10.5 -24.0 -38.3
        //
        // ReBirth holds a tied-into rest to the same half-step gate a note
        // gets and then releases; a tied rest that also slides sustains
        // straight through.
        ev.kind     = BassEvent::Kind::Hold;
        ev.accent   = st.accent;
        ev.sounding = true;
    } else {
        ev.kind = BassEvent::Kind::Release;
        // An accent on a plain rest, over a note that is still ringing.
        if (rules.accentOnPlainRest && st.accent && prevSounding) {
            ev.accent     = true;
            ev.accentOnly = true;
        }
    }
    return ev;
}

// Decide `count` consecutive steps, threading the slide state the way the
// sequencer does, and return the slide state left over. Wraps on pattern
// length. `out` must have room for `count` events.
//
// This is the form the tests use, because every defect in this code was a
// SEQUENCE property: what step 4 does depends on step 3, and no single-step
// assertion can express that.
inline bool decideBassSteps(const BassPattern& p, int startStep, int count,
                            bool prevSlide, BassEvent* out,
                            bool prevSounding = true,
                            const BassRules& rules = BassRules{}) {
    const int len = std::max(1, p.length);
    for (int i = 0; i < count; ++i) {
        const int idx = (startStep + i) % len;
        out[i] = decideBassStep(p.steps[(std::size_t) idx], p.absoluteNote(idx),
                                prevSlide, prevSounding, rules);
        prevSlide    = out[i].tie;
        prevSounding = out[i].sounding;
    }
    return prevSlide;
}

// Apply a decided event to a voice. The call ORDER is load-bearing and is the
// reason this is one function rather than three call sites:
// setStepNote() before the gate decision so the glide target is right whether
// the step strikes, ties or rests, and setStepTiming() before trigger()
// because trigger() is what arms the gate countdown.
inline void applyBassEvent(Instrument* inst, const BassEvent& ev,
                           double stepSeconds) {
    inst->setStepNote(ev.note);
    switch (ev.kind) {
        case BassEvent::Kind::Strike:
            inst->setStepTiming(stepSeconds, ev.tie);
            inst->trigger(ev.note, 1.0f, ev.accent, ev.slideIn);
            break;
        case BassEvent::Kind::Hold:
            inst->holdStep(stepSeconds, ev.tie, ev.accent);
            break;
        case BassEvent::Kind::Release:
            inst->release();
            // AFTER release(), so the gate closes and only the filter envelope
            // moves. See BassRules::accentOnPlainRest for why the gate is left
            // alone: the statistic that established this cannot see it.
            if (ev.accentOnly) inst->accentOnly();
            break;
    }
}

} // namespace reborn
