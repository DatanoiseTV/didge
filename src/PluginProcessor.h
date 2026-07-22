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
#include "dsp/SpatialEngine.h"
#include "presets/PresetManager.h"
#include <unordered_map>

// Top-level plugin. Owns the parameter tree, resolves the host transport for
// tempo-synced motion, converts raw parameters into engine units and hands
// audio to the SpatialEngine.
class QubeAudioProcessor : public juce::AudioProcessor,
                           private juce::ValueTree::Listener
{
public:
    QubeAudioProcessor();
    ~QubeAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Qube"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 3.5; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getValueTreeState() { return apvts; }
    PresetManager& getPresetManager() { return presetManager; }

    qube::SpatialEngine&       getEngine()       { return engine; }
    const qube::SpatialEngine& getEngine() const { return engine; }

    double getCurrentBpm() const { return currentBpm.load (std::memory_order_relaxed); }
    bool   isHostPlaying() const { return hostPlaying.load (std::memory_order_relaxed); }
    int    getActiveOutputChannels() const { return activeOutputChannels.load (std::memory_order_relaxed); }

    // True when any APVTS param has changed since the last preset load or
    // user save. Cleared by the preset manager's post-load hook.
    bool isCurrentPresetDirty() const { return presetDirty.load (std::memory_order_relaxed); }

private:
    // ValueTree::Listener — flags the current preset dirty on any param edit.
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;

    void snapshotCurrentParams();
    bool currentMatchesSnapshot() const;

    qube::EngineParams buildEngineParams() const;

    juce::AudioProcessorValueTreeState apvts;
    qube::SpatialEngine engine;
    PresetManager presetManager;

    std::atomic<double> currentBpm { 120.0 };
    std::atomic<bool>   hostPlaying { false };
    std::atomic<int>    activeOutputChannels { 2 };

    std::atomic<bool> presetDirty { false };
    std::unordered_map<juce::String, float> cleanSnapshot;

    double sampleRate = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (QubeAudioProcessor)
};
