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

#include "PresetManager.h"
#include "../ParameterIDs.h"

#ifndef DIDGE_VERSION
 #define DIDGE_VERSION "dev"   // set by CMake on the plugin target; tests fall back
#endif

namespace
{
    // Factory bank. Values are in real parameter units. Any parameter not
    // listed resets to its default first, so presets are self-contained.
    std::vector<PresetManager::Preset> makeFactory()
    {
        using namespace didge::ids;
        return {
            { "Deep Drone", {
                { pressure, 0.60f }, { breathNoise, 0.22f }, { lipDamp, 0.18f },
                { embouchure, 0.50f }, { tractMix, 0.45f }, { vowelX, 0.20f },
                { bell, 0.40f }, { flare, 0.50f }, { texture, 0.30f }, { wallDamp, 0.30f },
                { spaceMix, 0.18f }, { spaceSize, 0.40f },
            } },
            { "Yidaki", {
                // Bright, hard-edged traditional stringybark instrument:
                // narrow bell, harder walls, more upper harmonics.
                { pressure, 0.72f }, { breathNoise, 0.30f }, { lipDamp, 0.12f },
                { embouchure, 0.42f }, { tractMix, 0.55f }, { vowelX, 0.45f },
                { bell, 0.26f }, { flare, 0.35f }, { texture, 0.45f }, { wallDamp, 0.15f },
                { spaceMix, 0.14f }, { spaceSize, 0.30f },
            } },
            { "Termite Bore", {
                // Irregular, hollowed-out wall: maximum texture.
                { pressure, 0.58f }, { breathNoise, 0.35f }, { lipDamp, 0.22f },
                { embouchure, 0.55f }, { tractMix, 0.50f }, { vowelX, 0.30f },
                { bell, 0.55f }, { flare, 0.65f }, { texture, 0.95f }, { wallDamp, 0.40f },
                { spaceMix, 0.22f }, { spaceSize, 0.45f },
            } },
            { "Circular Breath", {
                // Long release plus slow vibrato so overlapping notes blend
                // the way a circular-breathing player sustains a drone.
                { pressure, 0.55f }, { attack, 90.0f }, { release, 520.0f },
                { vibRate, 3.2f }, { vibDepth, 0.18f }, { breathNoise, 0.28f },
                { lipDamp, 0.20f }, { tractMix, 0.48f }, { vowelX, 0.25f },
                { bell, 0.42f }, { spaceMix, 0.26f }, { spaceSize, 0.55f },
            } },
            { "Rhythm Machine", {
                // Short, tongued attacks for the percussive didgeridoo style.
                { pressure, 0.78f }, { attack, 6.0f }, { release, 45.0f },
                { breathNoise, 0.45f }, { lipDamp, 0.14f }, { embouchure, 0.40f },
                { tractMix, 0.62f }, { vowelX, 0.55f }, { vowelY, 0.65f },
                { bell, 0.38f }, { texture, 0.35f },
                { spaceMix, 0.12f }, { spaceSize, 0.28f },
            } },
            { "Growl Beast", {
                { pressure, 0.85f }, { breathNoise, 0.40f }, { lipDamp, 0.10f },
                { embouchure, 0.38f }, { tractMix, 0.75f }, { vowelX, 0.40f },
                { growl, 0.75f }, { growlPitch, 19.0f },
                { bell, 0.48f }, { flare, 0.55f }, { texture, 0.50f },
                { spaceMix, 0.16f }, { spaceSize, 0.38f },
            } },
            { "Wobble Bass", {
                { pressure, 0.68f }, { vibRate, 6.5f }, { vibDepth, 0.55f },
                { breathNoise, 0.20f }, { lipDamp, 0.25f }, { embouchure, 0.60f },
                { tractMix, 0.70f }, { vowelX, 0.10f }, { vowelY, 0.30f },
                { bell, 0.62f }, { flare, 0.45f }, { wallDamp, 0.45f },
                { spaceMix, 0.20f }, { spaceSize, 0.50f },
            } },
            { "High Mago", {
                // Shorter, higher-pitched instrument; tighter embouchure.
                { pressure, 0.66f }, { breathNoise, 0.26f }, { lipDamp, 0.14f },
                { embouchure, 0.35f }, { tractMix, 0.52f }, { vowelX, 0.60f },
                { bell, 0.30f }, { flare, 0.40f }, { texture, 0.25f }, { wallDamp, 0.22f },
                { spaceMix, 0.18f }, { spaceSize, 0.35f },
            } },
            { "Wide Bell", {
                { pressure, 0.64f }, { breathNoise, 0.24f }, { lipDamp, 0.20f },
                { embouchure, 0.55f }, { tractMix, 0.45f }, { vowelX, 0.20f },
                { bell, 0.95f }, { flare, 0.80f }, { texture, 0.30f }, { wallDamp, 0.35f },
                { spaceMix, 0.30f }, { spaceSize, 0.65f },
            } },
            { "Dry & Close", {
                { pressure, 0.62f }, { breathNoise, 0.18f }, { lipDamp, 0.18f },
                { tractMix, 0.40f }, { vowelX, 0.30f },
                { bell, 0.40f }, { flare, 0.50f }, { wallDamp, 0.30f },
                { spaceMix, 0.0f }, { spaceSize, 0.30f },
            } },
        };
    }
} // namespace

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& state)
    : apvts (state), factory (makeFactory())
{
    currentName = factory.empty() ? juce::String() : factory.front().name;
}

