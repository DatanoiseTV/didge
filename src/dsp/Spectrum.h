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
#include <vector>

namespace didge
{

// Output analyser for the display.
//
// This started as a constant-Q filter bank, which was the wrong instrument for
// the job: at a quarter-octave the bands are wider than the 73 Hz spacing of
// this drone's harmonics above about the third, so everything above it merged
// into an envelope and you could not see what the model was doing. An FFT
// resolves them — and, per sample, actually costs less than thirty-two
// filters did.
//
// The transform is written out here rather than pulled from a framework so the
// DSP core stays dependency-free and the offline tests keep building without
// one.
class Spectrum
{
public:
    static constexpr int kBins    = 256;      // display points, log spaced
    static constexpr int kPeaks   = 6;        // tracked partials
    static constexpr float kLoHz  = 40.0f;
    static constexpr float kHiHz  = 16000.0f;
    static constexpr float kFloorDb = -108.0f;

    void prepare (double sampleRate)
    {
        fs = static_cast<float> (sampleRate);

        // Aim for roughly 12 Hz per bin whatever the rate, so the harmonics of
        // a low drone stay separated; round up to a power of two.
        int want = static_cast<int> (fs / 12.0f);
        n = 1024;
        while (n < want && n < 16384) n <<= 1;
        half = n / 2;
        hop = std::max (256, n / 4);

        ring.assign (static_cast<size_t> (n), 0.0f);
        re.assign (static_cast<size_t> (n), 0.0f);
        im.assign (static_cast<size_t> (n), 0.0f);
        mag.assign (static_cast<size_t> (half), 0.0f);
        window.resize (static_cast<size_t> (n));
        for (int i = 0; i < n; ++i)
            window[static_cast<size_t> (i)] =
                0.5f - 0.5f * std::cos (6.2831853f * i / (n - 1));

        buildBitReverse();
        buildBinMap();
        reset();
    }

    void reset()
    {
        std::fill (ring.begin(), ring.end(), 0.0f);
        writePos = 0;
        sinceHop = 0;
        for (int i = 0; i < kBins; ++i) { level[i] = kFloorDb; peakHold[i] = kFloorDb; }
        for (int i = 0; i < kPeaks; ++i) { peakHz[i] = 0.0f; peakDb[i] = kFloorDb; }
    }

    void push (float x)
    {
        ring[static_cast<size_t> (writePos)] = x;
        writePos = (writePos + 1) % n;
        if (++sinceHop >= hop)
        {
            sinceHop = 0;
            analyse();
        }
    }

