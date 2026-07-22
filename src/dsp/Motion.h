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

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace qube
{

// Automated source movement. Phase-driven so tempo-synced modes lock to the
// host transport deterministically: for a given (phase, mode, radius) the
// offset is a pure function — except Random, which keeps a seeded RNG and a
// slewed target so it survives transport jumps gracefully.
//
// The offset returned is added to the user's (posX, posY); the UI draws the
// same path shapes from the same formulas (room.jsx), so what you see is
// literally what plays.
class Motion
{
public:
    enum class Mode { manual = 0, orbit, figure8, pendulum, bounce, random };

    struct Offset { float dx = 0.0f, dy = 0.0f; };

    void reset (uint32_t seedValue = 0x9e3779b9u)
    {
        rngState = seedValue != 0 ? seedValue : 1u;
        curX = curY = 0.0f;
        targetX = targetY = 0.0f;
        lastStep = -1;
    }

    // phase01: motion cycle phase in [0, 1). radius: 0..1 room units.
    // dtCycles: phase advanced since the previous call, in cycles (used only
    // by the random-walk slew, which is defined in cycle time so its feel
    // scales with the motion rate).
    Offset offsetFor (Mode mode, double phase01, float radius, double dtCycles)
    {
        const float r = std::clamp (radius, 0.0f, 1.0f);
        const auto  p = static_cast<float> (phase01 - std::floor (phase01));
        const float w = 2.0f * 3.14159265358979323846f * p;

        switch (mode)
        {
            case Mode::manual:
                return {};

            case Mode::orbit:
                // Starts at the top of the circle (front), travels clockwise.
                return { r * std::sin (w), r * std::cos (w) };

            case Mode::figure8:
                // Horizontal infinity symbol: full-width sweep, half-height loops.
                return { r * std::sin (w), 0.5f * r * std::sin (2.0f * w) };

            case Mode::pendulum:
                // Left <-> right sweep through the centre.
                return { r * std::sin (w), 0.0f };

            case Mode::bounce:
                // Front <-> back sweep through the centre.
                return { 0.0f, r * std::cos (w) };

            case Mode::random:
            {
                // New target twice per cycle; slew toward it with a time
                // constant of a quarter cycle so the movement is wandering,
                // not teleporting. dt-based so it is block-size independent.
                const int step = static_cast<int> (std::floor (phase01 * 2.0));
                if (step != lastStep)
                {
                    lastStep = step;
                    targetX = r * nextUniform();
                    targetY = r * nextUniform();
                }
                // Slew with tau = quarter cycle: reaches ~86% of the way to
                // the target before the next one lands.
                const float k = 1.0f - static_cast<float> (std::exp (-dtCycles / 0.25));
                curX += (targetX - curX) * k;
                curY += (targetY - curY) * k;
                return { curX, curY };
            }
        }
        return {};
    }

private:
    // xorshift32 — tiny, deterministic, no libc rand state.
    float nextUniform()
    {
        rngState ^= rngState << 13;
        rngState ^= rngState >> 17;
        rngState ^= rngState << 5;
        return (static_cast<float> (rngState & 0xffffffu) / static_cast<float> (0xffffff)) * 2.0f - 1.0f;
    }

    uint32_t rngState = 1u;
    float curX = 0.0f, curY = 0.0f;
    float targetX = 0.0f, targetY = 0.0f;
    int   lastStep = -1;
};

} // namespace qube
