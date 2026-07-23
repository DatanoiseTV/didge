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
#include <vector>

namespace didge
{

// Small 4-line Householder FDN placing the instrument in a room; mono in,
// decorrelated stereo out. Kept deliberately modest — the instrument is the
// point, the room is air around it.
class Ambience
{
public:
    void prepare (double sampleRate)
    {
        fs = static_cast<float> (sampleRate);
        static constexpr float baseMs[kLines] = { 17.3f, 23.9f, 29.7f, 37.1f };
        for (int i = 0; i < kLines; ++i)
        {
            const int maxLen = static_cast<int> (baseMs[i] * 2.2f * 0.001f * fs) + 8;
            int size = 8;
            while (size < maxLen) size <<= 1;
            line[i].assign (static_cast<size_t> (size), 0.0f);
            mask[i] = size - 1;
            w[i] = 0;
            baseSamples[i] = baseMs[i] * 0.001f * fs;
        }
        clear();
    }

    void clear()
    {
        for (int i = 0; i < kLines; ++i)
        {
            std::fill (line[i].begin(), line[i].end(), 0.0f);
            damp[i] = 0.0f;
        }
    }

    void setSize (float size01)   { sizeScale = 0.55f + 1.45f * size01; }
    void setDecay (float size01)  { fb = 0.35f + 0.38f * size01; }

    void process (float in, float& outL, float& outR)
    {
        float r[kLines];
        for (int i = 0; i < kLines; ++i)
        {
            const float d  = baseSamples[i] * sizeScale;
            const int   di = static_cast<int> (d);
            const float fr = d - static_cast<float> (di);
            const int i0 = (w[i] - di + (mask[i] + 1)) & mask[i];
            const int i1 = (i0 - 1 + (mask[i] + 1)) & mask[i];
            const float a = line[i][static_cast<size_t> (i0)];
            const float b = line[i][static_cast<size_t> (i1)];
            float y = a + fr * (b - a);
            damp[i] += 0.35f * (y - damp[i]);
            r[i] = damp[i] + 1.0e-18f;   // denormal guard
        }

        // Householder feedback: y_i - (2/N) * sum.
        const float s = 0.5f * (r[0] + r[1] + r[2] + r[3]);
        for (int i = 0; i < kLines; ++i)
        {
            const float fbIn = in + fb * (r[i] - s);
            line[i][static_cast<size_t> (w[i])] = fbIn;
            w[i] = (w[i] + 1) & mask[i];
        }

        outL = 0.7f * (r[0] + 0.6f * r[2]);
        outR = 0.7f * (r[1] + 0.6f * r[3]);
    }

private:
    static constexpr int kLines = 4;
    float fs = 48000.0f;
    std::vector<float> line[kLines];
    int mask[kLines] {}, w[kLines] {};
    float baseSamples[kLines] {}, damp[kLines] {};
    float sizeScale = 1.0f, fb = 0.6f;
};

} // namespace didge
