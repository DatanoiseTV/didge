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

#include <juce_audio_processors/juce_audio_processors.h>

// Single source of truth for parameter IDs. The WebEditor re-quotes these on
// the JS side (PARAM ids in the relays); keep both in sync — a typo becomes
// a dead control, not a compile error, on the JS side.
namespace didge::ids
{
    // Breath
    inline constexpr const char* pressure   = "pressure";    // 0..1
    inline constexpr const char* attack     = "attack";      // ms
    inline constexpr const char* release    = "release";     // ms
    inline constexpr const char* vibRate    = "vibRate";     // Hz
    inline constexpr const char* vibDepth   = "vibDepth";    // 0..1
    inline constexpr const char* breathNoise= "breathNoise"; // 0..1
    inline constexpr const char* decayOn    = "decayOn";     // bool
    inline constexpr const char* decay      = "decay";       // ms
    inline constexpr const char* sustain    = "sustain";     // 0..1

    // Performance
    inline constexpr const char* velTarget  = "velTarget";   // choice
    inline constexpr const char* velAmount  = "velAmount";   // 0..1
    inline constexpr const char* humanize   = "humanize";    // 0..1

    // What MIDI velocity is allowed to control. Blowing harder is the natural
    // reading, but a player also tightens up and attacks faster. The order
    // must match didge::VelTarget in dsp/DidgeEngine.h.
    inline const juce::StringArray velTargetNames {
        "Off", "Breath", "Breath + Attack", "Embouchure", "Brightness"
    };

    // Embouchure
    inline constexpr const char* tension    = "tension";     // semitones
    inline constexpr const char* lipDamp    = "lipDamp";     // 0..1
    inline constexpr const char* embouchure = "embouchure";  // 0..1
    inline constexpr const char* bendRange  = "bendRange";   // semitones

    // Voice (vocal tract)
    inline constexpr const char* tractMix   = "tractMix";    // 0..1
    inline constexpr const char* vowelX     = "vowelX";      // 0..1  u-o-a-e-i
    inline constexpr const char* vowelY     = "vowelY";      // 0..1  closed-open
    inline constexpr const char* growl      = "growl";       // 0..1
    inline constexpr const char* growlPitch = "growlPitch";  // semitones above drone

    // Instrument (bore)
    inline constexpr const char* tune       = "tune";        // cents
    inline constexpr const char* bell       = "bell";        // 0..1
    inline constexpr const char* flare      = "flare";       // 0..1
    inline constexpr const char* texture    = "texture";     // 0..1
    inline constexpr const char* wallDamp   = "wallDamp";    // 0..1
    inline constexpr const char* boreProfile= "boreProfile"; // choice
    inline constexpr const char* material   = "material";    // choice

    // Order must match didge::BoreProfile / didge::BoreMaterial in
    // dsp/DidgeModel.h.
    inline const juce::StringArray boreProfileNames {
        "Natural", "Cylinder", "Cone", "Flared", "Horn",
        "Trumpet", "Trombone", "Flugelhorn", "French Horn", "Tuba",
        "Alphorn", "Contrabass"
    };
    inline const juce::StringArray materialNames {
        "Wood", "Bamboo", "Brass", "Steel", "Glass"
    };

    // Output
    inline constexpr const char* spaceMix   = "spaceMix";    // 0..1
    inline constexpr const char* spaceSize  = "spaceSize";   // 0..1
    inline constexpr const char* outGain    = "outGain";     // dB

    inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        using P  = juce::AudioParameterFloat;
        using Pb = juce::AudioParameterBool;
        using Pc = juce::AudioParameterChoice;
        using Rng = juce::NormalisableRange<float>;

        std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
        auto add = [&params] (auto p) { params.push_back (std::move (p)); };

        auto pct = [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; };
        auto attrs = [] (auto fn)
        {
            return juce::AudioParameterFloatAttributes{}.withStringFromValueFunction (fn);
        };

