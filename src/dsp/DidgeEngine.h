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

#pragma once

#include "DidgeModel.h"
#include "Ambience.h"

#include <atomic>

namespace didge
{

// Control-rate parameters, in real units, resolved once per block by the
// plugin processor (or the tests) from whatever front end drives the engine.
struct EngineParams
{
    // Breath
    float pressure   = 0.62f;   // 0..1 -> lung pressure
    float attackMs   = 40.0f;
    float releaseMs  = 120.0f;
    float vibRate    = 4.5f;    // Hz
    float vibDepth   = 0.0f;    // 0..1
    float breath     = 0.25f;   // turbulence noise 0..1

    // Embouchure
    float tensionSemis = 0.0f;  // lip-tension trim, semitones
    float lipDamp      = 0.125f;// 0..1 -> damping ratio
    float embouchure   = 0.5f;  // 0..1 rest opening

    // Vocal tract / voice
    float tractMix   = 0.5f;    // 0..1 tract coupling
    float vowelX     = 0.35f;   // u..o..a..e..i
    float vowelY     = 0.5f;    // closed..open
    float growl      = 0.0f;    // 0..1 vocalization amount
    float growlSemis = 19.0f;   // voice pitch, semitones above the drone

    // Instrument
    float tuneCents  = 0.0f;
    BoreShape shape;

    // Output
    float spaceMix   = 0.18f;   // 0..1
    float spaceSize  = 0.4f;    // 0..1
    float outGainLin = 1.0f;
};

// Sample-accurate note events (already flattened from MIDI by the caller).
struct NoteEvent
{
    enum Type { noteOn, noteOff, allNotesOff };
    int   offset   = 0;
    Type  type     = noteOn;
    int   note     = 38;
    float velocity = 0.8f;
};

// ---------------------------------------------------------------------------
// Self-calibrating pitch trim.
//
// The linearised solver in Bore places the bore so the *threshold* oscillation
// lands on the target note, but a didgeridoo is driven far past threshold: the
// lips beat through several millimetres and spend part of each cycle shut, so
// the sounding pitch ends up sharp of the small-signal prediction. Measured on
// this model that offset runs from ~10 to ~180 cents depending almost entirely
// on bore shape and blowing pressure, so no fixed correction curve covers it.
//
// Instead the engine measures its own sounding period from the lip oscillator
// and integrates a length trim until the note is in tune, caching the result
// per frequency band. The first note in a band slides in over a few hundred
// milliseconds; every later note in that band starts in tune.
// ---------------------------------------------------------------------------
class PitchTrim
{
public:
    static constexpr int   kBands     = 12;
    static constexpr float kLoHz      = 30.0f;
    static constexpr float kHiHz      = 320.0f;
    static constexpr float kPrior     = 1.0f / 1.05f;  // model runs ~85 cents sharp
    static constexpr float kWindowSec = 0.20f;

    void prepare (double sampleRate);
    void resetLearning();

    // Trim to tune with, for a target frequency.
    float forFrequency (float hz) const;

    // Feed one sample of the lip oscillator; call every sample while sounding.
    // `stable` gates learning (no attack transient, no bend, no growl).
    void observe (float lipOpening, float targetHz, bool stable);

    void noteChanged() { reset(); }
    float lastMeasuredHz() const { return measuredHz; }

private:
    void reset();
    static int bandFor (float hz);

    float fs = 48000.0f;
    float trim[kBands];

    // Crossing tracker
    float prev = 0.0f, mean = 0.0f, env = 0.0f;
    bool  armed = false;
    double firstCross = -1.0, lastCross = -1.0;
    int    crossings = 0;
    double sampleIdx = 0.0;
    int    windowLeft = 0;
    float  measuredHz = 0.0f;
};

// The instrument: lungs -> vocal tract -> lip valve -> bore -> bell,
// with turbulence, vocalization, vibrato and a stereo ambience.
//
// Playing model: the lowest held note owns the bore (retuned, gliding). A
// second, higher held note (ratio >= ~1.45) tightens the embouchure onto the
// bore's next sustaining regime — the didgeridoo "toot"/trumpet register —
// without changing the bore, exactly like overblowing the real instrument.
// Pitch bend bends the lips, not the tube.
class DidgeEngine
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    void process (float* outL, float* outR, int numSamples,
                  const EngineParams& params,
                  const NoteEvent* events, int numEvents);

