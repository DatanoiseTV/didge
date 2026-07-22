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

#include "PluginProcessor.h"
#include "ParameterIDs.h"
#include "ui/WebEditor.h"

QubeAudioProcessor::QubeAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::quadraphonic(), true)),
      apvts (*this, nullptr, "PARAMS", qube::ids::createParameterLayout()),
      presetManager (apvts)
{
    apvts.state.addListener (this);
    presetManager.setPostLoadHook ([this]
    {
        snapshotCurrentParams();
        presetDirty.store (false, std::memory_order_relaxed);
    });
    snapshotCurrentParams();
}

QubeAudioProcessor::~QubeAudioProcessor()
{
    apvts.state.removeListener (this);
}

bool QubeAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    const bool inOk  = in == juce::AudioChannelSet::mono()
                    || in == juce::AudioChannelSet::stereo();
    const bool outOk = out == juce::AudioChannelSet::stereo()
                    || out == juce::AudioChannelSet::quadraphonic();
    return inOk && outOk;
}

void QubeAudioProcessor::prepareToPlay (double newSampleRate, int samplesPerBlock)
{
    sampleRate = newSampleRate;
    engine.prepare (newSampleRate, juce::jmax (16, samplesPerBlock));
}

qube::EngineParams QubeAudioProcessor::buildEngineParams() const
{
    using namespace qube::ids;
    auto raw = [this] (const char* id) { return apvts.getRawParameterValue (id)->load (std::memory_order_relaxed); };

    qube::EngineParams p;
    p.posX          = raw (posX);
    p.posY          = raw (posY);
    p.spread        = raw (spread);
    p.rotateDeg     = raw (rotate);

    p.motionMode    = static_cast<int> (raw (motionMode));
    p.motionRateHz  = raw (motionRate);
    p.motionSync    = raw (motionSync) > 0.5f;
    const int divIdx = juce::jlimit (0, (int) std::size (motionDivBeats) - 1,
                                     static_cast<int> (raw (motionDiv)));
    p.motionDivBeats = motionDivBeats[divIdx];
    p.motionRadius  = raw (motionRadius);
    p.motionPhaseDeg = raw (motionPhase);
    p.motionReverse = raw (motionReverse) > 0.5f;

    p.distAmount    = raw (distAmount);
    p.airAbsorb     = raw (airAbsorb);
    p.doppler       = raw (doppler);
    p.roomMix       = raw (roomMix);
    p.roomSize      = raw (roomSize);
    p.roomDamp      = raw (roomDamp);

    p.outputMode    = static_cast<int> (raw (outputMode));
    p.masterGainLin = juce::Decibels::decibelsToGain (raw (masterGain));
    return p;
}

void QubeAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int n      = buffer.getNumSamples();
    if (n == 0 || numOut < 2)
        return;

    activeOutputChannels.store (numOut, std::memory_order_relaxed);

    qube::TransportInfo transport;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto bpm = pos->getBpm())         transport.bpm = *bpm;
            if (auto ppq = pos->getPpqPosition()) transport.ppqPosition = *ppq;
            transport.playing = pos->getIsPlaying();
        }
    currentBpm.store (transport.bpm, std::memory_order_relaxed);
    hostPlaying.store (transport.playing, std::memory_order_relaxed);

    engine.process (buffer.getArrayOfReadPointers(), juce::jmin (numIn, 2),
                    buffer.getArrayOfWritePointers(), numOut,
                    n, buildEngineParams(), transport);
}

juce::AudioProcessorEditor* QubeAudioProcessor::createEditor()
{
    return new qube::WebEditor (*this);
}

void QubeAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void QubeAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            snapshotCurrentParams();
            presetDirty.store (false, std::memory_order_relaxed);
        }
}

void QubeAudioProcessor::valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&)
{
    presetDirty.store (! currentMatchesSnapshot(), std::memory_order_relaxed);
}

void QubeAudioProcessor::snapshotCurrentParams()
{
    cleanSnapshot.clear();
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            cleanSnapshot[rp->getParameterID()] = rp->getValue();
}

bool QubeAudioProcessor::currentMatchesSnapshot() const
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            const auto it = cleanSnapshot.find (rp->getParameterID());
            if (it == cleanSnapshot.end() || std::abs (it->second - rp->getValue()) > 1.0e-4f)
                return false;
        }
    return true;
}

// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new QubeAudioProcessor();
}
