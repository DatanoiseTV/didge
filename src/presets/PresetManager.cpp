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
        // Profile indices (didge::BoreProfile): natural 0, cylinder 1, cone 2,
        // flared 3, horn 4, trumpet 5, trombone 6, flugelhorn 7, frenchHorn 8,
        // tuba 9, alphorn 10, contrabass 11. Exciter (didge::Exciter): lips 0,
        // singleReed 1, doubleReed 2, freeReed 3, airJet 4.
        //
        // The bank spans the instruments the model actually plays in tune and
        // convincingly: the didgeridoo family, the brass family on lips, the
        // clarinet family and free-reed voices, and the flute family on the
        // jet. Reeds are paired with cylindrical bores, where they are stable;
        // a cane reed on a conical bore jumps registers and is left out. Each
        // preset here was rendered and measured -- pitch, level and character --
        // not dialled by ear.
        return {
            // ---- Didgeridoo (natural bore, lips) --------------------------
            { "Deep Drone", {
                { boreProfile, 0.0f }, { exciter, 0.0f },
                { pressure, 0.60f }, { breathNoise, 0.22f }, { lipDamp, 0.18f },
                { embouchure, 0.50f }, { tractMix, 0.45f }, { vowelX, 0.20f },
                { bell, 0.40f }, { flare, 0.50f }, { texture, 0.30f }, { wallDamp, 0.30f },
                { spaceMix, 0.18f }, { spaceSize, 0.40f },
            } },
            { "Yidaki", {
                // Bright, hard-edged traditional stringybark instrument:
                // narrow bell, harder walls, more upper harmonics.
                { boreProfile, 0.0f }, { exciter, 0.0f },
                { tune, -22.0f },
                { pressure, 0.72f }, { breathNoise, 0.30f }, { lipDamp, 0.12f },
                { embouchure, 0.42f }, { tractMix, 0.55f }, { vowelX, 0.45f },
                { bell, 0.26f }, { flare, 0.35f }, { texture, 0.45f }, { wallDamp, 0.15f },
                { spaceMix, 0.14f }, { spaceSize, 0.30f },
            } },
            { "Circular Breath", {
                // Long release plus slow vibrato so overlapping notes blend
                // the way a circular-breathing player sustains a drone.
                { boreProfile, 0.0f }, { exciter, 0.0f },
                { pressure, 0.55f }, { attack, 90.0f }, { release, 520.0f },
                { vibRate, 3.2f }, { vibDepth, 0.18f }, { breathNoise, 0.28f },
                { lipDamp, 0.20f }, { tractMix, 0.48f }, { vowelX, 0.25f },
                { bell, 0.42f }, { spaceMix, 0.26f }, { spaceSize, 0.55f },
            } },
            { "Rhythm Machine", {
                // Short, tongued attacks for the percussive didgeridoo style.
                { boreProfile, 0.0f }, { exciter, 0.0f },
                { pressure, 0.78f }, { attack, 6.0f }, { release, 45.0f },
                { breathNoise, 0.45f }, { lipDamp, 0.14f }, { embouchure, 0.40f },
                { tractMix, 0.62f }, { vowelX, 0.55f }, { vowelY, 0.65f },
                { bell, 0.38f }, { texture, 0.35f },
                { spaceMix, 0.12f }, { spaceSize, 0.28f },
            } },
            { "Growl Beast", {
                { boreProfile, 0.0f }, { exciter, 0.0f },
                { tune, 18.0f },
                { pressure, 0.85f }, { breathNoise, 0.40f }, { lipDamp, 0.10f },
                { embouchure, 0.38f }, { tractMix, 0.75f }, { vowelX, 0.40f },
                { growl, 0.75f }, { growlPitch, 19.0f },
                { bell, 0.48f }, { flare, 0.55f }, { texture, 0.50f },
                { spaceMix, 0.16f }, { spaceSize, 0.38f },
            } },

            // ---- Brass (lips) ---------------------------------------------
            { "Trumpet", {
                { boreProfile, 5.0f }, { exciter, 0.0f },
                { pressure, 0.70f }, { breathNoise, 0.10f }, { lipDamp, 0.10f },
                { embouchure, 0.40f }, { tractMix, 0.30f }, { vowelX, 0.55f },
                { bell, 0.45f }, { flare, 0.55f }, { texture, 0.0f }, { wallDamp, 0.12f },
                { spaceMix, 0.16f }, { spaceSize, 0.42f },
            } },
            { "Trombone", {
                { boreProfile, 6.0f }, { exciter, 0.0f },
                { pressure, 0.66f }, { breathNoise, 0.12f }, { lipDamp, 0.12f },
                { embouchure, 0.46f }, { tractMix, 0.28f }, { vowelX, 0.40f },
                { bell, 0.55f }, { flare, 0.55f }, { texture, 0.0f }, { wallDamp, 0.14f },
                { spaceMix, 0.20f }, { spaceSize, 0.50f },
            } },
            { "French Horn", {
                // Mellow, dark: the tract shapes it round and the walls take a
                // little more of the top than the trumpet's do.
                { boreProfile, 8.0f }, { exciter, 0.0f },
                { tune, 22.0f },
                { pressure, 0.64f }, { breathNoise, 0.10f }, { lipDamp, 0.16f },
                { embouchure, 0.48f }, { tractMix, 0.35f }, { vowelX, 0.25f },
                { bell, 0.62f }, { flare, 0.70f }, { texture, 0.0f }, { wallDamp, 0.22f },
                { spaceMix, 0.26f }, { spaceSize, 0.58f },
            } },
            { "Tuba", {
                { boreProfile, 9.0f }, { exciter, 0.0f },
                { tune, -48.0f },
                { pressure, 0.80f }, { breathNoise, 0.10f }, { lipDamp, 0.18f },
                { embouchure, 0.55f }, { tractMix, 0.30f }, { vowelX, 0.20f },
                { bell, 0.70f }, { flare, 0.55f }, { texture, 0.0f }, { wallDamp, 0.20f },
                { spaceMix, 0.22f }, { spaceSize, 0.55f },
            } },
            { "Flugelhorn", {
                // Conical and soft, the mellowest of the trumpets.
                { boreProfile, 7.0f }, { exciter, 0.0f },
                { pressure, 0.62f }, { breathNoise, 0.10f }, { lipDamp, 0.16f },
                { embouchure, 0.44f }, { tractMix, 0.32f }, { vowelX, 0.30f },
                { bell, 0.50f }, { flare, 0.55f }, { texture, 0.0f }, { wallDamp, 0.18f },
                { spaceMix, 0.24f }, { spaceSize, 0.52f },
            } },
            { "Alphorn", {
                // A very long gentle cone: broad, open and a little rough.
                { boreProfile, 10.0f }, { exciter, 0.0f },
                { tune, -16.0f },
                { pressure, 0.72f }, { breathNoise, 0.16f }, { lipDamp, 0.14f },
                { embouchure, 0.48f }, { tractMix, 0.30f }, { vowelX, 0.25f },
                { bell, 0.55f }, { flare, 0.50f }, { texture, 0.15f }, { wallDamp, 0.20f },
                { spaceMix, 0.32f }, { spaceSize, 0.70f },
            } },

            // ---- Reeds (cylindrical bores) --------------------------------
            { "Clarinet", {
                // Single reed on a cylinder: the odd-harmonic hollow woodwind.
                { boreProfile, 1.0f }, { exciter, 1.0f },
                { pressure, 0.68f }, { breathNoise, 0.10f }, { lipDamp, 0.18f },
                { embouchure, 0.50f }, { tractMix, 0.25f }, { vowelX, 0.45f },
                { bell, 0.30f }, { flare, 0.40f }, { texture, 0.0f }, { wallDamp, 0.16f },
                { spaceMix, 0.16f }, { spaceSize, 0.42f },
            } },
            { "Bass Clarinet", {
                { boreProfile, 1.0f }, { exciter, 1.0f },
                { pressure, 0.70f }, { breathNoise, 0.12f }, { lipDamp, 0.22f },
                { embouchure, 0.55f }, { tractMix, 0.22f }, { vowelX, 0.30f },
                { bell, 0.42f }, { flare, 0.45f }, { texture, 0.0f }, { wallDamp, 0.20f },
                { spaceMix, 0.20f }, { spaceSize, 0.50f },
            } },
            { "Reed Organ", {
                // Free reed: a sprung metal tongue, as in a harmonica or
                // accordion. Its own resonance holds the pitch on any bore.
                { boreProfile, 3.0f }, { exciter, 3.0f },
                { tune, -18.0f },
                { pressure, 0.58f }, { breathNoise, 0.10f }, { lipDamp, 0.14f },
                { embouchure, 0.50f }, { tractMix, 0.30f }, { vowelX, 0.50f },
                { bell, 0.40f }, { flare, 0.55f }, { texture, 0.0f }, { wallDamp, 0.18f },
                { spaceMix, 0.22f }, { spaceSize, 0.48f },
            } },

            // ---- Flute family (air jet) -----------------------------------
            { "Pan Flute", {
                // Air jet across a stopped pipe: breathy, hollow, odd-harmonic.
                { boreProfile, 1.0f }, { exciter, 4.0f },
                { pressure, 0.58f }, { breathNoise, 0.30f }, { attack, 30.0f },
                { embouchure, 0.50f }, { tractMix, 0.25f }, { vowelX, 0.45f },
                { bell, 0.28f }, { flare, 0.40f }, { texture, 0.0f }, { wallDamp, 0.16f },
                { spaceMix, 0.30f }, { spaceSize, 0.62f },
            } },
            { "Recorder", {
                { boreProfile, 1.0f }, { exciter, 4.0f },
                { pressure, 0.64f }, { breathNoise, 0.22f }, { attack, 18.0f },
                { embouchure, 0.46f }, { tractMix, 0.22f }, { vowelX, 0.55f },
                { bell, 0.24f }, { flare, 0.38f }, { texture, 0.0f }, { wallDamp, 0.14f },
                { spaceMix, 0.22f }, { spaceSize, 0.48f },
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
