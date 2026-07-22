/*
  Qube — quadraphonic spatial panner
  Copyright (C) 2026 DatanoiseTV

  State round-trip + preset tests against the real AudioProcessor.
*/

#include "PluginProcessor.h"
#include "ParameterIDs.h"

#include <iostream>

static int failures = 0;

#define CHECK(cond, msg)                                                    \
    do {                                                                    \
        if (!(cond)) {                                                      \
            ++failures;                                                     \
            std::cout << "FAIL " << __FILE__ << ":" << __LINE__ << "  "     \
                      << msg << std::endl;                                  \
        }                                                                   \
    } while (0)

static void setParam (QubeAudioProcessor& p, const char* id, float realValue)
{
    if (auto* rp = p.getValueTreeState().getParameter (id))
        rp->setValueNotifyingHost (rp->convertTo0to1 (realValue));
}

static float getParam (QubeAudioProcessor& p, const char* id)
{
    if (auto* rp = p.getValueTreeState().getParameter (id))
        return rp->convertFrom0to1 (rp->getValue());
    return -999.0f;
}

static void runAudio (QubeAudioProcessor& p, int blocks = 8)
{
    p.setPlayConfigDetails (2, 4, 48000.0, 512);
    p.prepareToPlay (48000.0, 512);
    juce::AudioBuffer<float> buf (4, 512);
    juce::MidiBuffer midi;
    for (int b = 0; b < blocks; ++b)
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 512; ++i)
                buf.setSample (ch, i, 0.25f * std::sin (2.0f * juce::MathConstants<float>::pi
                                                        * 330.0f * (float) (b * 512 + i) / 48000.0f));
        p.processBlock (buf, midi);
        for (int ch = 0; ch < 4; ++ch)
            for (int i = 0; i < 512; ++i)
                CHECK (std::isfinite (buf.getSample (ch, i)), "non-finite audio out");
    }
}

static void testStateRoundTrip()
{
    QubeAudioProcessor a;
    setParam (a, qube::ids::posX, -0.62f);
    setParam (a, qube::ids::posY, 0.31f);
    setParam (a, qube::ids::motionMode, 2.0f);
    setParam (a, qube::ids::roomMix, 0.71f);
    setParam (a, qube::ids::masterGain, -6.5f);

    juce::MemoryBlock state;
    a.getStateInformation (state);

    QubeAudioProcessor b;
    b.setStateInformation (state.getData(), (int) state.getSize());

    CHECK (std::abs (getParam (b, qube::ids::posX) - (-0.62f)) < 1.0e-3f, "posX round trip");
    CHECK (std::abs (getParam (b, qube::ids::posY) - 0.31f) < 1.0e-3f, "posY round trip");
    CHECK (std::abs (getParam (b, qube::ids::motionMode) - 2.0f) < 1.0e-3f, "motionMode round trip");
    CHECK (std::abs (getParam (b, qube::ids::roomMix) - 0.71f) < 1.0e-3f, "roomMix round trip");
    CHECK (std::abs (getParam (b, qube::ids::masterGain) - (-6.5f)) < 1.0e-2f, "masterGain round trip");
    CHECK (! b.isCurrentPresetDirty(), "restored state should not be dirty");
}

static void testFactoryPresets()
{
    QubeAudioProcessor p;
    auto& pm = p.getPresetManager();
    const auto names = pm.getFactoryNames();
    CHECK (names.size() >= 8, "factory bank too small");

    // Unique names.
    juce::StringArray sorted (names);
    sorted.sort (false);
    for (int i = 1; i < sorted.size(); ++i)
        CHECK (sorted[i] != sorted[i - 1], "duplicate preset name: " + sorted[i].toStdString());

    for (const auto& n : names)
    {
        pm.loadByName (n);
        CHECK (pm.getCurrentName() == n, "current name after load: " + n.toStdString());
        CHECK (! p.isCurrentPresetDirty(), "preset load left dirty flag: " + n.toStdString());
        runAudio (p, 4);
    }
}

static void testUserPresetRoundTrip()
{
    const juce::String name = "qube-test-tmp-preset";
    QubeAudioProcessor a;
    setParam (a, qube::ids::posX, 0.42f);
    setParam (a, qube::ids::doppler, 0.9f);
    a.getPresetManager().saveUser (name);

    QubeAudioProcessor b;
    b.getPresetManager().loadByName (name);
    CHECK (std::abs (getParam (b, qube::ids::posX) - 0.42f) < 1.0e-3f, "user preset posX");
    CHECK (std::abs (getParam (b, qube::ids::doppler) - 0.9f) < 1.0e-3f, "user preset doppler");
    CHECK (b.getPresetManager().getUserNames().contains (name), "user preset listed");
    CHECK (a.getPresetManager().deleteUser (name), "user preset delete");
}

static void testBusLayouts()
{
    QubeAudioProcessor p;
    auto layout = p.getBusesLayout();

    layout.getChannelSet (false, 0) = juce::AudioChannelSet::quadraphonic();
    layout.getChannelSet (true, 0)  = juce::AudioChannelSet::stereo();
    CHECK (p.checkBusesLayoutSupported (layout), "stereo->quad rejected");

    layout.getChannelSet (false, 0) = juce::AudioChannelSet::stereo();
    CHECK (p.checkBusesLayoutSupported (layout), "stereo->stereo rejected");

    layout.getChannelSet (true, 0) = juce::AudioChannelSet::mono();
    CHECK (p.checkBusesLayoutSupported (layout), "mono->stereo rejected");

    layout.getChannelSet (false, 0) = juce::AudioChannelSet::create5point1();
    CHECK (! p.checkBusesLayoutSupported (layout), "5.1 out should be rejected");
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    testStateRoundTrip();
    testFactoryPresets();
    testUserPresetRoundTrip();
    testBusLayouts();

    if (failures == 0)
    {
        std::cout << "qube_state_tests: all checks passed" << std::endl;
        return 0;
    }
    std::cout << "qube_state_tests: " << failures << " FAILURE(S)" << std::endl;
    return 1;
}