    // Block-rate performance inputs.
    void setPitchBend (float semitones) { bendSemis = semitones; }
    void setPressureScale (float s)     { ccPressureScale = s; }   // breath/expression CC

    // Model facts, for tests and the UI.
    float droneFrequency() const { return bore.droneFrequency(); }
    float tootFrequency()  const { return bore.tootFrequency(); }
    float measuredFrequency() const { return pitchTrim.lastMeasuredHz(); }
    bool  anyNoteHeld()    const { return numHeld > 0; }

    // Offline/test hook: relearn the pitch trim from scratch.
    void resetPitchLearning() { pitchTrim.resetLearning(); }

    // UI metering / visualization (written relaxed from the audio thread).
    float consumeOutPeak (int ch)
    {
        auto& a = ch == 0 ? peakL : peakR;
        return a.exchange (0.0f, std::memory_order_relaxed);
    }
    float vizPressure()  const { return vPressure.load (std::memory_order_relaxed); }
    float vizLipOpen()   const { return vLipOpen.load (std::memory_order_relaxed); }
    float vizFlow()      const { return vFlow.load (std::memory_order_relaxed); }
    float vizF0()        const { return vF0.load (std::memory_order_relaxed); }
    float vizToot()      const { return vToot.load (std::memory_order_relaxed); }
    bool  vizTootActive() const { return vTootActive.load (std::memory_order_relaxed); }
    float vizTractArea (int i) const { return vTract[i].load (std::memory_order_relaxed); }
    float vizBoreRadius (int i) const { return vBore[i].load (std::memory_order_relaxed); }

    static constexpr int kMaxHeld = 12;

private:
    void handleEvent (const NoteEvent& e);
    void refreshRegister();
    void retune (const EngineParams& p, bool force);
    LipLoad buildLipLoad (const EngineParams& p) const;

    float fs = 48000.0f;
    Bore bore;
    VocalTract tract;
    LipValve lips;
    Ambience ambience;
    PitchTrim pitchTrim;

    // Note state
    int   heldNotes[kMaxHeld] {};
    float heldVel[kMaxHeld] {};
    int   numHeld = 0;
    int   droneNote = 38;
    int   tootNote  = -1;
    bool  gate = false;
    float noteVelocity = 0.8f;

    // Envelopes / modulation state
    float pressureEnv = 0.0f;      // Pa
    float transientEnv = 0.0f;
    float vibPhase = 0.0f;
    float growlPhase = 0.0f;
    float lipFreqSm = 66.0f;       // Hz, glided lip resonance
    float lipFreqTarget = 66.0f;
    float bendSemis = 0.0f;
    float ccPressureScale = 1.0f;

    // Retune bookkeeping
    float tunedTargetHz = 0.0f;
    float tunedTrim = 0.0f;
    BoreShape tunedShape;
    bool  everTuned = false;

    // Noise state
    std::uint32_t rng = 0x1234567u;
    float bpZ1 = 0.0f, bpZ2 = 0.0f;
    float breathLp = 0.0f;

    // Previous sample's lip state, for the turbulence pressure source.
    float lipDrop = 0.0f, lipOpenGate = 0.0f;


    // Output chain state
    float dcX = 0.0f, dcY = 0.0f;
    float tiltPrev = 0.0f;

    // Metering / viz
    std::atomic<float> peakL { 0.0f }, peakR { 0.0f };
    std::atomic<float> vPressure { 0.0f }, vLipOpen { 0.0f }, vFlow { 0.0f };
    std::atomic<float> vF0 { 73.4f }, vToot { 160.0f };
    std::atomic<bool>  vTootActive { false };
    std::atomic<float> vTract[VocalTract::kSections];
    std::atomic<float> vBore[Bore::kSegments];
};

} // namespace didge
