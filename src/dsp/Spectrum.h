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

#include <algorithm>
#include <cmath>

namespace didge
{

// Log-spaced analyser for the display.
//
// A filter bank rather than an FFT: the bands are wanted on a log axis anyway,
// so constant-Q gives even resolution per octave where a linear FFT would
// crowd everything above the first octave into a handful of bins and leave the
// bottom — which is where a didgeridoo lives — almost unresolved. It also
// keeps the DSP core free of any framework dependency.
//
// Each band is a topology-preserving state variable filter tapped at its
// bandpass output, followed by a peak follower that rises fast and falls
// slowly so the display reads steadily at any frame rate.
class Spectrum
{
public:
    static constexpr int   kBands = 32;
    static constexpr float kLoHz  = 45.0f;
    static constexpr float kHiHz  = 12000.0f;
    static constexpr float kQ     = 4.5f;

    void prepare (double sampleRate)
    {
        fs = static_cast<float> (sampleRate);
        for (int i = 0; i < kBands; ++i)
        {
            const float t = static_cast<float> (i) / static_cast<float> (kBands - 1);
            const float f = kLoHz * std::pow (kHiHz / kLoHz, t);
            centre[i] = f;

            // Above Nyquist the band is meaningless; park it low and let it
            // read silence rather than alias.
            const float fc = std::min (f, 0.45f * fs);
            const float g = std::tan (3.14159265f * fc / fs);
            const float k = 1.0f / kQ;
            a1[i] = 1.0f / (1.0f + g * (g + k));
            a2[i] = g * a1[i];
            a3[i] = g * a2[i];
        }
        attack  = 1.0f - std::exp (-1.0f / (0.004f * fs));
        release = 1.0f - std::exp (-1.0f / (0.180f * fs));
        reset();
    }

    void reset()
    {
        for (int i = 0; i < kBands; ++i)
        {
            ic1[i] = ic2[i] = 0.0f;
            env[i] = 0.0f;
        }
    }

    void push (float x)
    {
        for (int i = 0; i < kBands; ++i)
        {
            const float v3 = x - ic2[i];
            const float v1 = a1[i] * ic1[i] + a2[i] * v3;
            const float v2 = ic2[i] + a2[i] * ic1[i] + a3[i] * v3;
            ic1[i] = 2.0f * v1 - ic1[i];
            ic2[i] = 2.0f * v2 - ic2[i];

            const float m = std::abs (v1) + 1.0e-20f;   // bandpass tap
            env[i] += (m > env[i] ? attack : release) * (m - env[i]);
        }
    }

    // Band level in dBFS, floored so the display has a defined bottom.
    float levelDb (int i) const
    {
        return 20.0f * std::log10 (std::max (1.0e-6f, env[i]));
    }

    float centreHz (int i) const { return centre[i]; }

private:
    float fs = 48000.0f;
    float centre[kBands] {};
    float a1[kBands] {}, a2[kBands] {}, a3[kBands] {};
    float ic1[kBands] {}, ic2[kBands] {};
    float env[kBands] {};
    float attack = 0.05f, release = 0.001f;
};

} // namespace didge
