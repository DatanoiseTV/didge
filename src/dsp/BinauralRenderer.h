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
#include <cstring>
#include <vector>

namespace qube
{

// Headphone virtualisation of the four quad speakers.
//
// Each speaker is a FIXED virtual source, so the whole renderer reduces to
// eight static head-model filters (4 speakers x 2 ears) plus eight static
// interaural delays — no HRTF dataset, no convolution, no zipper when the
// source moves (movement only changes the speaker-bed gains upstream).
//
// Head model per speaker/ear:
//   - ITD: Woodworth spherical-head formula, applied fully to the far ear
//     (near ear gets zero extra delay; only the interaural DIFFERENCE is
//     audible).
//   - Head shadow: first-order high shelf from the Brown-Duda spherical
//     model, H(s) = (alpha*s + w0) / (s + w0) with w0 = c/a. alpha depends
//     on the angle between the source and the ear axis: ~1.7 toward the
//     ear (slight HF boost), ~0.3 on the far side (HF cut).
//   - Front/back cue: the rear speakers get their HF response scaled down
//     (pinnae shadow approximation) so back positions read as "behind"
//     instead of mirroring the front.
class BinauralRenderer
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;

        constexpr double headRadius = 0.0875;   // metres
        constexpr double speedOfSound = 343.0;
        const double maxItd = (headRadius / speedOfSound) * (quad::pi / 2.0 + 1.0);
        maxDelaySamples = static_cast<int> (std::ceil (maxItd * fs)) + 4;

        for (int s = 0; s < quad::kNumSpeakers; ++s)
        {
            const double az = quad::kSpeakerAzDeg[s] * quad::pi / 180.0;
            const bool  rear = std::abs (quad::kSpeakerAzDeg[s]) > 90.0f;

            // Lateral angle (front/back folded) drives the ITD.
            const double lat = std::asin (std::abs (std::sin (az)));
            const double itd = (headRadius / speedOfSound) * (lat + std::sin (lat));
            const int itdSamps = static_cast<int> (std::round (itd * fs));

            for (int e = 0; e < 2; ++e)
            {
                auto& ch = chains[(size_t) s][(size_t) e];
                const double earAz = e == 0 ? -quad::pi / 2.0 : quad::pi / 2.0; // L, R
                const double cosToEar = std::cos (az - earAz);

                // Far ear (negative cos) gets the full ITD.
                ch.delaySamples = cosToEar < 0.0 ? itdSamps : 0;

                // Brown-Duda style alpha in [~0.3 .. ~1.7]; rear speakers
                // additionally darkened for the front/back spectral cue.
                double alpha = 1.0 + 0.7 * cosToEar;
                if (rear) alpha *= 0.6;
                alpha = std::max (0.12, alpha);

                // Bilinear transform of H(s) = (alpha*s + w0) / (s + w0).
                const double w0 = speedOfSound / headRadius;   // ~3920 rad/s
                const double twoFs = 2.0 * fs;
                const double a0 = twoFs + w0;
                ch.b0 = static_cast<float> ((twoFs * alpha + w0) / a0);
                ch.b1 = static_cast<float> ((w0 - twoFs * alpha) / a0);
                ch.a1 = static_cast<float> ((w0 - twoFs) / a0);

                ch.buffer.assign (static_cast<size_t> (maxDelaySamples), 0.0f);
                ch.writePos = 0;
                ch.z1In = ch.z1Out = 0.0f;
            }
        }
    }

    void reset()
    {
        for (auto& spk : chains)
            for (auto& ch : spk)
            {
                std::fill (ch.buffer.begin(), ch.buffer.end(), 0.0f);
                ch.z1In = ch.z1Out = 0.0f;
                ch.writePos = 0;
            }
    }

    // speakers: 4 pointers to speaker-bed channels; outL/outR: summed result
    // (overwritten). All buffers length n.
    void process (const float* const* speakers, float* outL, float* outR, int n)
    {
        std::memset (outL, 0, sizeof (float) * static_cast<size_t> (n));
        std::memset (outR, 0, sizeof (float) * static_cast<size_t> (n));

        for (int s = 0; s < quad::kNumSpeakers; ++s)
        {
            renderEar (chains[(size_t) s][0], speakers[s], outL, n);
            renderEar (chains[(size_t) s][1], speakers[s], outR, n);
        }

        // The four virtual speakers sum coherently at each ear; scale to sit
        // at roughly the same loudness as the quad bed.
        for (int i = 0; i < n; ++i) { outL[i] *= 0.7071f; outR[i] *= 0.7071f; }
    }

    // Interaural delay of a given speaker/ear, for tests.
    int delaySamplesFor (int speaker, int ear) const { return chains[(size_t) speaker][(size_t) ear].delaySamples; }
    // Filter gain at Nyquist (z = -1) of a given speaker/ear shelf, for tests.
    float hfGainFor (int speaker, int ear) const
    {
        const auto& ch = chains[(size_t) speaker][(size_t) ear];
        return (ch.b0 - ch.b1) / (1.0f - ch.a1);
    }

private:
    struct EarChain
    {
        std::vector<float> buffer;   // integer-sample interaural delay line
        int writePos = 0;
        int delaySamples = 0;
        float b0 = 1.0f, b1 = 0.0f, a1 = 0.0f;   // first-order shelf state
        float z1In = 0.0f, z1Out = 0.0f;
    };

    void renderEar (EarChain& ch, const float* in, float* out, int n)
    {
        const int size = static_cast<int> (ch.buffer.size());
        for (int i = 0; i < n; ++i)
        {
            ch.buffer[static_cast<size_t> (ch.writePos)] = in[i];
            int rp = ch.writePos - ch.delaySamples;
            if (rp < 0) rp += size;
            const float delayed = ch.buffer[static_cast<size_t> (rp)];
            ch.writePos = (ch.writePos + 1) % size;

            // First-order shelf: y = b0*x + b1*x1 - a1*y1
            const float y = ch.b0 * delayed + ch.b1 * ch.z1In - ch.a1 * ch.z1Out;
            ch.z1In = delayed;
            ch.z1Out = y;
            out[i] += y;
        }
    }

    std::array<std::array<EarChain, 2>, quad::kNumSpeakers> chains;
    double fs = 48000.0;
    int maxDelaySamples = 64;
};

} // namespace qube
