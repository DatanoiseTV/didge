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

#include "PresetManager.h"
#include "../ParameterIDs.h"

#ifndef QUBE_VERSION
 #define QUBE_VERSION "dev"   // set by CMake on the plugin target; tests fall back
#endif

namespace
{
    // Factory bank. Values are in real parameter units. Any parameter not
    // listed resets to its default first, so presets are self-contained.
    std::vector<PresetManager::Preset> makeFactory()
    {
        using namespace qube::ids;
        return {
            { "Center Stage", {
                { posX, 0.0f }, { posY, 0.6f }, { spread, 0.2f },
                { motionMode, 0 }, { roomMix, 0.15f }, { roomSize, 0.35f },
            } },
            { "Slow Orbit", {
                { posX, 0.0f }, { posY, 0.0f }, { spread, 0.15f },
                { motionMode, 1 }, { motionRate, 0.08f }, { motionRadius, 0.75f },
                { distAmount, 0.45f }, { airAbsorb, 0.5f },
                { roomMix, 0.3f }, { roomSize, 0.55f }, { roomDamp, 0.45f },
            } },
            { "Vertigo", {
                { posX, 0.0f }, { posY, 0.0f },
                { motionMode, 1 }, { motionRate, 1.8f }, { motionRadius, 0.9f },
                { doppler, 0.65f }, { distAmount, 0.6f }, { airAbsorb, 0.6f },
                { roomMix, 0.2f }, { roomSize, 0.5f },
            } },
            { "Figure Eight", {
                { posX, 0.0f }, { posY, 0.0f },
                { motionMode, 2 }, { motionRate, 0.22f }, { motionRadius, 0.85f },
                { distAmount, 0.5f }, { roomMix, 0.25f }, { roomSize, 0.5f },
            } },
            { "Synced Pendulum", {
                { posX, 0.0f }, { posY, 0.35f },
                { motionMode, 3 }, { motionSync, 1.0f }, { motionDiv, 3 },
                { motionRadius, 0.8f }, { roomMix, 0.15f },
            } },
            { "Front-Back Bounce", {
                { posX, 0.0f }, { posY, 0.0f },
                { motionMode, 4 }, { motionSync, 1.0f }, { motionDiv, 2 },
                { motionRadius, 0.85f }, { doppler, 0.4f },
                { distAmount, 0.55f }, { airAbsorb, 0.55f }, { roomMix, 0.3f },
            } },
            { "Haunted Hallway", {
                { posX, 0.0f }, { posY, -0.2f },
                { motionMode, 5 }, { motionRate, 0.15f }, { motionRadius, 0.9f },
                { distAmount, 0.7f }, { airAbsorb, 0.8f }, { doppler, 0.25f },
                { roomMix, 0.5f }, { roomSize, 0.85f }, { roomDamp, 0.6f },
            } },
            { "Fly-By", {
                { posX, 0.0f }, { posY, 0.0f },
                { motionMode, 3 }, { motionRate, 0.5f }, { motionRadius, 1.0f },
                { doppler, 1.0f }, { distAmount, 0.8f }, { airAbsorb, 0.7f },
                { roomMix, 0.2f }, { roomSize, 0.6f },
            } },
            { "Wide & Close", {
                { posX, 0.0f }, { posY, 0.45f }, { spread, 0.85f },
                { motionMode, 0 }, { distAmount, 0.2f }, { airAbsorb, 0.2f },
                { roomMix, 0.12f }, { roomSize, 0.3f },
            } },
            { "Distant Storm", {
                { posX, -0.4f }, { posY, -0.85f }, { spread, 0.5f },
                { motionMode, 5 }, { motionRate, 0.06f }, { motionRadius, 0.35f },
                { distAmount, 0.9f }, { airAbsorb, 0.9f },
                { roomMix, 0.55f }, { roomSize, 0.95f }, { roomDamp, 0.35f },
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
        .getChildFile ("Qube").getChildFile ("Presets");
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

    juce::ValueTree tree ("QubePreset");
    tree.setProperty ("version", QUBE_VERSION, nullptr);
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
