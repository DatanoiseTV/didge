/*
  Didge — physically modeled didgeridoo
  Copyright (C) 2026 DatanoiseTV

  This program is free software: you can redistribute it and/or modify it under
  the terms of the GNU General Public License as published by the Free Software
  Foundation, either version 3 of the License, or (at your option) any later
  version, and distributed WITHOUT ANY WARRANTY. See <https://www.gnu.org/licenses/>.
  You must retain this notice and the attribution to DatanoiseTV in any
  redistributed or derivative version.
*/

#include "DidgeEngine.h"

namespace didge
{

namespace
{
    constexpr float kMaxLungPressure = 4000.0f;   // Pa at pressure = 1
    constexpr float kOutputScale     = 1.0f / 3000.0f;

    // Nominal embouchure: lip resonance as a fraction of the sounding pitch.
    // The bore is calibrated at this value, so the player's tension control
    // bends around it instead of redefining it.
    constexpr float kNominalLipRatio = 0.90f;

    // Turbulence level, as a fraction of the Bernoulli pressure drop across
    // the lips. Calibrated by measuring harmonic-to-noise ratio, not by ear:
    // the literature puts a sustained wind-instrument tone around +20 dB HNR,
    // with noise a few per cent of the blowing pressure.
#ifndef DIDGE_NOISE
#define DIDGE_NOISE 1.5f
#endif
    constexpr float kNoiseScale = DIDGE_NOISE;

    // Level trim per bore profile, in dB, measured on a held D2 and referred
    // to the natural bore. The profiles genuinely differ in how strongly the
    // lip valve drives them -- a narrow, half-cylindrical trombone bore asked
    // to sound a low drone is a much weaker regime than a wide conical one --
    // and an 18 dB jump when switching would make the control unusable. The
    // boost is capped, so the weakest profiles stay a little quieter rather
    // than having their noise floor lifted with them.
    constexpr float kProfileTrimDb[] = {
        0.0f,   // natural (reference)
       -0.4f,   // cylinder
        2.9f,   // cone
        4.8f,   // flared
        3.8f,   // horn
        1.2f,   // trumpet
        4.2f,   // trombone
        2.2f,   // flugelhorn
        8.0f,   // french horn
       10.0f,   // tuba        (wants 11.5; capped)
        1.0f,   // alphorn
       10.0f,   // contrabass  (wants 12.9; capped)
    };

    // How far the vocal tract may exceed the bore impedance. Measured values
    // (Tarnopolsky 2005; Smith et al. JASA 121, 547) put a didgeridoo tract
    // peak near 12 MPa.s/m^3 against a bore around 0.7, so the tract really
    // does dominate by more than an order of magnitude in the formant band.
#ifndef DIDGE_ZLIM
#define DIDGE_ZLIM 2.5f
#endif
    constexpr float kTractZLimit = DIDGE_ZLIM;