    float binDb (int i)     const { return level[i]; }
    float binPeakDb (int i) const { return peakHold[i]; }
    float binHz (int i)     const { return binFreq[i]; }
    float peakFreq (int i)  const { return peakHz[i]; }
    float peakLevel (int i) const { return peakDb[i]; }

private:
    void buildBitReverse()
    {
        rev.assign (static_cast<size_t> (n), 0);
        int bits = 0;
        while ((1 << bits) < n) ++bits;
        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < bits; ++b)
                if (i & (1 << b)) r |= 1 << (bits - 1 - b);
            rev[static_cast<size_t> (i)] = r;
        }
    }

    // Log-spaced display points, each mapped to the FFT bins under it. At the
    // bottom a display point is narrower than a bin, so it simply takes the
    // nearest one and the low harmonics stand as separate lines.
    void buildBinMap()
    {
        const float df = fs / static_cast<float> (n);
        const float hi = std::min (kHiHz, 0.48f * fs);
        for (int i = 0; i < kBins; ++i)
        {
            const float t0 = (i - 0.5f) / (kBins - 1);
            const float t1 = (i + 0.5f) / (kBins - 1);
            const float f  = kLoHz * std::pow (hi / kLoHz, static_cast<float> (i) / (kBins - 1));
            binFreq[i] = f;
            binLo[i] = std::max (1, static_cast<int> (kLoHz * std::pow (hi / kLoHz, t0) / df));
            binHi[i] = std::min (half - 1, static_cast<int> (kLoHz * std::pow (hi / kLoHz, t1) / df));
            if (binHi[i] < binLo[i]) binHi[i] = binLo[i];
        }
    }

    void analyse()
    {
        // Windowed copy, oldest sample first, bit-reversed into place.
        for (int i = 0; i < n; ++i)
        {
            const float s = ring[static_cast<size_t> ((writePos + i) % n)]
                          * window[static_cast<size_t> (i)];
            const int d = rev[static_cast<size_t> (i)];
            re[static_cast<size_t> (d)] = s;
            im[static_cast<size_t> (d)] = 0.0f;
        }

        // Iterative radix-2 Cooley-Tukey.
        for (int len = 2; len <= n; len <<= 1)
        {
            const float ang = -6.2831853f / static_cast<float> (len);
            const float wr = std::cos (ang), wi = std::sin (ang);
            for (int i = 0; i < n; i += len)
            {
                float cr = 1.0f, ci = 0.0f;
                for (int j = 0; j < len / 2; ++j)
                {
                    const size_t a = static_cast<size_t> (i + j);
                    const size_t b = static_cast<size_t> (i + j + len / 2);
                    const float tr = re[b] * cr - im[b] * ci;
                    const float ti = re[b] * ci + im[b] * cr;
                    re[b] = re[a] - tr; im[b] = im[a] - ti;
                    re[a] += tr;        im[a] += ti;
                    const float ncr = cr * wr - ci * wi;
                    ci = cr * wi + ci * wr;
                    cr = ncr;
                }
            }
        }

        const float norm = 2.0f / (static_cast<float> (n) * 0.5f);   // Hann coherent gain
        for (int k = 0; k < half; ++k)
            mag[static_cast<size_t> (k)] =
                std::sqrt (re[static_cast<size_t> (k)] * re[static_cast<size_t> (k)]
                         + im[static_cast<size_t> (k)] * im[static_cast<size_t> (k)]) * norm;

        // Display points take the strongest bin under them: averaging would
        // bury exactly the narrow partials this exists to show.
        for (int i = 0; i < kBins; ++i)
        {
            float m = 0.0f;
            for (int k = binLo[i]; k <= binHi[i]; ++k)
                m = std::max (m, mag[static_cast<size_t> (k)]);
            const float db = 20.0f * std::log10 (std::max (1.0e-7f, m));
            level[i] = std::max (kFloorDb, db);
            peakHold[i] = std::max (peakHold[i] - kPeakFallDb, level[i]);
        }

        trackPeaks();
    }

    // Strongest spectral peaks, with the true frequency recovered by fitting a
    // parabola across the bin and its neighbours -- otherwise a partial can
    // only ever be reported to the nearest bin, which at this resolution is
    // several cents wide down where the drone lives.
    void trackPeaks()
    {
        float bestMag[kPeaks] = {};
        float bestHz[kPeaks] = {};
        for (int i = 0; i < kPeaks; ++i) { bestMag[i] = 0.0f; bestHz[i] = 0.0f; }

        const float df = fs / static_cast<float> (n);
        const int kMax = std::min (half - 2, static_cast<int> (kHiHz / df));
        for (int k = 2; k < kMax; ++k)
        {
            const float m = mag[static_cast<size_t> (k)];
            if (m <= mag[static_cast<size_t> (k - 1)] || m < mag[static_cast<size_t> (k + 1)])
                continue;

            const float a = mag[static_cast<size_t> (k - 1)];
            const float c = mag[static_cast<size_t> (k + 1)];
            const float den = a - 2.0f * m + c;
            const float d = std::abs (den) > 1.0e-12f
                          ? std::max (-0.5f, std::min (0.5f, 0.5f * (a - c) / den))
                          : 0.0f;
            const float f = (static_cast<float> (k) + d) * df;

            for (int p = 0; p < kPeaks; ++p)
            {
                if (m > bestMag[p])
                {
                    for (int q = kPeaks - 1; q > p; --q)
                    {
                        bestMag[q] = bestMag[q - 1];
                        bestHz[q]  = bestHz[q - 1];
                    }
                    bestMag[p] = m;
                    bestHz[p]  = f;
                    break;
                }
            }
        }

        for (int p = 0; p < kPeaks; ++p)
        {
            peakHz[p] = bestHz[p];
            peakDb[p] = bestMag[p] > 0.0f
                      ? std::max (kFloorDb, 20.0f * std::log10 (bestMag[p]))
                      : kFloorDb;
        }
    }

    static constexpr float kPeakFallDb = 0.55f;   // per hop

    float fs = 48000.0f;
    int n = 4096, half = 2048, hop = 1024;
    int writePos = 0, sinceHop = 0;

    std::vector<float> ring, re, im, mag, window;
    std::vector<int> rev;

    int   binLo[kBins] {}, binHi[kBins] {};
    float binFreq[kBins] {};
    float level[kBins] {}, peakHold[kBins] {};
    float peakHz[kPeaks] {}, peakDb[kPeaks] {};
};

} // namespace didge
