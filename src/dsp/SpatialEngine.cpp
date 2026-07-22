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

#include "SpatialEngine.h"

#include <cassert>
#include <cstring>

namespace qube
{

namespace
{
    inline float smoothstep01 (float t)
    {
        t = std::clamp (t, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    // Cubic (Catmull-Rom) fractional delay read.
    inline float cubicRead (const std::vector<float>& buf, int writePos, float delaySamples)
    {
        const int size = static_cast<int> (buf.size());
        float rp = static_cast<float> (writePos) - delaySamples;
        while (rp < 0.0f) rp += static_cast<float> (size);
        const int i1 = static_cast<int> (rp);
        const float frac = rp - static_cast<float> (i1);
        const int i0 = (i1 - 1 + size) % size;
        const int i2 = (i1 + 1) % size;
        const int i3 = (i1 + 2) % size;
        const float y0 = buf[static_cast<size_t> (i0)], y1 = buf[static_cast<size_t> (i1)];
        const float y2 = buf[static_cast<size_t> (i2)], y3 = buf[static_cast<size_t> (i3)];
        const float a = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        const float b = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c = 0.5f * (y2 - y0);
        return ((a * frac + b) * frac + c) * frac + y1;
    }
} // namespace

void SpatialEngine::prepare (double sampleRate, int maxBlockSize)
{
    fs = sampleRate;
    maxBlock = maxBlockSize;

    // Position smoothing: tau ~12 ms, stepped once per kSub samples.
    posSmoothK = 1.0f - std::exp (-static_cast<float> (kSub) / (0.012f * static_cast<float> (fs)));
    // Doppler delay smoothing: tau ~70 ms, per sample. Slow enough that a
    // posX jump becomes a pitch glide instead of a click.
    dopSmoothK = 1.0f - std::exp (-1.0f / (0.070f * static_cast<float> (fs)));

    const int dopLen = static_cast<int> (0.060 * fs) + 8;   // 25 ms max + margin
    for (auto& d : dop)
    {
        d.buffer.assign (static_cast<size_t> (dopLen), 0.0f);
        d.writePos = 0;
        d.smDelay = 0.0f;
    }

    for (auto& b : srcBuf) b.assign (static_cast<size_t> (maxBlock), 0.0f);
    for (auto& b : bedBuf) b.assign (static_cast<size_t> (maxBlock), 0.0f);
    sendBuf.assign (static_cast<size_t> (maxBlock), 0.0f);
    tmpA.assign (static_cast<size_t> (maxBlock), 0.0f);
    tmpB.assign (static_cast<size_t> (maxBlock), 0.0f);
    for (auto& b : fadeA) b.assign (static_cast<size_t> (maxBlock), 0.0f);
    for (auto& b : fadeB) b.assign (static_cast<size_t> (maxBlock), 0.0f);

    room.prepare (fs, maxBlock);
    binaural.prepare (fs);
    uhj.reset();
    motion.reset();

    reset();
}

void SpatialEngine::reset()
{
    for (auto& d : dop)
    {
        std::fill (d.buffer.begin(), d.buffer.end(), 0.0f);
        d.writePos = 0;
    }
    airState = {};
    for (auto& g : curGains) g = {};
    room.reset();
    binaural.reset();
    uhj.reset();
    lastMode = -1;
    smMasterGain = 1.0f;
}

void SpatialEngine::process (const float* const* in, int numIn,
                             float* const* out, int numOut,
                             int n,
                             const EngineParams& p,
                             const TransportInfo& t)
{
    assert (n <= maxBlock);
    numIn  = std::clamp (numIn, 1, 2);
    numOut = std::clamp (numOut, 2, 4);

    // ---- 0) Copy inputs (buffers may alias outputs in-place) -------------
    for (int c = 0; c < numIn; ++c)
        std::memcpy (srcBuf[c].data(), in[c], sizeof (float) * static_cast<size_t> (n));

    // ---- 1) Motion phase for this block ----------------------------------
    const auto mode = static_cast<Motion::Mode> (p.motionMode);
    const double dir = p.motionReverse ? -1.0 : 1.0;
    const double phase0 = p.motionPhaseDeg / 360.0;
    double cycleSeconds;
    if (p.motionSync && t.bpm > 1.0)
        cycleSeconds = p.motionDivBeats * 60.0 / t.bpm;
    else
        cycleSeconds = 1.0 / std::max (0.01, static_cast<double> (p.motionRateHz));

    const double blockCycles = (static_cast<double> (n) / fs) / cycleSeconds;
    if (p.motionSync && t.playing)
    {
        // Transport-locked: deterministic phase from the song position, so
        // bounces and loops land identically every pass.
        phase = dir * (t.ppqPosition / p.motionDivBeats) + phase0;
    }
    else
    {
        phase += dir * blockCycles;
    }
    phase -= std::floor (phase);

    // ---- 2) Per-subblock parameter update + conditioning + bed -----------
    for (auto& b : bedBuf)
        std::memset (b.data(), 0, sizeof (float) * static_cast<size_t> (n));

    const float rotRad = p.rotateDeg * quad::pi / 180.0f;
    const float cosR = std::cos (rotRad), sinR = std::sin (rotRad);

    int done = 0;
    while (done < n)
    {
        const int len = std::min (kSub, n - done);
        const double subPhase = phase - dir * (static_cast<double> (n - done) / fs) / cycleSeconds;
        const auto off = motion.offsetFor (mode, subPhase - std::floor (subPhase), p.motionRadius,
                                           (static_cast<double> (len) / fs) / cycleSeconds);

        // Base position + motion offset, then scene rotation about the room
        // centre, then clamp slightly outside the speaker square.
        float px = p.posX + off.dx;
        float py = p.posY + off.dy;
        const float rx = cosR * px + sinR * py;
        const float ry = -sinR * px + cosR * py;
        px = std::clamp (rx, -1.2f, 1.2f);
        py = std::clamp (ry, -1.2f, 1.2f);

        // Smooth toward target (removes automation staircases).
        smX += (px - smX) * posSmoothK;
        smY += (py - smY) * posSmoothK;

        const float dist = std::sqrt (smX * smX + smY * smY);
        const float az = std::atan2 (smX, smY);
        const float interior = smoothstep01 (dist / 0.30f);

        // Distance conditioning targets.
        const float distGainT = 1.0f / (1.0f + 3.0f * p.distAmount * dist);
        const float fc = std::clamp (20000.0f * std::exp (-2.2f * p.airAbsorb * dist), 500.0f, 20000.0f);
        airCoeff = 1.0f - std::exp (-2.0f * quad::pi * fc / static_cast<float> (fs));
        // Above ~18 kHz the filter is only shading the top octave; blend it
        // out entirely so a centred/close source stays bit-transparent.
        const float airBlend = std::clamp ((fc - 18000.0f) / 2000.0f, 0.0f, 1.0f);
        const float dopTargetSamples = dist * p.doppler * 0.020f * static_cast<float> (fs);

        // Speaker-gain targets per input channel. Stereo inputs keep their
        // L/R identity: each channel is its own virtual source, offset from
        // the centre azimuth by the spread angle.
        std::array<std::array<float, 4>, 2> targetGains;
        if (numIn == 2)
        {
            const float sep = p.spread * quad::pi / 4.0f;
            targetGains[0] = quad::panGains (az - sep, p.spread * 0.5f, interior);
            targetGains[1] = quad::panGains (az + sep, p.spread * 0.5f, interior);
        }
        else
        {
            targetGains[0] = quad::panGains (az, p.spread, interior);
            targetGains[1] = targetGains[0];
        }

        // Per-sample conditioning + bed accumulation with gain ramps.
        const float invLen = 1.0f / static_cast<float> (len);
        for (int c = 0; c < numIn; ++c)
        {
            auto& d = dop[static_cast<size_t> (c)];
            float* src = srcBuf[c].data() + done;
            const int dsize = static_cast<int> (d.buffer.size());

            for (int i = 0; i < len; ++i)
            {
                // Doppler: write dry, read at the smoothed distance delay.
                d.buffer[static_cast<size_t> (d.writePos)] = src[i];
                d.smDelay += (dopTargetSamples - d.smDelay) * dopSmoothK;
                // +3 samples of guard keeps the cubic interpolator behind the
                // write head even at zero doppler (62 us at 48 kHz).
                float v = cubicRead (d.buffer, d.writePos, d.smDelay + 3.0f);
                d.writePos = (d.writePos + 1) % dsize;

                // Air absorption (one-pole LP), then distance gain.
                auto& lp = airState[static_cast<size_t> (c)];
                lp += (v - lp) * airCoeff;
                v = lp + (v - lp) * airBlend;

                const float ramp = static_cast<float> (i + 1) * invLen;
                const float dg = curDistGain + (distGainT - curDistGain) * ramp;
                src[i] = v * dg;

                // Bed accumulate with per-sample gain ramp.
                auto& cg = curGains[static_cast<size_t> (c)];
                for (int s = 0; s < 4; ++s)
                {
                    const float g = cg[static_cast<size_t> (s)]
                                  + (targetGains[static_cast<size_t> (c)][static_cast<size_t> (s)] - cg[static_cast<size_t> (s)]) * ramp;
                    bedBuf[s][static_cast<size_t> (done + i)] += src[i] * g * (numIn == 2 ? 0.7071f : 1.0f);
                }
            }
            curGains[static_cast<size_t> (c)] = targetGains[static_cast<size_t> (c)];
        }
        curDistGain = distGainT;

        done += len;

        if (done >= n)
        {
            uiPosX.store (smX, std::memory_order_relaxed);
            uiPosY.store (smY, std::memory_order_relaxed);
        }
    }

    // ---- 3) Room send + reverb into the bed -------------------------------
    // Send taken post-air, pre-distance-gain would keep the wet level constant
    // with distance; we take post-everything and boost the send with distance
    // instead, which reads the same but keeps one code path.
    const float distNow = std::sqrt (smX * smX + smY * smY);
    const float sendGain = 0.5f + 0.5f * std::min (distNow, 1.2f);
    for (int i = 0; i < n; ++i)
    {
        float s = srcBuf[0][static_cast<size_t> (i)];
        if (numIn == 2) s = 0.5f * (s + srcBuf[1][static_cast<size_t> (i)]);
        sendBuf[static_cast<size_t> (i)] = s * sendGain;
    }
    room.setParams (p.roomSize, p.roomDamp);
    room.process (sendBuf.data(),
                  { bedBuf[0].data(), bedBuf[1].data(), bedBuf[2].data(), bedBuf[3].data() },
                  p.roomMix, n);

    // ---- 4) Speaker-bed metering ------------------------------------------
    for (int s = 0; s < 4; ++s)
    {
        float pk = 0.0f;
        const float* b = bedBuf[s].data();
        for (int i = 0; i < n; ++i) pk = std::max (pk, std::abs (b[i]));
        auto& slot = speakerPeak[static_cast<size_t> (s)];
        float cur = slot.load (std::memory_order_relaxed);
        while (pk > cur && ! slot.compare_exchange_weak (cur, pk, std::memory_order_relaxed)) {}
    }

    // ---- 5) Output stage ---------------------------------------------------
    RenderMode rm;
    switch (p.outputMode)
    {
        case 1:  rm = RenderMode::quad; break;
        case 2:  rm = RenderMode::binaural; break;
        case 3:  rm = RenderMode::uhj; break;
        case 4:  rm = RenderMode::stereoMix; break;
        default: rm = numOut >= 4 ? RenderMode::quad : RenderMode::binaural; break;
    }
    if (numOut < 4 && rm == RenderMode::quad)
        rm = RenderMode::binaural;   // quad cannot render on a stereo bus

    const float gainT = p.masterGainLin;
    const float gainS = smMasterGain;
    smMasterGain = gainT;

    const int rmInt = static_cast<int> (rm);
    if (lastMode >= 0 && lastMode != rmInt)
    {
        // Mode switch: render both stages and crossfade over this block.
        std::array<float*, 4> a { fadeA[0].data(), fadeA[1].data(), fadeA[2].data(), fadeA[3].data() };
        std::array<float*, 4> b { fadeB[0].data(), fadeB[1].data(), fadeB[2].data(), fadeB[3].data() };
        renderOutput (static_cast<RenderMode> (lastMode), a.data(), numOut, n, gainS, gainT);
        renderOutput (rm, b.data(), numOut, n, gainS, gainT);
        const float invN = 1.0f / static_cast<float> (n);
        for (int c = 0; c < numOut; ++c)
            for (int i = 0; i < n; ++i)
            {
                const float f = static_cast<float> (i) * invN;
                out[c][i] = a[static_cast<size_t> (c)][i] * (1.0f - f) + b[static_cast<size_t> (c)][i] * f;
            }
    }
    else
    {
        renderOutput (rm, out, numOut, n, gainS, gainT);
    }
    lastMode = rmInt;
    uiRenderMode.store (rmInt, std::memory_order_relaxed);

    // ---- 6) Output metering ------------------------------------------------
    for (int c = 0; c < numOut; ++c)
    {
        float pk = 0.0f;
        for (int i = 0; i < n; ++i) pk = std::max (pk, std::abs (out[c][i]));
        auto& slot = outPeak[static_cast<size_t> (c)];
        float cur = slot.load (std::memory_order_relaxed);
        while (pk > cur && ! slot.compare_exchange_weak (cur, pk, std::memory_order_relaxed)) {}
    }
}

void SpatialEngine::renderOutput (RenderMode mode, float* const* out, int numOut, int n,
                                  float gainStart, float gainEnd)
{
    const float invN = 1.0f / static_cast<float> (n);
    auto gainAt = [gainStart, gainEnd, invN] (int i)
    {
        return gainStart + (gainEnd - gainStart) * static_cast<float> (i) * invN;
    };

    const float* bl = bedBuf[0].data();
    const float* br = bedBuf[1].data();
    const float* rl = bedBuf[2].data();
    const float* rr = bedBuf[3].data();

    switch (mode)
    {
        case RenderMode::quad:
            for (int c = 0; c < numOut && c < 4; ++c)
            {
                const float* b = bedBuf[c].data();
                for (int i = 0; i < n; ++i)
                    out[c][i] = b[i] * gainAt (i);
            }
            for (int c = 4; c < numOut; ++c)
                std::memset (out[c], 0, sizeof (float) * static_cast<size_t> (n));
            break;

        case RenderMode::binaural:
        {
            const float* spk[4] = { bl, br, rl, rr };
            binaural.process (spk, tmpA.data(), tmpB.data(), n);
            for (int i = 0; i < n; ++i)
            {
                const float g = gainAt (i);
                out[0][i] = tmpA[static_cast<size_t> (i)] * g;
                out[1][i] = tmpB[static_cast<size_t> (i)] * g;
            }
            for (int c = 2; c < numOut; ++c)
                std::memset (out[c], 0, sizeof (float) * static_cast<size_t> (n));
            break;
        }

        case RenderMode::uhj:
        {
            const float* spk[4] = { bl, br, rl, rr };
            uhj.process (spk, tmpA.data(), tmpB.data(), n);
            for (int i = 0; i < n; ++i)
            {
                const float g = gainAt (i);
                out[0][i] = tmpA[static_cast<size_t> (i)] * g;
                out[1][i] = tmpB[static_cast<size_t> (i)] * g;
            }
            for (int c = 2; c < numOut; ++c)
                std::memset (out[c], 0, sizeof (float) * static_cast<size_t> (n));
            break;
        }

        case RenderMode::stereoMix:
            // Equal-power fold of the rears into the fronts.
            for (int i = 0; i < n; ++i)
            {
                const float g = gainAt (i);
                out[0][i] = (bl[i] + 0.7071f * rl[i]) * g;
                out[1][i] = (br[i] + 0.7071f * rr[i]) * g;
            }
            for (int c = 2; c < numOut; ++c)
                std::memset (out[c], 0, sizeof (float) * static_cast<size_t> (n));
            break;
    }
}

} // namespace qube
