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

#include "QuadPanner.h"

#include <array>
#include <cmath>

namespace qube
{

// 90-degree phase-difference network (IIR Hilbert approximation).
//
// Two parallel chains of four second-order allpass sections (in z^-2), with
// chain A followed by a one-sample delay. Across ~200 Hz .. 20 kHz chain B
// leads the delayed chain A by 90 degrees (+-1 deg; verified in
// tests/test_dsp.cpp). Pole positions are Olli Niemitalo's classic set.
// Used by the UHJ encoder for the j() terms.
class PhaseQuadrature
{
public:
    void reset()
    {
        for (auto& s : stA) s = {};
        for (auto& s : stB) s = {};
        zA = 0.0f;
    }

    // Returns {ref, shifted}: `shifted` leads `ref` by 90 degrees (+-1 deg,
    // 200 Hz .. 20 kHz at 48 kHz; verified in tests/test_dsp.cpp). Both are
    // allpass-filtered versions of the input (same magnitude).
    struct Pair { float ref, shifted; };

    Pair process (float x)
    {
        float a = x;
        for (int i = 0; i < 4; ++i) a = stA[(size_t) i].tick (a, kPolesA[i]);
        float b = x;
        for (int i = 0; i < 4; ++i) b = stB[(size_t) i].tick (b, kPolesB[i]);
        // Chain A delayed one sample is the reference phase; chain B leads
        // it by 90 degrees across the audio band.
        const float refOut = zA;
        zA = a;
        return { refOut, b };
    }

private:
    struct Section
    {
        float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;
        float tick (float x, float pole)
        {
            // Second-order allpass in z^-2: y[n] = a*(x[n] + y[n-2]) - x[n-2]
            // with a = pole^2 (the published values are pole positions).
            const float a = pole * pole;
            const float y = a * (x + y2) - x2;
            x2 = x1; x1 = x;
            y2 = y1; y1 = y;
            return y;
        }
    };

    static constexpr float kPolesA[4] = { 0.6923878f, 0.9360654322959f, 0.9882295226860f, 0.9987488452737f };
    static constexpr float kPolesB[4] = { 0.4021921162426f, 0.8561710882420f, 0.9722909545651f, 0.9952884791278f };

    std::array<Section, 4> stA, stB;
    float zA = 0.0f;
};

// Stereo UHJ encoder fed from the four-speaker bed.
//
// The bed is first re-encoded to first-order horizontal B-format (FuMa
// convention: W with 1/sqrt2 weight, X front-positive, Y left-positive) using
// the known speaker azimuths, then folded to two-channel UHJ:
//
//   S = 0.9397*W + 0.1856*X
//   D = j*(-0.3420*W + 0.5099*X) + 0.6555*Y
//   L = (S + D) / 2,  R = (S - D) / 2
//
// UHJ is the ambisonic stereo format: fully mono- and stereo-compatible on
// speakers, and a UHJ-aware decoder can recover the surround field. On plain
// stereo speakers the phase relationships widen the image well beyond the
// speaker base, so rear positions read as "around" rather than just quieter.
class UhjEncoder
{
public:
    void reset()
    {
        qS.reset();
        qJ.reset();
        qY.reset();
    }

    // speakers: 4 speaker-bed channels; outL/outR overwritten, length n.
    void process (const float* const* speakers, float* outL, float* outR, int n)
    {
        // Speaker azimuths in radians; Y is left-positive so it takes -sin.
        constexpr float invSqrt2 = 0.70710678f;
        static const std::array<float, 4> cosAz = [] {
            std::array<float, 4> c {};
            for (int s = 0; s < 4; ++s) c[static_cast<size_t> (s)] = std::cos (quad::kSpeakerAzDeg[s] * quad::pi / 180.0f);
            return c;
        }();
        static const std::array<float, 4> negSinAz = [] {
            std::array<float, 4> c {};
            for (int s = 0; s < 4; ++s) c[static_cast<size_t> (s)] = -std::sin (quad::kSpeakerAzDeg[s] * quad::pi / 180.0f);
            return c;
        }();

        for (int i = 0; i < n; ++i)
        {
            float w = 0.0f, x = 0.0f, y = 0.0f;
            for (int s = 0; s < quad::kNumSpeakers; ++s)
            {
                const float v = speakers[s][i];
                w += v * invSqrt2;
                x += v * cosAz[static_cast<size_t> (s)];
                y += v * negSinAz[static_cast<size_t> (s)];
            }
            // Normalise the four coherent speaker feeds down to source level.
            w *= 0.5f; x *= 0.5f; y *= 0.5f;

            const float sTerm = 0.9396926f * w + 0.1855740f * x;
            const float jTerm = -0.3420201f * w + 0.5098604f * x;

            // The non-shifted terms (S and Y) pass through the reference-phase
            // allpass path so their phase stays matched to the +90-degree
            // shifted j term. One PhaseQuadrature instance per signal.
            const auto ps = qS.process (sTerm);
            const auto pj = qJ.process (jTerm);
            const auto py = qY.process (0.6554516f * y);

            const float d = pj.shifted + py.ref;
            outL[i] = 0.5f * (ps.ref + d);
            outR[i] = 0.5f * (ps.ref - d);
        }
    }

private:
    PhaseQuadrature qS, qJ, qY;
};

} // namespace qube
