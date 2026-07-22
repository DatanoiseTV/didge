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
#include <vector>

namespace qube
{

// Small 8-line FDN room, with FOUR decorrelated outputs aligned to the quad
// speaker bed. The room always renders into the speaker bed — the output
// stage (quad / binaural / UHJ / downmix) then treats early-reflection energy
// exactly like direct sound, which is what makes the binaural mode read as
// "a source in a room" instead of "a dry pan plus stereo reverb".
//
// Topology: 8 delay lines, Householder feedback (energy-preserving), one-pole
// damping in each loop, per-line decay gains derived from a T60 target so the
// tail length is size-consistent across lines.
class RoomVerb
{
public:
    void prepare (double sampleRate, int maxBlock)
    {
        fs = sampleRate;
        (void) maxBlock;
        for (int i = 0; i < kLines; ++i)
        {
            // Max size scale is 1.6 -> allocate for it with headroom.
            const int maxLen = static_cast<int> (kBaseDelayMs[i] * 1.7 * 0.001 * fs) + 8;
            lines[(size_t) i].buffer.assign (static_cast<size_t> (maxLen), 0.0f);
            lines[(size_t) i].writePos = 0;
            lines[(size_t) i].damp = 0.0f;
        }
        setParams (0.5f, 0.5f);
        reset();
    }

    void reset()
    {
        for (auto& l : lines)
        {
            std::fill (l.buffer.begin(), l.buffer.end(), 0.0f);
            l.damp = 0.0f;
            l.writePos = 0;
        }
    }

    // size01 scales both the line lengths and the decay time; damp01 controls
    // in-loop HF damping. Cheap enough to call per block.
    void setParams (float size01, float damp01)
    {
        const float size = std::clamp (size01, 0.0f, 1.0f);
        const float scale = 0.45f + 1.15f * size;          // line length scale
        const float t60 = 0.35f + 2.6f * size * size;      // seconds

        for (int i = 0; i < kLines; ++i)
        {
            auto& l = lines[(size_t) i];
            l.length = std::min (static_cast<int> (l.buffer.size()) - 1,
                                 std::max (16, static_cast<int> (kBaseDelayMs[i] * scale * 0.001 * fs)));
            const float loopSeconds = static_cast<float> (l.length) / static_cast<float> (fs);
            l.gain = std::pow (10.0f, -3.0f * loopSeconds / t60);
        }
        const float d = std::clamp (damp01, 0.0f, 1.0f);
        // One-pole lowpass coefficient inside the loop: 0 -> bright, 1 -> dark.
        dampCoeff = 0.12f + 0.78f * d;
    }

    // in: mono send, length n. out[4]: ADDED to (not overwritten), scaled by
    // wet. The four outputs are distinct sign-pattern mixes of the lines so
    // each speaker gets a decorrelated tail.
    void process (const float* in, std::array<float*, 4> out, float wet, int n)
    {
        if (wet <= 1.0e-5f)
            wet = 0.0f;   // still run the tank so the tail continues naturally

        for (int i = 0; i < n; ++i)
        {
            // Read all line outputs first.
            std::array<float, kLines> v;
            float sum = 0.0f;
            for (int k = 0; k < kLines; ++k)
            {
                auto& l = lines[(size_t) k];
                int rp = l.writePos - l.length;
                if (rp < 0) rp += static_cast<int> (l.buffer.size());
                v[static_cast<size_t> (k)] = l.buffer[static_cast<size_t> (rp)];
                sum += v[static_cast<size_t> (k)];
            }

            // Householder feedback: y_k = v_k - (2/N) * sum. Energy preserving
            // for any N; dense mixing after a couple of passes.
            const float h = sum * (2.0f / kLines);
            const float x = in[i];

            for (int k = 0; k < kLines; ++k)
            {
                auto& l = lines[(size_t) k];
                float fb = (v[static_cast<size_t> (k)] - h) * l.gain;
                // In-loop damping (one-pole lowpass).
                l.damp += (fb - l.damp) * (1.0f - dampCoeff);
                float w = l.damp + x * kInputSigns[k] * 0.35f;
                l.buffer[static_cast<size_t> (l.writePos)] = w;
                l.writePos = (l.writePos + 1) % static_cast<int> (l.buffer.size());
            }

            if (wet > 0.0f)
            {
                for (int o = 0; o < 4; ++o)
                {
                    float acc = 0.0f;
                    for (int k = 0; k < kLines; ++k)
                        acc += v[static_cast<size_t> (k)] * kOutSigns[o][k];
                    out[static_cast<size_t> (o)][i] += acc * wet * 0.30f;
                }
            }
        }
    }

private:
    static constexpr int kLines = 8;
    // Mutually prime-ish base lengths (ms) spread over ~19..93 ms.
    static constexpr float kBaseDelayMs[kLines] = { 19.7f, 27.9f, 36.1f, 44.3f, 53.9f, 63.1f, 78.7f, 93.3f };
    static constexpr float kInputSigns[kLines] = { 1, -1, 1, 1, -1, 1, -1, -1 };
    // Hadamard-derived sign rows -> orthogonal output mixes (decorrelated).
    static constexpr float kOutSigns[4][kLines] = {
        {  1,  1,  1,  1,  1,  1,  1,  1 },
        {  1, -1,  1, -1,  1, -1,  1, -1 },
        {  1,  1, -1, -1,  1,  1, -1, -1 },
        {  1, -1, -1,  1,  1, -1, -1,  1 },
    };

    struct Line
    {
        std::vector<float> buffer;
        int writePos = 0;
        int length = 480;
        float gain = 0.7f;
        float damp = 0.0f;
    };

    std::array<Line, kLines> lines;
    float dampCoeff = 0.5f;
    double fs = 48000.0;
};

} // namespace qube
