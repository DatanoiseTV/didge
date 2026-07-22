/*
  Qube — quadraphonic spatial panner
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
namespace qube::ids
{
    // Position
    inline constexpr const char* posX         = "posX";          // -1..+1  (left..right)
    inline constexpr const char* posY         = "posY";          // -1..+1  (back..front)
    inline constexpr const char* spread       = "spread";        // 0..1    source width
    inline constexpr const char* rotate       = "rotate";        // -180..180 degrees, scene rotation

    // Motion engine
    inline constexpr const char* motionMode   = "motionMode";    // choice
    inline constexpr const char* motionRate   = "motionRate";    // Hz, free-running
    inline constexpr const char* motionSync   = "motionSync";    // bool
    inline constexpr const char* motionDiv    = "motionDiv";     // choice, musical division
    inline constexpr const char* motionRadius = "motionRadius";  // 0..1 path size
    inline constexpr const char* motionPhase  = "motionPhase";   // 0..360 degrees
    inline constexpr const char* motionReverse= "motionReverse"; // bool

    // Distance & room
    inline constexpr const char* distAmount   = "distAmount";    // 0..1 distance attenuation
    inline constexpr const char* airAbsorb    = "airAbsorb";     // 0..1 distance LPF
    inline constexpr const char* doppler      = "doppler";       // 0..1
    inline constexpr const char* roomMix      = "roomMix";       // 0..1
    inline constexpr const char* roomSize     = "roomSize";      // 0..1
    inline constexpr const char* roomDamp     = "roomDamp";      // 0..1

    // Output
    inline constexpr const char* outputMode   = "outputMode";    // choice
    inline constexpr const char* masterGain   = "masterGain";    // dB

    // Choice option lists. Shared between the APVTS layout and the engine's
    // interpretation of the stored index; the UI mirrors the same strings.
    inline const juce::StringArray motionModeNames {
        "Manual", "Orbit", "Figure 8", "Pendulum", "Bounce", "Random"
    };
    inline const juce::StringArray motionDivNames {
        "8 bars", "4 bars", "2 bars", "1 bar", "1/2", "1/2T", "1/4", "1/4T", "1/8", "1/8T", "1/16"
    };
    // Beats per full motion cycle for each motionDiv entry (4/4 assumed for
    // "bar" entries — the host meter isn't consulted; this matches how most
    // delay plugins treat bar syncs).
    inline constexpr double motionDivBeats[] = {
        32.0, 16.0, 8.0, 4.0, 2.0, 4.0 / 3.0, 1.0, 2.0 / 3.0, 0.5, 1.0 / 3.0, 0.25
    };
    inline const juce::StringArray outputModeNames {
        "Auto", "Quad", "Binaural", "Stereo UHJ", "Stereo Mix"
    };

    enum class MotionMode { manual = 0, orbit, figure8, pendulum, bounce, random };
    enum class OutputMode { autoDetect = 0, quad, binaural, uhj, stereoMix };

    inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        using P  = juce::AudioParameterFloat;
        using Pb = juce::AudioParameterBool;
        using Pc = juce::AudioParameterChoice;
        using Rng = juce::NormalisableRange<float>;

        std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
        auto add = [&params] (auto p) { params.push_back (std::move (p)); };

        auto pct = [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; };

        // Position. Defaults put the source front-centre, slightly into the room.
        add (std::make_unique<P> (juce::ParameterID { posX, 1 }, "Position X",
                                  Rng { -1.0f, 1.0f, 0.0f }, 0.0f,
                                  juce::AudioParameterFloatAttributes{}.withStringFromValueFunction (
                                      [] (float v, int) { return juce::String (v, 2); })));
        add (std::make_unique<P> (juce::ParameterID { posY, 1 }, "Position Y",
                                  Rng { -1.0f, 1.0f, 0.0f }, 0.5f,
                                  juce::AudioParameterFloatAttributes{}.withStringFromValueFunction (
                                      [] (float v, int) { return juce::String (v, 2); })));
        add (std::make_unique<P> (juce::ParameterID { spread, 1 }, "Spread",
                                  Rng { 0.0f, 1.0f, 0.0f }, 0.15f,
                                  juce::AudioParameterFloatAttributes{}.withStringFromValueFunction (pct)));
        add (std::make_unique<P> (juce::ParameterID { rotate, 1 }, "Rotate",
                                  Rng { -180.0f, 180.0f, 1.0f }, 0.0f,
                                  juce::AudioParameterFloatAttributes{}.withStringFromValueFunction (
                                      [] (float v, int) { return juce::String (juce::roundToInt (v)) + juce::String::fromUTF8 ("\xc2\xb0"); })));

        // Motion
        add (std::make_unique<Pc> (juce::ParameterID { motionMode, 1 }, "Motion", motionModeNames, 0));
        {
            Rng rateRange { 0.02f, 8.0f };
            rateRange.setSkewForCentre (0.5f);
            add (std::make_unique<P> (juce::ParameterID { motionRate, 1 }, "Motion Rate",
                                      rateRange, 0.25f,
                                      juce::AudioParameterFloatAttributes{}.withStringFromValueFunction (
                                          [] (float v, int) { return juce::String (v, 2) + " Hz"; })));
        }
        add (std::make_unique<Pb> (juce::ParameterID { motionSync, 1 }, "Motion Sync", false));
        add (std::make_unique<Pc> (juce::ParameterID { motionDiv, 1 }, "Motion Division", motionDivNames, 3));
        add (std::make_unique<P>  (juce::ParameterID { motionRadius, 1 }, "Motion Radius",
                                   Rng { 0.0f, 1.0f, 0.0f }, 0.5f,
                                   juce::AudioParameterFloatAttributes{}.withStringFromValueFunction (pct)));
        add (std::make_unique<P>  (juce::ParameterID { motionPhase, 1 }, "Motion Phase",
                                   Rng { 0.0f, 360.0f, 1.0f }, 0.0f,
                                   juce::AudioParameterFloatAttributes{}.withStringFromValueFunction (
                                       [] (float v, int) { return juce::String (juce::roundToInt (v)) + juce::String::fromUTF8 ("\xc2\xb0"); })));
        add (std::make_unique<Pb> (juce::ParameterID { motionReverse, 1 }, "Motion Reverse", false));

        // Distance & room
        add (std::make_unique<P> (juce::ParameterID { distAmount, 1 }, "Distance",
                                  Rng { 0.0f, 1.0f, 0.0f }, 0.5f,
                                  juce::AudioParameterFloatAttributes{}.withStringFromValueFunction (pct)));
        add (std::make_unique<P> (juce::ParameterID { airAbsorb, 1 }, "Air Absorb",
                                  Rng { 0.0f, 1.0f, 0.0f }, 0.5f,
                                  juce::AudioParameterFloatAttributes{}.withStringFromValueFunction (pct)));
        add (std::make_unique<P> (juce::ParameterID { doppler, 1 }, "Doppler",
                                  Rng { 0.0f, 1.0f, 0.0f }, 0.0f,
                                  juce::AudioParameterFloatAttributes{}.withStringFromValueFunction (pct)));
        add (std::make_unique<P> (juce::ParameterID { roomMix, 1 }, "Room Mix",
                                  Rng { 0.0f, 1.0f, 0.0f }, 0.25f,
                                  juce::AudioParameterFloatAttributes{}.withStringFromValueFunction (pct)));
        add (std::make_unique<P> (juce::ParameterID { roomSize, 1 }, "Room Size",
                                  Rng { 0.0f, 1.0f, 0.0f }, 0.5f,
                                  juce::AudioParameterFloatAttributes{}.withStringFromValueFunction (pct)));
        add (std::make_unique<P> (juce::ParameterID { roomDamp, 1 }, "Room Damp",
                                  Rng { 0.0f, 1.0f, 0.0f }, 0.5f,
                                  juce::AudioParameterFloatAttributes{}.withStringFromValueFunction (pct)));

        // Output
        add (std::make_unique<Pc> (juce::ParameterID { outputMode, 1 }, "Output Mode", outputModeNames, 0));
        add (std::make_unique<P>  (juce::ParameterID { masterGain, 1 }, "Master Gain",
                                   Rng { -24.0f, 12.0f, 0.1f }, 0.0f,
                                   juce::AudioParameterFloatAttributes{}.withStringFromValueFunction (
                                       [] (float v, int) { return juce::String (v, 1) + " dB"; })));

        return { params.begin(), params.end() };
    }
} // namespace qube::ids
