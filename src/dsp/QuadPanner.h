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

#include <array>
#include <algorithm>
#include <cmath>

namespace qube
{

// Room coordinate convention used everywhere in Qube:
//   x: -1 = left,  +1 = right
//   y: -1 = back,  +1 = front
//   listener at the origin, speakers on the corners of the unit square.
// Azimuth is measured from the front (+y axis), positive clockwise (to the
// right), in radians. atan2(x, y) — note the argument order.
//
// Speaker order matches JUCE's quadraphonic channel set:
//   0 = front-left (-45 deg), 1 = front-right (+45), 2 = rear-left (-135),
//   3 = rear-right (+135).
namespace quad
{
    inline constexpr int   kNumSpeakers = 4;
    inline constexpr float kSpeakerAzDeg[kNumSpeakers] = { -45.0f, 45.0f, -135.0f, 135.0f };

    inline constexpr float pi = 3.14159265358979323846f;

    inline float wrapPi (float a)
    {
        while (a >  pi) a -= 2.0f * pi;
        while (a < -pi) a += 2.0f * pi;
        return a;
    }

    // Equal-power pairwise (2D VBAP) gains for one direction. All adjacent
    // speaker pairs are 90 degrees apart, so within a sector the tangent law
    // reduces to a quarter-cycle sine/cosine crossfade — exact equal power:
    // g_a^2 + g_b^2 == 1 for every azimuth.
    inline std::array<float, 4> directionGains (float azRad)
    {
        const float az = wrapPi (azRad);
        std::array<float, 4> g { 0.0f, 0.0f, 0.0f, 0.0f };

        // Sector boundaries at -135, -45, +45, +135 degrees.
        constexpr float q = pi / 4.0f;          // 45 deg
        auto blend = [] (float t) { return std::pair { std::cos (t * pi / 2.0f), std::sin (t * pi / 2.0f) }; };

        if (az >= -q && az < q)                  // front: FL -> FR
        {
            auto [ga, gb] = blend ((az + q) / (2.0f * q));
            g[0] = ga; g[1] = gb;
        }
        else if (az >= q && az < 3.0f * q)       // right: FR -> RR
        {
            auto [ga, gb] = blend ((az - q) / (2.0f * q));
            g[1] = ga; g[3] = gb;
        }
        else if (az >= -3.0f * q && az < -q)     // left: RL -> FL
        {
            auto [ga, gb] = blend ((az + 3.0f * q) / (2.0f * q));
            g[2] = ga; g[0] = gb;
        }
        else                                     // back: RR -> RL (through 180)
        {
            const float t = az >= 0.0f ? (az - 3.0f * q) / (2.0f * q)
                                       : (az + 5.0f * q) / (2.0f * q);
            auto [ga, gb] = blend (t);
            g[3] = ga; g[2] = gb;
        }
        return g;
    }

    // Full panning law: direction + source spread + interior (in-head) blend.
    //
    //  azRad    direction of the source centre
    //  spread01 0 = point source, 1 = wraps ~180 degrees. Rendered MDAP-style
    //           as three weighted virtual directions, summed in the power
    //           domain, so total energy stays 1.
    //  interior 0 = source at the listener position (all speakers equal),
    //           1 = fully directional. Callers derive this from distance so a
    //           source crossing the centre of the room morphs smoothly through
    //           "everywhere" instead of snapping across.
    //
    // Result is power-normalised: sum of squares == 1 (within float rounding).
    inline std::array<float, 4> panGains (float azRad, float spread01, float interior)
    {
        const float sigma = std::clamp (spread01, 0.0f, 1.0f) * (pi / 2.0f);
        std::array<float, 4> p { 0.0f, 0.0f, 0.0f, 0.0f };

        if (sigma < 1.0e-3f)
        {
            const auto g = directionGains (azRad);
            for (int i = 0; i < 4; ++i) p[(size_t) i] = g[(size_t) i] * g[(size_t) i];
        }
        else
        {
            constexpr float wc = 0.5f, ws = 0.25f;   // centre + two side taps
            const auto gc = directionGains (azRad);
            const auto gl = directionGains (azRad - sigma);
            const auto gr = directionGains (azRad + sigma);
            for (int i = 0; i < 4; ++i)
                p[(size_t) i] = wc * gc[(size_t) i] * gc[(size_t) i] + ws * gl[(size_t) i] * gl[(size_t) i]
                             + ws * gr[(size_t) i] * gr[(size_t) i];
        }

        const float c = std::clamp (interior, 0.0f, 1.0f);
        std::array<float, 4> g;
        float sum = 0.0f;
        for (int i = 0; i < 4; ++i)
        {
            // Blend in the power domain between "equal from everywhere" (0.25
            // per speaker) and the directional distribution — both are power-
            // normalised, so the blend is too.
            const float pw = (1.0f - c) * 0.25f + c * p[(size_t) i];
            g[(size_t) i] = std::sqrt (std::max (0.0f, pw));
            sum += pw;
        }
        // Guard against float drift; keeps sum-of-squares at exactly 1.
        if (sum > 1.0e-9f)
        {
            const float norm = 1.0f / std::sqrt (sum);
            for (auto& v : g) v *= norm;
        }
        return g;
    }
} // namespace quad
} // namespace qube
