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
#include "Motion.h"
#include "BinauralRenderer.h"
#include "UhjEncoder.h"
#include "RoomVerb.h"

#include <array>
#include <atomic>
#include <vector>

namespace qube
{

// All engine parameters in real units, produced from the APVTS once per block
// by the processor. The engine itself has no JUCE dependency so the DSP tests
// build it standalone.
struct EngineParams
{
    float posX = 0.0f, posY = 0.5f;
    float spread = 0.15f;
    float rotateDeg = 0.0f;

    int   motionMode = 0;            // Motion::Mode
    float motionRateHz = 0.25f;
    bool  motionSync = false;
    double motionDivBeats = 4.0;
    float motionRadius = 0.5f;
    float motionPhaseDeg = 0.0f;
    bool  motionReverse = false;

    float distAmount = 0.5f;
    float airAbsorb = 0.5f;
    float doppler = 0.0f;
    float roomMix = 0.25f;
    float roomSize = 0.5f;
    float roomDamp = 0.5f;

    int   outputMode = 0;            // ids::OutputMode
    float masterGainLin = 1.0f;
};

struct TransportInfo
{
    double bpm = 120.0;
    double ppqPosition = 0.0;
    bool   playing = false;
};

// Rendered output layout the engine actually used this block (resolves the
// "Auto" mode against the bus width). Mirrored to the UI.
enum class RenderMode { quad = 0, binaural, uhj, stereoMix };

class SpatialEngine
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    // in: numIn (1 or 2) input channel pointers. out: numOut (2 or 4) output
    // channel pointers. In-place aliasing (out == in) is supported — inputs
    // are copied to internal storage first thing.
    void process (const float* const* in, int numIn,
                  float* const* out, int numOut,
                  int numSamples,
                  const EngineParams& params,
                  const TransportInfo& transport);

    // ---- UI feed (lock-free) ---------------------------------------------
    // Peak-held speaker-bed magnitudes; consuming zeroes the slot so the UI
    // sees the max across all blocks since its last poll.
    float consumeSpeakerPeak (int i)
    {
        return speakerPeak[static_cast<size_t> (i & 3)].exchange (0.0f, std::memory_order_relaxed);
    }
    float consumeOutPeak (int ch)
    {
        return outPeak[static_cast<size_t> (ch & 3)].exchange (0.0f, std::memory_order_relaxed);
    }
    // Actual (post-motion, post-rotate) source position, for the UI puck.
    float currentX() const { return uiPosX.load (std::memory_order_relaxed); }
    float currentY() const { return uiPosY.load (std::memory_order_relaxed); }
    // What the output stage rendered last block.
    RenderMode lastRenderMode() const
    {
        return static_cast<RenderMode> (uiRenderMode.load (std::memory_order_relaxed));
    }

    // Test access: current motion phase in [0,1).
    double motionPhase() const { return phase; }

private:
    void renderOutput (RenderMode mode, float* const* out, int numOut, int n, float gainStart, float gainEnd);

    static constexpr int kSub = 32;              // gain-update subblock

    double fs = 48000.0;
    int    maxBlock = 0;

    // Position smoothing (one-pole toward target, updated per subblock).
    float smX = 0.0f, smY = 0.5f;
    float posSmoothK = 0.1f;

    // Motion state.
    Motion motion;
    double phase = 0.0;

    // Per input channel: current speaker gains (ramped between subblocks).
    std::array<std::array<float, 4>, 2> curGains {};

    // Doppler delay lines (one per input channel).
    struct DelayLine
    {
        std::vector<float> buffer;
        int writePos = 0;
        float smDelay = 0.0f;
    };
    std::array<DelayLine, 2> dop;
    float dopSmoothK = 0.001f;

    // Air-absorption one-pole per input channel.
    std::array<float, 2> airState {};
    float airCoeff = 0.0f;               // updated per subblock
    float curDistGain = 1.0f;

    // Master gain smoothing.
    float smMasterGain = 1.0f;

    // Work buffers.
    std::vector<float> srcBuf[2];        // conditioned input channels
    std::vector<float> bedBuf[4];        // quad speaker bed
    std::vector<float> sendBuf;          // room send (mono)
    std::vector<float> tmpA, tmpB;       // output-stage crossfade scratch
    std::vector<float> fadeA[4], fadeB[4];

    RoomVerb room;
    BinauralRenderer binaural;
    UhjEncoder uhj;

    // Output-stage mode handling: crossfade over one block on change.
    int lastMode = -1;

    // UI atomics.
    std::array<std::atomic<float>, 4> speakerPeak {};
    std::array<std::atomic<float>, 4> outPeak {};
    std::atomic<float> uiPosX { 0.0f }, uiPosY { 0.5f };
    std::atomic<int>   uiRenderMode { 0 };
};

} // namespace qube