        // ---- Breath ---------------------------------------------------------
        add (std::make_unique<P> (juce::ParameterID { pressure, 1 }, "Breath",
                                  Rng { 0.0f, 1.0f, 0.0f }, 0.62f, attrs (pct)));
        {
            Rng r { 1.0f, 500.0f };
            r.setSkewForCentre (60.0f);
            add (std::make_unique<P> (juce::ParameterID { attack, 1 }, "Attack", r, 40.0f,
                                      attrs ([] (float v, int) { return juce::String (juce::roundToInt (v)) + " ms"; })));
        }
        {
            Rng r { 5.0f, 2000.0f };
            r.setSkewForCentre (200.0f);
            add (std::make_unique<P> (juce::ParameterID { release, 1 }, "Release", r, 140.0f,
                                      attrs ([] (float v, int) { return juce::String (juce::roundToInt (v)) + " ms"; })));
        }
        {
            Rng r { 0.1f, 12.0f };
            r.setSkewForCentre (4.0f);
            add (std::make_unique<P> (juce::ParameterID { vibRate, 1 }, "Vibrato Rate", r, 4.5f,
                                      attrs ([] (float v, int) { return juce::String (v, 2) + " Hz"; })));
        }
        add (std::make_unique<P> (juce::ParameterID { vibDepth, 1 }, "Vibrato Depth",
                                  Rng { 0.0f, 1.0f, 0.0f }, 0.0f, attrs (pct)));
        add (std::make_unique<P> (juce::ParameterID { breathNoise, 1 }, "Breath Noise",
                                  Rng { 0.0f, 1.0f, 0.0f }, 0.25f, attrs (pct)));

        // Optional decay stage. With it off the drone simply sustains, which
        // is how the instrument is normally played; switching it on lets the
        // breath fall away under a held note, for short struck excitations.
        add (std::make_unique<Pb> (juce::ParameterID { decayOn, 1 }, "Decay On", false));
        {
            Rng r { 20.0f, 4000.0f };
            r.setSkewForCentre (400.0f);
            add (std::make_unique<P> (juce::ParameterID { decay, 1 }, "Decay", r, 500.0f,
                                      attrs ([] (float v, int) { return juce::String (juce::roundToInt (v)) + " ms"; })));
        }
        add (std::make_unique<P> (juce::ParameterID { sustain, 1 }, "Sustain",
                                  Rng { 0.0f, 1.0f, 0.0f }, 0.0f, attrs (pct)));

        // ---- Performance ----------------------------------------------------
        add (std::make_unique<Pc> (juce::ParameterID { velTarget, 1 }, "Velocity To",
                                   velTargetNames, 1));
        add (std::make_unique<P> (juce::ParameterID { velAmount, 1 }, "Velocity Amount",
                                  Rng { 0.0f, 1.0f, 0.0f }, 0.6f, attrs (pct)));
        // Deliberately small at its default: a real player is inconsistent,
        // but only slightly, and anything audible as an effect is too much.
        add (std::make_unique<P> (juce::ParameterID { humanize, 1 }, "Humanize",
                                  Rng { 0.0f, 1.0f, 0.0f }, 0.35f, attrs (pct)));

        // ---- Embouchure -----------------------------------------------------
        add (std::make_unique<P> (juce::ParameterID { tension, 1 }, "Lip Tension",
                                  Rng { -12.0f, 12.0f, 0.01f }, 0.0f,
                                  attrs ([] (float v, int) { return juce::String (v, 2) + " st"; })));
        add (std::make_unique<P> (juce::ParameterID { lipDamp, 1 }, "Lip Damping",
                                  Rng { 0.0f, 1.0f, 0.0f }, 0.18f, attrs (pct)));
        add (std::make_unique<P> (juce::ParameterID { embouchure, 1 }, "Embouchure",
                                  Rng { 0.0f, 1.0f, 0.0f }, 0.5f, attrs (pct)));
        add (std::make_unique<P> (juce::ParameterID { bendRange, 1 }, "Bend Range",
                                  Rng { 0.0f, 24.0f, 0.5f }, 2.0f,
                                  attrs ([] (float v, int)
                                  {
                                      return juce::String (v, 1) + (v == 1.0f ? " st" : " st");
                                  })));