    inline float noteToHz (float note)
    {
        return 440.0f * std::pow (2.0f, (note - 69.0f) / 12.0f);
    }
}

// ===========================================================================
// PitchTrim
// ===========================================================================
void PitchTrim::prepare (double sampleRate)
{
    fs = static_cast<float> (sampleRate);
    resetLearning();
}

void PitchTrim::resetLearning()
{
    for (int i = 0; i < kBands; ++i)
    {
        const float t = (static_cast<float> (i) + 0.5f) / kBands;
        trim[i] = priorFor (kLoHz * std::pow (kHiHz / kLoHz, t));
    }
    reset();
}

void PitchTrim::reset()
{
    prev = mean = 0.0f;
    env = 0.0f;
    armed = false;
    firstCross = lastCross = -1.0;
    crossings = 0;
    sampleIdx = 0.0;
    windowLeft = static_cast<int> (kWindowSec * fs);
    measuredHz = 0.0f;
}

void PitchTrim::bandSplit (float hz, int& idx, float& frac)
{
    const float t = std::log (std::max (kLoHz, std::min (kHiHz, hz)) / kLoHz)
                  / std::log (kHiHz / kLoHz) * kBands - 0.5f;
    idx = std::max (0, std::min (kBands - 2, static_cast<int> (std::floor (t))));
    frac = std::max (0.0f, std::min (1.0f, t - static_cast<float> (idx)));
}

// Uniform in [-1, 1], its own generator so the audio noise stream is not
// perturbed by how many notes have been played.
float DidgeEngine::nextHumanRandom()
{
    humRng = humRng * 1664525u + 1013904223u;
    return (static_cast<float> ((humRng >> 8) & 0xffffff) / 8388608.0f) - 1.0f;
}

float PitchTrim::forFrequency (float hz) const
{
    int i; float f;
    bandSplit (hz, i, f);
    return trim[i] + (trim[i + 1] - trim[i]) * f;
}

void PitchTrim::observe (float lipOpening, float targetHz, bool stable)
{
    sampleIdx += 1.0;

    // Track the slow mean so crossings are detected around the operating
    // point rather than zero (the lips sit open under blowing pressure).
    mean += 0.0004f * (lipOpening - mean);

    const float a = prev - mean, b = lipOpening - mean;
    prev = lipOpening;

    // Peak envelope, used as a hysteresis threshold. Hard-driven lips often
    // show a faint period-doubling — alternate cycles differ slightly — and a
    // bare crossing count would read f/2. Requiring the signal to swing below
    // -h before arming the next crossing rejects the small alternate cycles.
    env += (std::abs (b) > env ? 0.02f : 0.0004f) * (std::abs (b) - env);
    const float h = 0.25f * env;

    if (b < -h) armed = true;
    if (armed && a <= h && b > h)
    {
        armed = false;
        // Linear interpolation of the crossing instant: sub-sample accurate,
        // which matters since a 0.25 s window only holds ~18 cycles at 73 Hz.
        const float frac = (b - a) != 0.0f ? ((h - a) / (b - a)) : 0.0f;
        const double t = sampleIdx - 1.0 + static_cast<double> (frac);
        if (firstCross < 0.0) firstCross = t;
        else { lastCross = t; ++crossings; }
    }

    if (--windowLeft > 0)
        return;

    windowLeft = static_cast<int> (kWindowSec * fs);

    if (crossings >= 4 && lastCross > firstCross)
    {
        measuredHz = static_cast<float> (static_cast<double> (crossings)
                                         * static_cast<double> (fs)
                                         / (lastCross - firstCross));

        // Gate tightly around the target. The untrimmed model starts at most
        // ~1.2x sharp, so this window still admits every real correction while
        // rejecting octave errors — a faint period-doubling in the lip motion
        // would otherwise read as f/2 and drive the trim to double the bore.
        const bool usable = stable && targetHz > 1.0f
                          && measuredHz > 0.78f * targetHz
                          && measuredHz < 1.30f * targetHz;

        // Let the note settle before taking the one measurement it gets. A
        // note is transiently sharp for the first few hundred milliseconds
        // while the bore and the lip glide into place; measuring there gives
        // an error of the wrong sign, and the single correction then walks the
        // tuning steadily further out with every note played.
        if (usable) ++stableWindows; else stableWindows = 0;

        if (armedToLearn && usable && stableWindows >= kSettleWindows)
        {
            // One full correction, not a fraction of one. The measurement is
            // good to about a cent, and there is only this one chance before
            // the trim is held until the next note.
            armedToLearn = false;
            int i; float f;
            bandSplit (targetHz, i, f);
            const float err = targetHz / measuredHz;
            // Spread the correction across the same two bands the reader
            // blends, in the same proportion.
            const float step = err;
            const float lo = std::pow (step, 1.0f - f);
            const float hi = std::pow (step, f);
            trim[i]     = std::max (0.75f, std::min (1.15f, trim[i] * lo));
            trim[i + 1] = std::max (0.75f, std::min (1.15f, trim[i + 1] * hi));
        }
    }
    firstCross = lastCross = -1.0;
    crossings = 0;
}

// ===========================================================================
// DidgeEngine
// ===========================================================================
void DidgeEngine::prepare (double sampleRate, int)
{
    fs = static_cast<float> (sampleRate);
    bore.prepare (sampleRate);
    bore.setSmoothing (sampleRate);
    tract.prepare (sampleRate);
    lips.prepare (sampleRate);
    ambience.prepare (sampleRate);
    spectrum.prepare (sampleRate);
    pitchTrim.prepare (sampleRate);
    for (auto& a : vTract) a.store (2.0f, std::memory_order_relaxed);
    for (auto& a : vBore)  a.store (0.02f, std::memory_order_relaxed);
    for (auto& a : vPress) a.store (0.0f, std::memory_order_relaxed);
    for (auto& a : vFlowSeg) a.store (0.0f, std::memory_order_relaxed);
    lipDecim = std::max (1, static_cast<int> (3.0f * fs / (73.42f * kLipTraceLen)));
    everTuned = false;
    reset();
}

void DidgeEngine::reset()
{
    bore.clear();
    tract.clear();
    lips.reset();
    ambience.clear();
    spectrum.reset();
    numHeld = 0;
    tootNote = -1;
    gate = false;
    pressureEnv = 0.0f;
    transientEnv = 0.0f;
    inDecay = false;
    vibPhase = growlPhase = 0.0f;
    bpZ1 = bpZ2 = breathLp = 0.0f;
    dcX = dcY = tiltPrev = 0.0f;
    lipDrop = lipOpenGate = 0.0f;
    humPressure = humTension = humAttack = 0.0f;
    driftA = driftB = 0.0f;
    lipTraceIdx = lipDecimCount = 0;
    lipTracePeak = 0.0f;
    for (auto& v : lipTrace) v.store (0.0f, std::memory_order_relaxed);
}

LipLoad DidgeEngine::buildLipLoad (const EngineParams& p) const
{
    LipLoad load;
    load.zeta = 0.05f + 0.45f * p.lipDamp;
    load.restOpening = -0.6e-3f + 1.8e-3f * p.embouchure;
    load.mouthPressure = std::max (150.0f, kMaxLungPressure * p.pressure);
    return load;
}

void DidgeEngine::handleEvent (const NoteEvent& e)
{
    switch (e.type)
    {
        case NoteEvent::noteOn:
        {
            int idx = -1;
            for (int i = 0; i < numHeld; ++i)
                if (heldNotes[i] == e.note) idx = i;
            if (idx < 0 && numHeld < kMaxHeld) idx = numHeld++;
            if (idx >= 0)
            {
                heldNotes[idx] = e.note;
                heldVel[idx]   = e.velocity;
            }
            if (! gate)
            {
                noteVelocity = e.velocity;
                transientEnv = 0.4f + 0.6f * e.velocity;   // tongued attack chiff
                inDecay = false;                           // retrigger the envelope
            }
            // Fresh inconsistencies for this note. No two notes from a player
            // start with quite the same breath, embouchure or tonguing.
            humPressure = nextHumanRandom();
            humTension  = nextHumanRandom();
            humAttack   = nextHumanRandom();
            gate = true;
            // Re-arm the learner and re-latch whatever it knows now.
            pitchTrim.noteChanged();
            needsRetune = true;
            break;
        }
        case NoteEvent::noteOff:
        {
            for (int i = 0; i < numHeld; ++i)
                if (heldNotes[i] == e.note)
                {
                    for (int j = i; j < numHeld - 1; ++j)
                    {
                        heldNotes[j] = heldNotes[j + 1];
                        heldVel[j]   = heldVel[j + 1];
                    }
                    --numHeld;
                    break;
                }
            gate = numHeld > 0;
            break;
        }
        case NoteEvent::allNotesOff:
            numHeld = 0;
            gate = false;
            break;
    }
    refreshRegister();
}

void DidgeEngine::refreshRegister()
{
    if (numHeld == 0)
    {
        tootNote = -1;      // keep droneNote: the release tail rings on it
        return;
    }
    int lo = heldNotes[0], hi = heldNotes[0];
    for (int i = 1; i < numHeld; ++i)
    {
        lo = std::min (lo, heldNotes[i]);
        hi = std::max (hi, heldNotes[i]);
    }
    const int prevDrone = droneNote;
    droneNote = lo;
    tootNote = (hi != lo && noteToHz (static_cast<float> (hi))
                          / noteToHz (static_cast<float> (lo)) >= 1.45f) ? hi : -1;
    if (droneNote != prevDrone)
    {
        pitchTrim.noteChanged();
        needsRetune = true;
    }
}

void DidgeEngine::retune (const EngineParams& p, bool force)
{
    const float target = noteToHz (static_cast<float> (droneNote) + p.tuneCents * 0.01f);
    const float trimNow = pitchTrim.forFrequency (target);

    const bool shapeChanged = ! everTuned || tunedShape.differsFrom (p.shape);
    const bool noteChanged  = ! everTuned || needsRetune
                            || std::abs (target - tunedTargetHz) > tunedTargetHz * 0.0008f;
    needsRetune = false;

    // The learned trim is latched here and NOT re-applied while a note holds.
    // It used to be, and that is heard as portamento: the servo walks out its
    // error over a second or two, so every note slid up to about fifty cents
    // into place instead of simply being in tune. Learning still runs
    // throughout -- it just lands on the next note-on rather than sliding the
    // one being played.
    if (! force && ! shapeChanged && ! noteChanged)
        return;

    if (shapeChanged)
    {
        bore.setShape (p.shape);
        // A different tube sounds differently sharp; start the learner over.
        pitchTrim.resetLearning();
    }

    bore.tuneForPlayed (target * trimNow, buildLipLoad (p), kNominalLipRatio);

    tunedTargetHz = target;
    tunedTrim = trimNow;
    tunedShape = p.shape;

    // Snap only when nothing is ringing, so legato note changes slur.
    if (! everTuned || pressureEnv < 0.02f * kMaxLungPressure)
        bore.snapToTargets();
    everTuned = true;

    for (int i = 0; i < Bore::kSegments; ++i)
        vBore[i].store (bore.segmentRadius (i), std::memory_order_relaxed);
    vF0.store (bore.droneFrequency(), std::memory_order_relaxed);
    vToot.store (bore.tootFrequency(), std::memory_order_relaxed);
}

void DidgeEngine::process (float* outL, float* outR, int numSamples,
                           const EngineParams& p,
                           const NoteEvent* events, int numEvents)
{
    int evIdx = 0;
    while (evIdx < numEvents && events[evIdx].offset <= 0)
        handleEvent (events[evIdx++]);
    retune (p, false);

    tract.setVowel (p.vowelX, p.vowelY);

    // Reaching the overblown register takes a tighter embouchure as well as a
    // higher lip resonance — with the loose setting that suits the drone, the
    // toot barely speaks (measured 0.0007 rms against 0.038 when tightened).
    // A player does this without thinking, so asking for the higher note is
    // enough here: pressing it also firms the lips.
    float embNow = tootNote >= 0 ? p.embouchure * 0.45f : p.embouchure;
    float dampNow = p.lipDamp;
    {
        const auto v = static_cast<VelTarget> (p.velTarget);
        const float vn = std::max (0.0f, std::min (1.0f, noteVelocity));
        const float a = std::max (0.0f, std::min (1.0f, p.velAmount));
        if (v == VelTarget::embouchure)
            embNow *= 1.0f - a * 0.6f * vn;          // harder note, firmer lips
        else if (v == VelTarget::brightness)
            dampNow *= 1.0f - a * 0.8f * vn;         // less damping, brighter
    }
    lips.setDamping (0.05f + 0.45f * dampNow);
    lips.setRestOpening (-0.6e-3f + 1.8e-3f * embNow);

    // Humanising. Two parts: an offset drawn once per note, so repeats are
    // never identical, and a slow wander that keeps moving under a held note,
    // because a player's breath and embouchure never truly hold still. Both
    // are kept well under what reads as an effect -- a couple of cents and a
    // couple of per cent.
    const float hum = std::max (0.0f, std::min (1.0f, p.humanize));
    driftA += (nextHumanRandom() - driftA) * (1.0f - std::exp (-1.0f / (0.55f * fs)));
    driftB += (nextHumanRandom() - driftB) * (1.0f - std::exp (-1.0f / (1.30f * fs)));
    const float humPitchSemis = hum * (0.055f * humTension + 0.035f * driftA);
    const float humPressMul   = 1.0f + hum * (0.045f * humPressure + 0.030f * driftB);

    const float bendMul = std::pow (2.0f, (bendSemis + p.tensionSemis + humPitchSemis) / 12.0f);

    // Register: the drone plays at the calibrated embouchure; a second, higher
    // held note tightens the lips onto the bore's next sustaining regime.
    const float droneTargetHz = noteToHz (static_cast<float> (droneNote) + p.tuneCents * 0.01f);
    float lipBase = droneTargetHz * kNominalLipRatio;
    if (tootNote >= 0)
    {
        // Overblowing does not give you an arbitrary note: the tube offers one
        // higher sustaining regime, and the player lips it a little either
        // way. So the second held note selects that register and then bends it
        // by at most a couple of semitones toward what was asked for, rather
        // than pretending any pitch is reachable.
        const float natural = bore.tootFrequency();
        const float wanted  = noteToHz (static_cast<float> (tootNote) + p.tuneCents * 0.01f);
        const float bendLimit = std::pow (2.0f, 2.0f / 12.0f);
        const float pull = std::max (1.0f / bendLimit,
                                     std::min (bendLimit, wanted / std::max (1.0f, natural)));
        const float aim = natural * pull;

        const float solved = bore.lipResonanceForPlayed (aim, buildLipLoad (p));
        lipBase = solved > 0.0f ? solved : aim * kNominalLipRatio;
        vToot.store (aim, std::memory_order_relaxed);
    }
    lipFreqTarget = lipBase * bendMul;

    // Bend the tube with the lips. Bending only the embouchure barely moves
    // the sounding pitch -- the bore dominates it -- so a bend range of an
    // octave would deliver a couple of semitones and the control would be a
    // lie. Shortening the tube by the same ratio makes the bend mean what it
    // says, and the length already glides, so it stays smooth.
    // The bend comes out about 4 per cent compressive, because the lip
    // resonance and the tube length do not scale quite identically through an
    // outward-striking valve. Measured and corrected here.
    bore.setLengthScale (std::pow (1.0f / bendMul, 1.042f));
    vTootActive.store (tootNote >= 0, std::memory_order_relaxed);

    // Velocity routing. A wind player blowing harder also tightens up and
    // tongues faster, so the destinations beyond plain breath are the ones
    // that follow from the same gesture.
    const auto vt = static_cast<VelTarget> (p.velTarget);
    const float velN = std::max (0.0f, std::min (1.0f, noteVelocity));
    const float amt = std::max (0.0f, std::min (1.0f, p.velAmount));
    const bool  velToBreath = vt == VelTarget::breath
                           || vt == VelTarget::breathAttack;

    const float velScale = velToBreath ? (1.0f - amt) + amt * (0.25f + 0.75f * velN) : 1.0f;

    // Overblowing takes more breath as well as a firmer lip. The higher
    // register sits where the bore's losses bite harder, so without the extra
    // push it stops speaking as soon as the walls are at all damped -- which
    // is exactly what a wooden tube is.
    const float regScale = tootNote >= 0 ? 1.45f : 1.0f;
    const float pTargetOn = kMaxLungPressure * p.pressure * velScale * regScale * humPressMul
                          * std::max (0.0f, std::min (1.5f, ccPressureScale));

    // Harder notes speak faster; softer ones ease in.
    float attackMs = std::max (1.0f, p.attackMs);
    if (vt == VelTarget::breathAttack)
        attackMs *= 1.0f - amt * 0.85f * velN;
    attackMs *= 1.0f + hum * 0.30f * humAttack;

    const float lipGlide = 1.0f - std::exp (-1.0f / (0.030f * fs));
    const float envAtk   = 1.0f - std::exp (-1.0f / (std::max (1.0f, attackMs)   * 0.001f * fs));
    const float envRel   = 1.0f - std::exp (-1.0f / (std::max (5.0f, p.releaseMs) * 0.001f * fs));
    const float envDec   = 1.0f - std::exp (-1.0f / (std::max (10.0f, p.decayMs)  * 0.001f * fs));
    const float trDecay  = std::exp (-1.0f / (0.060f * fs));
    const float vibInc   = p.vibRate / fs;

    const float growlF   = droneTargetHz * std::pow (2.0f, p.growlSemis / 12.0f);
    const float growlInc = growlF / fs;

    const int profIdx = std::max (0, std::min ((int) std::size (kProfileTrimDb) - 1,
                                               p.shape.profile));
    const float profileTrim = std::pow (10.0f, kProfileTrimDb[profIdx] / 20.0f);

    const bool analyse = spectrumOn.load (std::memory_order_relaxed);

    const float zBore = bore.mouthImpedance();
    const float dcR = 1.0f - 6.2831853f * 18.0f / fs;
    const float tiltGain = 0.5f * (fs / 48000.0f);

    // Learning is only meaningful on a steady, un-modulated drone. Humanising
    // moves the pitch by design, so it has to be quiet for the learner to
    // trust what it hears.
    const bool learnable = tootNote < 0
                        && hum < 0.02f
                        && std::abs (bendSemis + p.tensionSemis) < 0.05f
                        && p.growl < 0.02f && p.vibDepth < 0.02f;

    float pkL = 0.0f, pkR = 0.0f;
    float lipOpenAcc = 0.0f, flowAcc = 0.0f;

    for (int n = 0; n < numSamples; ++n)
    {
        while (evIdx < numEvents && events[evIdx].offset <= n)
            handleEvent (events[evIdx++]);

        // --- breath pressure -------------------------------------------------
        // Attack, then optionally decay toward a sustain level while the note
        // is still held. With sustain at zero that gives a short struck
        // excitation rather than a drone: the breath simply runs out.
        float target, k;
        if (! gate)
        {
            target = 0.0f;
            k = envRel;
        }
        else if (p.decayOn)
        {
            if (! inDecay)
            {
                target = pTargetOn;
                k = envAtk;
                if (pressureEnv >= 0.95f * pTargetOn) inDecay = true;
            }
            else
            {
                target = pTargetOn * std::max (0.0f, std::min (1.0f, p.sustain));
                k = envDec;
            }
        }
        else
        {
            target = pTargetOn;
            k = target > pressureEnv ? envAtk : envRel;
        }
        pressureEnv += (target - pressureEnv) * k;
        transientEnv *= trDecay;

        vibPhase += vibInc;
        if (vibPhase >= 1.0f) vibPhase -= 1.0f;
        const float vib = 1.0f + p.vibDepth * 0.30f * std::sin (6.2831853f * vibPhase);

        float pLung = pressureEnv * vib * (1.0f + 0.55f * transientEnv);

        // --- vocalization: glottal pulses modulate the lung flow -------------
        if (p.growl > 0.001f)
        {
            growlPhase += growlInc;
            if (growlPhase >= 1.0f) growlPhase -= 1.0f;
            const float s = std::sin (3.14159265f * growlPhase);
            const float pulse = s * s * s;
            pLung *= 1.0f - 0.80f * p.growl + p.growl * 1.9f * pulse;
        }

        // --- noise sources ---------------------------------------------------
        rng = rng * 1664525u + 1013904223u;
        const float white = (static_cast<float> ((rng >> 8) & 0xffffff) / 8388608.0f) - 1.0f;
        bpZ1 += 0.28f * (white - bpZ1);
        bpZ2 += 0.055f * (bpZ1 - bpZ2);
        const float turb = bpZ1 - bpZ2;
        breathLp += 0.02f * (white - breathLp);

        // --- lip tension glide ----------------------------------------------
        lipFreqSm += (lipFreqTarget - lipFreqSm) * lipGlide;
        lips.setResonance (lipFreqSm);

        // --- coupled tract | lips | bore ------------------------------------
        const float pMinus = bore.beginStep();
        const float fTract = tract.beginStep();
        const float zTract = tract.lipEndImpedance();

        // The lips sit between the tract and the bore and see both impedances
        // in series. Blending zTract toward zero with tractMix turns the mouth
        // from a resonant cavity into a plain pressure source, which is what
        // the "Voice" control does.
        //
        // The coupling is gated by how hard the player is actually blowing.
        // Without that gate the tract drives the lips, the lips' flow drives
        // the tract, and the pair keeps ringing after the breath stops — a
        // loop with no energy source behind it, which left the instrument
        // humming at -76 dBFS forever instead of falling silent.
        const float engage = std::min (1.0f, pressureEnv / (0.05f * kMaxLungPressure));
        const float voice = engage * p.tractMix;

        // The tract is treated as a passive resonator hanging off the mouth,
        // excited by the oscillating lip flow, while the steady lung pressure
        // is supplied directly. Feeding the lung DC through the tract as well
        // would count it twice, which collapsed the oscillation entirely.
        //
        // The steady breath is no longer a problem here: the glottis is an
        // allpass that looks open at DC, so the tract's input impedance
        // vanishes at low frequency and cannot choke the mean flow. An earlier
        // version subtracted a DC estimate to achieve the same thing, but that
        // also cancelled the tract's low-frequency influence on the sound.
        // Cap how far the tract can load the lips. A tight tongue constriction
        // makes the tract's characteristic impedance several times the bore's,
        // and letting that through unbounded simply throttles the flow until
        // the drone stops (measured: fundamental 30 dB down at some vowels).
        // A real player's tongue shapes the sound without ever being able to
        // switch the instrument off. The frequency-dependent behaviour that
        // makes formants still arrives through the tract's returning wave.
        const float zMouth = voice * std::min (zTract, kTractZLimit * zBore);
        // The tract's returning wave is likewise bounded against the breath.
        // A tight constriction can build a standing wave strong enough to
        // reverse the pressure across the lips, which stops the valve dead;
        // a real mouth cavity modulates the blowing pressure, it does not
        // overpower it.
        const float lungNow = pLung * (1.0f + p.breath * 0.25f * breathLp);
        const float tractLimit = 0.7f * std::abs (lungNow) + 50.0f;
        const float tractTerm = std::max (-tractLimit,
                                          std::min (tractLimit, voice * 2.0f * fTract));

        // Turbulence, as a fluctuating PRESSURE jump across the lips.
        //
        // Hirschberg and Verge (ISMA 1995) derive the jet noise source as a
        // dipole in the instrument's mouth, "a fluctuating pressure jump of
        // amplitude scaling with the square of the jet velocity". Since
        // (1/2)*rho*u_jet^2 is exactly the Bernoulli drop across the slit, the
        // source is proportional to |dp| — and it therefore falls to nothing
        // while the lips are shut, which is what makes the hiss ride on the
        // tone in step with it instead of sounding like a separate draught.
        //
        // The previous version scaled noise off the acoustic flow instead,
        // which no published model does; it left a constant wind layer running
        // through the closed phase.
        const float breathAmt = std::min (1.5f, p.breath + 0.9f * transientEnv);
        const float turbPressure = kNoiseScale * breathAmt * turb * lipDrop * lipOpenGate;

        const float drive = lungNow + tractTerm - 2.0f * pMinus + turbPressure;

        const auto lip = lips.step (drive, zMouth + zBore);

        // Remembered for the next sample's noise source (the drop is only
        // known once the valve has been solved).
        lipDrop = std::min (std::abs (lip.deltaP), 3.0f * kMaxLungPressure);
        lipOpenGate = lips.opening() > 0.0f ? 1.0f : 0.0f;

        // The flow the lips pass launches a forward wave into the bore and an
        // equal and opposite reaction back up the tract.
        const float u = lip.flow;
        const float radiated = bore.finishStep (pMinus + zBore * u);

        tract.finishStep (0.0f, fTract - zMouth * lip.flow);

        const float lipOpen = lips.opening();
        pitchTrim.observe (lipOpen, droneTargetHz,
                           learnable && pressureEnv > 0.5f * pTargetOn && transientEnv < 0.05f);

        // Capture the lip motion for display, decimated so the stored window
        // covers about three periods of whatever note is sounding.
        if (++lipDecimCount >= lipDecim)
        {
            lipDecimCount = 0;
            lipTrace[lipTraceIdx].store (lipOpen, std::memory_order_relaxed);
            lipTraceIdx = (lipTraceIdx + 1) % kLipTraceLen;
            lipTracePeak = std::max (lipTracePeak, lipOpen);
        }

        // --- output chain ----------------------------------------------------
        const float dcIn = radiated;
        float y = dcIn - dcX + dcR * dcY;
        dcX = dcIn; dcY = y;

        const float tilt = y + tiltGain * (y - tiltPrev);
        tiltPrev = y;

        float mono = std::tanh (tilt * kOutputScale * profileTrim);

        float ambL, ambR;
        ambience.process (mono * 0.7f, ambL, ambR);
        const float wet = p.spaceMix;
        const float l = (mono + wet * ambL) * p.outGainLin;
        const float r = (mono + wet * ambR) * p.outGainLin;

        if (analyse) spectrum.push (0.5f * (l + r));

        outL[n] = l;
        outR[n] = r;
        pkL = std::max (pkL, std::abs (l));
        pkR = std::max (pkR, std::abs (r));
        lipOpenAcc += lipOpen;
        flowAcc += std::abs (lip.flow);
    }

    ambience.setSize (p.spaceSize);
    ambience.setDecay (p.spaceSize);

    auto bump = [] (std::atomic<float>& a, float v)
    {
        float cur = a.load (std::memory_order_relaxed);
        while (v > cur && ! a.compare_exchange_weak (cur, v, std::memory_order_relaxed)) {}
    };
    bump (peakL, pkL);
    bump (peakR, pkR);
    // Three periods of the drone spread across the trace buffer.
    lipDecim = std::max (1, static_cast<int> (3.0f * fs / (droneTargetHz * kLipTraceLen)));

    for (int i = 0; i < Bore::kSegments; ++i)
    {
        vPress[i].store (bore.segmentPressure (i), std::memory_order_relaxed);
        vFlowSeg[i].store (bore.segmentFlow (i), std::memory_order_relaxed);
    }
    vMeanFlow.store (bore.meanFlow(), std::memory_order_relaxed);
    if (analyse)
    {
        for (int i = 0; i < Spectrum::kBins; ++i)
        {
            vSpec[i].store (spectrum.binDb (i), std::memory_order_relaxed);
            vSpecPk[i].store (spectrum.binPeakDb (i), std::memory_order_relaxed);
        }
        for (int i = 0; i < Spectrum::kPeaks; ++i)
        {
            vPeakHz[i].store (spectrum.peakFreq (i), std::memory_order_relaxed);
            vPeakDb[i].store (spectrum.peakLevel (i), std::memory_order_relaxed);
        }
    }
    vTurb.store (std::min (1.5f, p.breath + 0.9f * transientEnv)
                 * (pressureEnv / kMaxLungPressure), std::memory_order_relaxed);

    const float inv = 1.0f / static_cast<float> (std::max (1, numSamples));
    vPressure.store (pressureEnv / kMaxLungPressure, std::memory_order_relaxed);
    vLipOpen.store (lipOpenAcc * inv, std::memory_order_relaxed);
    vFlow.store (flowAcc * inv, std::memory_order_relaxed);
    for (int i = 0; i < VocalTract::kSections; ++i)
        vTract[i].store (tract.sectionAreaCm (i), std::memory_order_relaxed);
}

} // namespace didge