juce::StringArray PresetManager::getFactoryNames() const
{
    juce::StringArray names;
    for (const auto& p : factory) names.add (p.name);
    return names;
}

juce::StringArray PresetManager::getUserNames() const
{
    juce::StringArray names;
    const auto dir = userPresetDirectory();
    for (const auto& f : dir.findChildFiles (juce::File::findFiles, false, "*.xml"))
        names.add (f.getFileNameWithoutExtension());
    names.sortNatural();
    return names;
}

juce::File PresetManager::userPresetDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Didge").getChildFile ("Presets");
}

void PresetManager::applyPreset (const Preset& preset)
{
    // Reset everything to defaults first so presets are self-contained.
    for (auto* p : apvts.processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            rp->beginChangeGesture();
            rp->setValueNotifyingHost (rp->getDefaultValue());
            rp->endChangeGesture();
        }

    for (const auto& [id, value] : preset.values)
        if (auto* rp = apvts.getParameter (id))
        {
            rp->beginChangeGesture();
            rp->setValueNotifyingHost (rp->convertTo0to1 (value));
            rp->endChangeGesture();
        }

    currentName = preset.name;
    if (postLoadHook) postLoadHook();
}

void PresetManager::loadFactory (int index)
{
    if (index >= 0 && index < static_cast<int> (factory.size()))
        applyPreset (factory[static_cast<size_t> (index)]);
}

void PresetManager::loadByName (const juce::String& name)
{
    for (size_t i = 0; i < factory.size(); ++i)
        if (factory[i].name == name)
        {
            applyPreset (factory[i]);
            return;
        }

    const auto file = userPresetDirectory().getChildFile (name + ".xml");
    if (! file.existsAsFile()) return;

    if (auto xml = juce::parseXML (file))
    {
        auto tree = juce::ValueTree::fromXml (*xml);
        if (tree.isValid())
        {
            Preset p;
            p.name = name;
            // Stored as the APVTS tree; convert to id/value pairs so the
            // same defaults-first apply path runs for user presets too.
            for (int i = 0; i < tree.getNumChildren(); ++i)
            {
                auto child = tree.getChild (i);
                if (child.hasProperty ("id") && child.hasProperty ("value"))
                    p.values.emplace_back (child["id"].toString(), (float) child["value"]);
            }
            applyPreset (p);
        }
    }
}

void PresetManager::saveUser (const juce::String& name)
{
    auto dir = userPresetDirectory();
    dir.createDirectory();

    juce::ValueTree tree ("DidgePreset");
    tree.setProperty ("version", DIDGE_VERSION, nullptr);
    for (auto* p : apvts.processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            juce::ValueTree param ("Param");
            param.setProperty ("id", rp->getParameterID(), nullptr);
            param.setProperty ("value", rp->convertFrom0to1 (rp->getValue()), nullptr);
            tree.addChild (param, -1, nullptr);
        }

    if (auto xml = tree.createXml())
        xml->writeTo (dir.getChildFile (name + ".xml"));

    currentName = name;
    if (postLoadHook) postLoadHook();
}

bool PresetManager::deleteUser (const juce::String& name)
{
    return userPresetDirectory().getChildFile (name + ".xml").deleteFile();
}

juce::StringArray PresetManager::allNames() const
{
    auto names = getFactoryNames();
    names.addArray (getUserNames());
    return names;
}

void PresetManager::next()
{
    const auto names = allNames();
    if (names.isEmpty()) return;
    const int idx = names.indexOf (currentName);
    loadByName (names[(idx + 1 + names.size()) % names.size()]);
}

void PresetManager::previous()
{
    const auto names = allNames();
    if (names.isEmpty()) return;
    const int idx = juce::jmax (0, names.indexOf (currentName));
    loadByName (names[(idx - 1 + names.size()) % names.size()]);
}