        // ---- Voice ----------------------------------------------------------
        add (std::make_unique<P> (juce::ParameterID { tractMix, 1 }, "Voice Amount",
                                  Rng { 0.0f, 1.0f, 0.0f }, 0.5f, attrs (pct)));
        add (std::make_unique<P> (juce::ParameterID { vowelX, 1 }, "Vowel",
                                  Rng { 0.0f, 1.0f, 0.0f }, 0.35f,
                                  attrs ([] (float v, int)
                                  {
                                      static const char* names[] = { "oo", "oh", "ah", "eh", "ee" };
                                      const int i = juce::jlimit (0, 4, juce::roundToInt (v * 4.0f));
                                      return juce::String (names[i]);
                                  })));
        add (std::make_unique<P> (juce::ParameterID { vowelY, 1 }, "Mouth Open",
                                  Rng { 0.0f, 1.0f, 0.0f }, 0.5f, attrs (pct)));
        add (std::make_unique<P> (juce::ParameterID { growl, 1 }, "Growl",
                                  Rng { 0.0f, 1.0f, 0.0f }, 0.0f, attrs (pct)));
        add (std::make_unique<P> (juce::ParameterID { growlPitch, 1 }, "Growl Pitch",
                                  Rng { 0.0f, 36.0f, 0.1f }, 19.0f,
                                  attrs ([] (float v, int) { return juce::String (v, 1) + " st"; })));

        // ---- Instrument -----------------------------------------------------
        add (std::make_unique<P> (juce::ParameterID { tune, 1 }, "Tune",
                                  Rng { -100.0f, 100.0f, 0.1f }, 0.0f,
                                  attrs ([] (float v, int) { return juce::String (v, 1) + " ct"; })));
        add (std::make_unique<P> (juce::ParameterID { bell, 1 }, "Bell",
                                  Rng { 0.0f, 1.0f, 0.0f }, 0.4f, attrs (pct)));
        add (std::make_unique<P> (juce::ParameterID { flare, 1 }, "Flare",
                                  Rng { 0.0f, 1.0f, 0.0f }, 0.5f, attrs (pct)));
        add (std::make_unique<P> (juce::ParameterID { texture, 1 }, "Wall Texture",
                                  Rng { 0.0f, 1.0f, 0.0f }, 0.3f, attrs (pct)));
        add (std::make_unique<P> (juce::ParameterID { wallDamp, 1 }, "Wall Damping",
                                  Rng { 0.0f, 1.0f, 0.0f }, 0.3f, attrs (pct)));
        // The profile sets the resonance series, which is most of what makes
        // one wind instrument sound unlike another.
        add (std::make_unique<Pc> (juce::ParameterID { boreProfile, 1 }, "Bore Profile",
                                   boreProfileNames, 0));
        add (std::make_unique<Pc> (juce::ParameterID { material, 1 }, "Material",
                                   materialNames, 0));

        // ---- Output ---------------------------------------------------------
        add (std::make_unique<P> (juce::ParameterID { spaceMix, 1 }, "Space",
                                  Rng { 0.0f, 1.0f, 0.0f }, 0.18f, attrs (pct)));
        add (std::make_unique<P> (juce::ParameterID { spaceSize, 1 }, "Space Size",
                                  Rng { 0.0f, 1.0f, 0.0f }, 0.4f, attrs (pct)));
        add (std::make_unique<P> (juce::ParameterID { outGain, 1 }, "Output",
                                  Rng { -24.0f, 12.0f, 0.1f }, 0.0f,
                                  attrs ([] (float v, int) { return juce::String (v, 1) + " dB"; })));

        return { params.begin(), params.end() };
    }
} // namespace didge::ids
